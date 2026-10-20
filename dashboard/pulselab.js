// pulselab.js
// Talks to PulseLab over Bluetooth (Web Bluetooth, Chrome / Edge) and decodes
// what it sends. The byte layouts are the same as firmware/08_ble/ble_packets.h.
// Also has a demo mode that makes up data, so the page can be shown without
// the board. Works in the browser and in Node (for tests/test_ble_packets).

(function (root) {
  "use strict";

  const UUID = {
    hrService: 0x180d,
    hrMeasurement: 0x2a37,
    service: "7e1a0001-5a4e-4c2f-9d3b-2b7c1e5a9f10",
    vitals: "7e1a0002-5a4e-4c2f-9d3b-2b7c1e5a9f10",
    wave: "7e1a0003-5a4e-4c2f-9d3b-2b7c1e5a9f10",
    guided: "7e1a0004-5a4e-4c2f-9d3b-2b7c1e5a9f10",
    result: "7e1a0005-5a4e-4c2f-9d3b-2b7c1e5a9f10",
    control: "7e1a0006-5a4e-4c2f-9d3b-2b7c1e5a9f10",
  };

  const CMD = { startGuided: 1, cancelGuided: 2 };
  const STATES = ["Place finger", "Measuring...", "Measuring...", "Hold still", "Too bright", "Weak pulse", "Good signal"];
  const STRESS = ["", "High stress", "Some stress", "Calm", "Relaxed"];
  const PHASES = ["off", "before", "slow breathing", "after", "done"];

  // ---------- decoding (DataView in, plain object out) ----------

  function u16(dv, o) { return dv.getUint16(o, true); }

  function decodeHeartRate(dv) {
    const flags = dv.getUint8(0);
    const wide = flags & 0x01;
    let o = 1;
    const bpm = wide ? u16(dv, o) : dv.getUint8(o);
    o += wide ? 2 : 1;
    if (flags & 0x08) o += 2;                 // energy expended (PulseLab doesn't send it)
    const rr = [];
    if (flags & 0x10) for (; o + 1 < dv.byteLength; o += 2) rr.push(u16(dv, o) * 1000 / 1024);
    return { bpm, contact: (flags & 0x06) === 0x06, rrMs: rr };
  }

  function decodeVitals(dv) {
    const f = dv.getUint8(3);
    return {
      version: dv.getUint8(0),
      state: dv.getUint8(1),
      stateText: STATES[dv.getUint8(1)] || "?",
      quality: dv.getUint8(2),
      showBpm: !!(f & 0x01), showSpo2: !!(f & 0x02), hrvReady: !!(f & 0x04),
      hrSteady: !!(f & 0x08), breathValid: !!(f & 0x10), finger: !!(f & 0x20),
      bpm: u16(dv, 4) / 10, spo2: u16(dv, 6) / 10, pi: u16(dv, 8) / 100,
      rmssd: u16(dv, 10) / 10, breath: u16(dv, 12) / 10,
      hrvProgress: dv.getUint8(14), breathProgress: dv.getUint8(15),
      stress: dv.getUint8(16), stressText: STRESS[dv.getUint8(16)] || "",
      guidedPhase: dv.getUint8(17), guidedLeft: u16(dv, 18),
    };
  }

  function decodeWave(dv) {
    const n = dv.getUint8(1), points = [];
    for (let i = 0; i < n; i++) points.push(dv.getInt16(2 + 2 * i, true));
    return { seq: dv.getUint8(0), points };
  }

  function decodeGuided(dv) {
    const f = dv.getUint8(1);
    return {
      phase: dv.getUint8(0), phaseText: PHASES[dv.getUint8(0)] || "?",
      paused: !!(f & 0x01), inhale: !!(f & 0x02),
      level: dv.getUint8(2) / 255, left: u16(dv, 3), lastSwing: u16(dv, 5) / 10, pace: dv.getUint8(7),
    };
  }

  function decodeResult(dv) {
    if (dv.byteLength < 20) {   // version 1 firmware (Phase 8 before SDNN): 17 bytes, HR x10
      return {
        hr: [u16(dv, 0) / 10, u16(dv, 2) / 10, u16(dv, 4) / 10],
        rmssd: [u16(dv, 6) / 10, u16(dv, 8) / 10, u16(dv, 10) / 10], sdnn: [0, 0, 0],
        swing: u16(dv, 12) / 10, swingBreaths: dv.getUint8(14), follow: u16(dv, 15) / 10, steady: [true, true, true],
      };
    }
    const b = dv.getUint8(17);
    return {
      hr: [dv.getUint8(0), dv.getUint8(1), dv.getUint8(2)],
      rmssd: [u16(dv, 3) / 10, u16(dv, 5) / 10, u16(dv, 7) / 10],
      sdnn: [u16(dv, 9) / 10, u16(dv, 11) / 10, u16(dv, 13) / 10],
      swing: u16(dv, 15) / 10, swingBreaths: b & 0x1f, follow: u16(dv, 18) / 10,
      steady: [!!(b & 0x20), !!(b & 0x40), !!(b & 0x80)],
    };
  }

  // ---------- the device ----------

  class PulseLabDevice {
    constructor(handlers) {
      this.h = handlers || {};
      this.device = null;
      this.control = null;
      this.lastSeq = null;
      this.lostWave = 0;
    }

    static supported() { return typeof navigator !== "undefined" && !!navigator.bluetooth; }

    async connect() {
      this.device = await navigator.bluetooth.requestDevice({
        // by name, or by the heart rate service in the advertisement (some
        // computers only see the name after an extra scan request)
        filters: [{ name: "PulseLab" }, { namePrefix: "Pulse" }, { services: [UUID.hrService] }],
        optionalServices: [UUID.service, UUID.hrService],
      });
      this.device.addEventListener("gattserverdisconnected", () => this.h.onDisconnect && this.h.onDisconnect());
      // the first connect sometimes fails on Windows; try a second time
      let server;
      try { server = await this.device.gatt.connect(); }
      catch (e) { await new Promise((r) => setTimeout(r, 800)); server = await this.device.gatt.connect(); }
      const hrs = await server.getPrimaryService(UUID.hrService);
      const pl = await server.getPrimaryService(UUID.service);
      await this.listen(hrs, UUID.hrMeasurement, (dv) => this.h.onHeartRate && this.h.onHeartRate(decodeHeartRate(dv)));
      await this.listen(pl, UUID.vitals, (dv) => this.h.onVitals && this.h.onVitals(decodeVitals(dv)));
      await this.listen(pl, UUID.wave, (dv) => {
        const w = decodeWave(dv);
        if (this.lastSeq !== null) this.lostWave += (w.seq - this.lastSeq - 1 + 256) % 256;
        this.lastSeq = w.seq;
        this.h.onWave && this.h.onWave(w);
      });
      await this.listen(pl, UUID.guided, (dv) => this.h.onGuided && this.h.onGuided(decodeGuided(dv)));
      const result = await this.listen(pl, UUID.result, (dv) => this.h.onResult && this.h.onResult(decodeResult(dv)));
      this.control = await pl.getCharacteristic(UUID.control);
      // a result from a session that finished before connecting
      const last = await result.readValue();
      if (last.byteLength >= 17 && last.getUint8(0) > 0) this.h.onResult && this.h.onResult(decodeResult(last));
      return this.device.name;
    }

    async listen(service, uuid, fn) {
      const c = await service.getCharacteristic(uuid);
      c.addEventListener("characteristicvaluechanged", (e) => fn(e.target.value));
      await c.startNotifications();
      return c;
    }

    async send(cmd) {
      if (this.control) await this.control.writeValue(new Uint8Array([cmd]));
    }

    disconnect() {
      if (this.device && this.device.gatt.connected) this.device.gatt.disconnect();
    }
  }

  // ---------- demo: made-up data through the same decoders ----------

  class DemoDevice {
    constructor(handlers) { this.h = handlers || {}; this.timers = []; this.t = 0; this.seq = 0; this.guided = null; this.beatAt = 0; }

    connect() {
      const tick = 40;   // ms, 25 wave points per second like the real thing
      let wave = [];
      this.timers.push(setInterval(() => {
        this.t += tick / 1000;
        const t = this.t, slow = this.guided && this.guided.phase === 2;
        const breathHz = slow ? 0.1 : 0.25;
        const hr = 72 + (slow ? 8 : 3) * Math.sin(2 * Math.PI * breathHz * t);
        this.phase = ((this.phase || 0) + (tick / 1000) * hr / 60) % 1;   // add up, since the rate changes
        const ph = this.phase;
        const g = (x, m, s) => Math.exp(-0.5 * ((x - m) / s) ** 2);
        // a smooth pulse with a small second bump, roughly like the filtered signal on the device
        const pulse = g(ph, 0.2, 0.11) + g(ph - 1, 0.2, 0.11) + 0.35 * g(ph, 0.55, 0.1);
        wave.push(Math.round(520 * pulse - 200 + 25 * Math.sin(2 * Math.PI * breathHz * t)));
        if (wave.length === 8) {
          const b = new DataView(new ArrayBuffer(18));
          b.setUint8(0, this.seq++ & 255); b.setUint8(1, 8);
          wave.forEach((v, i) => b.setInt16(2 + 2 * i, v, true));
          this.h.onWave && this.h.onWave(decodeWave(b));
          wave = [];
        }
        if (t >= this.beatAt) {
          const rr = 60000 / hr;
          this.beatAt = t + rr / 1000;
          const b = new DataView(new ArrayBuffer(4));
          b.setUint8(0, 0x16); b.setUint8(1, Math.round(hr)); b.setUint16(2, Math.round(rr * 1.024), true);
          this.h.onHeartRate && this.h.onHeartRate(decodeHeartRate(b));
        }
        if (this.guided) this.stepGuided(tick / 1000);
      }, tick));
      this.timers.push(setInterval(() => this.sendVitals(), 500));
      return Promise.resolve("PulseLab (demo)");
    }

    sendVitals() {
      const t = this.t, slow = this.guided && this.guided.phase === 2;
      const b = new DataView(new ArrayBuffer(20));
      const ready = t > 30, breathReady = t > 60;
      b.setUint8(0, 1); b.setUint8(1, 6); b.setUint8(2, 92 + Math.round(6 * Math.sin(t / 7)));
      b.setUint8(3, 0x01 | 0x02 | (ready ? 0x04 | 0x08 : 0) | (breathReady ? 0x10 : 0) | 0x20);
      b.setUint16(4, Math.round(10 * (72 + 2 * Math.sin(t / 20))), true);
      b.setUint16(6, Math.round(10 * (97.5 + 0.4 * Math.sin(t / 15))), true);
      b.setUint16(8, Math.round(100 * (1.8 + 0.2 * Math.sin(t / 9))), true);
      const rmssd = slow ? 58 + 4 * Math.sin(t / 11) : 38 + 3 * Math.sin(t / 13);
      b.setUint16(10, ready ? Math.round(10 * rmssd) : 0, true);
      b.setUint16(12, breathReady ? Math.round(10 * (slow ? 6.1 : 14.3 + 0.6 * Math.sin(t / 17))) : 0, true);
      b.setUint8(14, Math.min(100, Math.round(t / 30 * 100))); b.setUint8(15, Math.min(100, Math.round(t / 60 * 100)));
      b.setUint8(16, ready ? (rmssd >= 60 ? 4 : 3) : 0);
      b.setUint8(17, this.guided ? this.guided.phase : 0); b.setUint16(18, this.guided ? Math.ceil(this.guided.left) : 0, true);
      this.h.onVitals && this.h.onVitals(decodeVitals(b));
    }

    stepGuided(dt) {
      const g = this.guided;
      if (g.phase === 4) return;
      g.left -= dt; g.inPhase += dt;
      if (g.left <= 0) {
        g.phase++; g.inPhase = 0;
        g.left = g.phase === 2 ? 180 * this.speed : 60 * this.speed;
        if (g.phase === 4) {
          const r = new DataView(new ArrayBuffer(20));
          [72, 71, 70].forEach((v, i) => r.setUint8(i, v));
          [362, 418, 389, 480, 760, 530].forEach((v, i) => r.setUint16(3 + 2 * i, v, true));
          r.setUint16(15, 216, true); r.setUint8(17, 16 | 0xe0); r.setUint16(18, 61, true);
          this.h.onResult && this.h.onResult(decodeResult(r));
        }
      }
      g.sinceSend = (g.sinceSend || 0) + dt;
      if (g.sinceSend >= 0.25 && g.phase < 4) {
        g.sinceSend = 0;
        const b = new DataView(new ArrayBuffer(8));
        let level = 0, inhale = false;
        if (g.phase === 2) {
          const p = (g.inPhase / this.speed) % 10;
          inhale = p < 4;
          level = inhale ? 0.5 - 0.5 * Math.cos(Math.PI * p / 4) : 0.5 + 0.5 * Math.cos(Math.PI * (p - 4) / 6);
        }
        b.setUint8(0, g.phase); b.setUint8(1, inhale ? 2 : 0); b.setUint8(2, Math.round(level * 255));
        b.setUint16(3, Math.ceil(g.left), true); b.setUint16(5, g.phase === 2 && g.inPhase > 20 ? 170 + Math.round(30 * Math.random()) : 0, true);
        b.setUint8(7, g.phase === 2 ? 6 : 0);
        this.h.onGuided && this.h.onGuided(decodeGuided(b));
      }
    }

    async send(cmd) {
      if (cmd === CMD.startGuided) this.guided = { phase: 1, left: 60 * this.speed, inPhase: 0 };
      if (cmd === CMD.cancelGuided) { this.guided = null; }
    }

    disconnect() { this.timers.forEach(clearInterval); this.timers = []; this.h.onDisconnect && this.h.onDisconnect(); }
  }
  DemoDevice.prototype.speed = 1;   // 0.1 = 10x faster guided session (for testing the page)

  const api = { UUID, CMD, STATES, STRESS, PHASES, decodeHeartRate, decodeVitals, decodeWave, decodeGuided, decodeResult, PulseLabDevice, DemoDevice };
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  else root.PulseLab = api;
})(typeof window !== "undefined" ? window : globalThis);
