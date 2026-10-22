"""
Makes SIMULATED Phase 11 readings, only to check that analyze.py works
before the real test. These are NOT measurements and must not be reported
as results.

    python validation/sim/make_sim_readings.py
    python validation/analyze.py validation/sim/simulated_readings.csv

The made-up "truth" inside: the sensor really follows SpO2 = 104 - 17R
(so the textbook 110 - 25R reads a bit high), and PulseLab heart rate is
1.5 BPM low with 2 BPM of noise. The script should find roughly those.
"""

from pathlib import Path

import numpy as np

rng = np.random.default_rng(11)
rows = []
i = 0
for subject in ["S1", "S2", "S3", "S4", "S5"]:
    base_hr = rng.uniform(64, 82)
    base_spo2 = rng.choice([96, 97, 98, 99])
    for condition, n, hr_add in [("rest", 4, 0), ("after_exercise", 3, 25)]:
        for _ in range(n):
            i += 1
            ref_hr = round(base_hr + hr_add + rng.normal(0, 4))
            ref_spo2 = int(np.clip(base_spo2 + rng.integers(-1, 2) - (1 if condition == "after_exercise" else 0), 93, 100))
            r = (104 - ref_spo2) / 17 + rng.normal(0, 0.03)
            rows.append((i, subject, condition, ref_hr - 1.5 + rng.normal(0, 2), ref_hr,
                         110 - 25 * r, ref_spo2, r))

# first 21 readings (S1-S3) calibrate, S4-S5 test: different people in each set
lines = ["# SIMULATED DATA - NOT REAL MEASUREMENTS. Made by make_sim_readings.py to test analyze.py.",
         "reading_id,subject,condition,set,pulselab_hr,reference_hr,pulselab_spo2,reference_spo2,pulselab_r,notes"]
for (i, s, c, phr, rhr, psp, rsp, r) in rows:
    st = "cal" if s in ("S1", "S2", "S3") else "test"
    lines.append(f"{i},{s},{c},{st},{phr:.1f},{rhr},{psp:.1f},{rsp},{r:.3f},simulated")

out = Path(__file__).with_name("simulated_readings.csv")
out.write_text("\n".join(lines) + "\n", encoding="utf-8")
print(f"Wrote {len(rows)} simulated readings to {out}")
