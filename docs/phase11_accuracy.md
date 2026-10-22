# Phase 11: accuracy test vs a commercial oximeter

Code: `validation/analyze.py`, data sheet `validation/readings_template.csv`.

Status: **pending, to be done before submission.** The procedure and the analysis are ready and checked on simulated data. No real readings yet, so there are no Phase 11 results in this repo.

## What I want to find out

1. How close PulseLab's heart rate is to a commercial fingertip pulse oximeter, at rest and after light exercise.
2. How far off the textbook SpO2 formula (`110 - 25R`) is on my sensor, and whether a calibration fitted on some readings makes other readings better.

## Equipment

- PulseLab (`firmware/08_ble`, Serial Monitor mode 2 so the beat lines show `R`)
- a commercial fingertip pulse oximeter (write down the model)
- laptop with Serial Monitor open, or `validation/capture_serial.py` running
- 4-5 people, each agrees to take part

## How to take one reading

1. Person sits, hand resting on the table, PulseLab on one index finger, the reference oximeter on the other hand.
2. Wait until PulseLab shows "Good signal" and both devices have been steady for about 20 s.
3. Write down at the same moment: PulseLab BPM and SpO2 (Details screen, it isn't capped at 100), reference BPM and SpO2, and the `R` from the latest Serial beat line.
4. Take the finger out, put it back, and repeat. Taking the finger out between readings makes each reading more independent.

Plan: 4 readings at rest + 3 readings right after 1 minute of stairs or squats, per person. 5 people gives 35 readings.

## Calibration and test set

The formula must be tested on readings that weren't used to fit it, otherwise the result looks better than it really is. So, in the `set` column:

- `cal`: readings from the first 3 people, used to fit A and B
- `test`: readings from the other people, only used to check

Different people in each set is the fair way, because the error can depend on the person's finger.

## Running the analysis

```
pip install pandas matplotlib numpy tabulate
python validation/analyze.py validation/readings.csv
```

It writes `results.md` and the Bland-Altman plots next to the CSV:

- heart rate and SpO2 against the reference: MAE, bias, 95% limits of agreement ([Bland & Altman 1986, ref 12](REFERENCES.md)), ARMS (the root mean square error that oximeter standards use) and Pearson r
- heart rate split by condition (rest / after exercise)
- SpO2 calibration fitted on `cal`, three formulas compared on `test`: textbook, offset only (only A fitted), full fit (A and B fitted)
- the constants to paste into `spo2.h`

### Offset only or full fit?

Healthy people at rest read about 95-99%, so the reference SpO2 range will be narrow. A slope (B) fitted to a narrow range isn't reliable, so the script only recommends the full fit when the reference values span at least 5 points and B comes out between 10 and 40. Otherwise it recommends offset only, which corrects the bias but keeps the textbook slope.

Proper oximeter calibration (ISO 80601-2-61) uses controlled desaturation down to 70% in a lab. I can't do that safely, so the calibration here is only valid for the 94-100% range, and the report has to say that.

## Checked on simulated data

`validation/sim/make_sim_readings.py` makes 35 **made-up** readings, where the "true" sensor follows `104 - 17R` and the heart rate is 1.5 BPM low. This is only to check that the script finds what was put in. **It's not a result and isn't used in the report.**

| What was put in | What the script found |
|---|---|
| HR bias -1.5 BPM | -1.14 BPM |
| SpO2 = 104 - 17R | full fit 103.4 - 15.8R, test MAE 0.35% |

Output: `validation/sim/results.md`. The plots there have a "SIMULATED DATA" watermark.

## Results (fill in after the test)

| | n | MAE | Bias | 95% LoA |
|---|---|---|---|---|
| Heart rate, rest | | | | |
| Heart rate, after exercise | | | | |
| SpO2, textbook formula | | | | |
| SpO2, calibrated (test readings only) | | | | |

New `SPO2_A` / `SPO2_B`: ___ / ___

Reference oximeter model: ___
