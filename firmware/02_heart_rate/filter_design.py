"""
Designs the band-pass filter used in ppg_dsp.h and plots its response.

    pip install scipy numpy matplotlib
    python filter_design.py

(If you get an error on the "import matplotlib" line, the first command wasn't run.)

Prints the coefficients to paste into the BandPass class, and saves
filter_response.png so you can see what the filter does.
"""

import matplotlib.pyplot as plt
import numpy as np
from scipy.signal import butter, sosfreqz

FS = 100.0          # sample rate (Hz)
LOW, HIGH = 0.5, 4.0  # pass band: 30 to 240 BPM
ORDER = 2           # 2 for band-pass gives a 4th-order filter (two biquads)

sos = butter(ORDER, [LOW, HIGH], btype="bandpass", fs=FS, output="sos")

print("Paste into BandPass in ppg_dsp.h (b0, b1, b2, a1, a2 for each section):\n")
for i, (b0, b1, b2, a0, a1, a2) in enumerate(sos, start=1):
    print(f"  Biquad s{i}_{{{b0:.10f}f, {b1:.10f}f, {b2:.10f}f, {a1:.10f}f, {a2:.10f}f}};")

freqs, h = sosfreqz(sos, worN=4096, fs=FS)
gain_db = 20 * np.log10(np.maximum(np.abs(h), 1e-12))

fig, ax = plt.subplots(figsize=(8, 4))
ax.semilogx(freqs[1:], gain_db[1:])
ax.axvspan(LOW, HIGH, alpha=0.1, label="pass band (30-240 BPM)")
ax.axvline(0.25, linestyle="--", color="gray", label="breathing (~15/min)")
ax.set_xlim(0.05, 50)
ax.set_ylim(-60, 5)
ax.set_xlabel("Frequency (Hz)")
ax.set_ylabel("Gain (dB)")
ax.set_title("Band-pass filter, 0.5-4 Hz, 4th-order Butterworth, fs = 100 Hz")
ax.grid(True, which="both", alpha=0.3)
ax.legend()
fig.tight_layout()
fig.savefig("filter_response.png", dpi=150)
print("\nSaved filter_response.png")
