// Checks dashboard/pulselab.js against packets made by the firmware code.
//   ./test_ble_packets | node tests/test_ble_decode.js
const PL = require("../dashboard/pulselab.js");
const lines = require("fs").readFileSync(0, "utf8").trim().split("\n").map((l) => JSON.parse(l));
let failures = 0;

function dv(hex) {
  const b = new Uint8Array(hex.match(/../g).map((x) => parseInt(x, 16)));
  return new DataView(b.buffer);
}
function close(a, b) {
  if (typeof b === "number") return Math.abs(a - b) <= (Number.isInteger(b) && b > 100 ? 1 : 0.011);
  if (Array.isArray(b)) return a.length === b.length && b.every((x, i) => close(a[i], x));
  return a === b;
}
const decoders = { hr: PL.decodeHeartRate, vitals: PL.decodeVitals, wave: PL.decodeWave, guided: PL.decodeGuided, result: PL.decodeResult };

for (const l of lines) {
  if (l.type === "sizes") {
    const ok = Object.entries(l).every(([k, v]) => k === "type" || v <= 20);
    console.log(`  [${ok ? "PASS" : "FAIL"}] every packet fits in 20 bytes  ${JSON.stringify(l)}`);
    if (!ok) failures++;
    continue;
  }
  const got = decoders[l.type](dv(l.hex));
  const bad = Object.entries(l.expect).filter(([k, v]) => !close(got[k], v)).map(([k, v]) => `${k}: got ${JSON.stringify(got[k])}, expected ${JSON.stringify(v)}`);
  console.log(`  [${bad.length ? "FAIL" : "PASS"}] ${l.type} (${l.len} bytes ${l.hex})${bad.length ? "  " + bad.join("; ") : ""}`);
  if (bad.length) failures++;
}
console.log(`\n${failures ? "SOME TESTS FAILED" : "ALL TESTS PASSED"} (${failures} failed)`);
process.exit(failures ? 1 : 0);
