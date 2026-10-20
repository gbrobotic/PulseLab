"""
Plots a raw recording (SERIAL_MODE 3) to see what breathing does to the signal.

    pip install numpy scipy matplotlib
    python validation/plot_raw.py validation/logs/raw_pace6.txt
    python validation/plot_raw.py validation/logs/raw_pace6.txt 60 90   (only 60-90 s)

Saves a PNG next to the recording. Four rows, all on the same time axis:
  1. raw IR: the slow drift is the "baseline" effect of breathing
  2. pulse (band-pass 0.5-4 Hz, like the device), with each beat's height:
     the "amplitude" effect
  3. time between beats: the "timing" effect (the same thing HRV measures)
  4. what the pacer said: breathe in (shaded) or out
The 3 effects are the ones Karlen et al. 2013 use (docs/REFERENCES.md, ref 9).
"""

import sys

import numpy as np
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from scipy.signal import butter, sosfiltfilt, find_peaks

FS = 100
PACE_IN_PART = 0.4   # same as pacer.h


def load(path):
    idx, ir, red, paces = [], [], [], []
    with open(path, encoding="utf-8", errors="ignore") as f:
        for line in f:
            if line.startswith("# pace"):
                p = line.split()
                paces.append((int(p[2]), int(p[4])))
                continue
            parts = line.strip().split(",")
            if len(parts) == 3 and all(x.isdigit() for x in parts):
                idx.append(int(parts[0]))
                ir.append(int(parts[1]))
                red.append(int(parts[2]))
    idx = np.array(idx)
    # put the samples on a regular grid, fill lost ones by a straight line
    full = np.arange(idx[0], idx[-1] + 1)
    ir = np.interp(full, idx, ir)
    red = np.interp(full, idx, red)
    return full, ir, red, paces


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    path = sys.argv[1]
    idx, ir, red, paces = load(path)
    t = (idx - idx[0]) / FS

    # Filter each stretch with the finger on separately, and skip its first
    # 2 s (placing the finger makes a huge jump that would hide everything).
    finger = ir > 10000
    valid = np.zeros_like(finger)
    pulse = np.zeros_like(ir)
    sos = butter(2, [0.5, 4.0], btype="band", fs=FS, output="sos")
    edges = np.flatnonzero(np.diff(np.r_[0, finger.astype(int), 0]))
    for a, b in zip(edges[::2], edges[1::2]):
        a2 = a + 2 * FS
        if b - a2 < 3 * FS:
            continue
        seg = ir[a2:b]
        pulse[a2:b] = -sosfiltfilt(sos, seg - seg.mean())   # inverted: beat = peak
        valid[a2:b] = True

    prom = np.percentile(np.abs(pulse[valid]), 75) * 0.5 if valid.any() else 1
    peaks, _ = find_peaks(pulse, distance=int(0.33 * FS), prominence=prom)
    peaks = peaks[valid[peaks]]
    heights = pulse[peaks]
    ibi_t = t[peaks[1:]]
    ibi = np.diff(t[peaks]) * 1000

    t0 = float(sys.argv[2]) if len(sys.argv) > 2 else t[0]
    t1 = float(sys.argv[3]) if len(sys.argv) > 3 else t[-1]
    view = (t >= t0) & (t <= t1)
    vv = view & valid

    def robust_ylim(a, values):
        if len(values) < 2:
            return
        lo, hi = np.percentile(values, [1, 99])
        pad = 0.15 * (hi - lo + 1e-9)
        a.set_ylim(lo - pad, hi + pad)

    fig, ax = plt.subplots(4, 1, figsize=(12, 9), sharex=True,
                           gridspec_kw={"height_ratios": [2, 2, 2, 0.6]})
    ax[0].plot(t[view], ir[view], lw=0.8)
    ax[0].set_ylabel("raw IR")
    ax[0].set_title(path.replace("\\", "/").rsplit("/", 1)[-1])
    robust_ylim(ax[0], ir[vv])

    ax[1].plot(t[view], np.where(valid[view], pulse[view], np.nan), lw=0.6, color="0.5")
    pv = (t[peaks] >= t0) & (t[peaks] <= t1)
    ax[1].plot(t[peaks][pv], heights[pv], "o-", ms=3, lw=1, color="C1", label="beat height")
    ax[1].set_ylabel("pulse (filtered)")
    robust_ylim(ax[1], pulse[vv])
    ax[1].legend(loc="upper right")

    iv = (ibi_t >= t0) & (ibi_t <= t1) & (ibi > 300) & (ibi < 2000)
    ax[2].plot(ibi_t[iv], ibi[iv], "o-", ms=3, lw=1, color="C2")
    ax[2].set_ylabel("beat interval (ms)")
    robust_ylim(ax[2], ibi[iv])

    # pacer: shade "in" parts
    for k, (pace, start) in enumerate(paces):
        end = paces[k + 1][1] if k + 1 < len(paces) else idx[-1]
        if pace == 0:
            continue
        period = 60.0 / pace
        s = (start - idx[0]) / FS
        e = (end - idx[0]) / FS
        b = s
        while b < e:
            for a in ax:
                a.axvspan(b, min(b + PACE_IN_PART * period, e), color="C0", alpha=0.08, lw=0)
            b += period
        ax[3].text(max(s, t0), 0.5, f" pacer {pace}/min", va="center")
    ax[3].set_yticks([])
    ax[3].set_ylabel("pacer")
    ax[3].set_xlabel("time (s)   shaded = breathe in")
    ax[3].set_xlim(t0, t1)

    out = path.rsplit(".", 1)[0] + ".png"
    fig.subplots_adjust(left=0.08, right=0.98, top=0.95, bottom=0.07, hspace=0.12)
    fig.savefig(out, dpi=110)
    print(f"saved {out}")


if __name__ == "__main__":
    main()
