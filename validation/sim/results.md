# Phase 11 results: `simulated_readings.csv`

> **SIMULATED DATA - not real measurements.** Only for checking the script.

## Agreement with the reference (as measured)

| What                              |   n | MAE      | Bias      | 95% LoA        | ARMS     |   Pearson r | Plot                        |
|:----------------------------------|----:|:---------|:----------|:---------------|:---------|------------:|:----------------------------|
| Heart rate (all readings)         |  35 | 1.55 BPM | -1.14 BPM | -4.23 to +1.95 | 1.93 BPM |       0.994 | bland_altman_hr.png         |
| SpO2, uncalibrated (all readings) |  35 | 1.96 %   | +1.96 %   | +0.49 to +3.42 | 2.09 %   |       0.86  | bland_altman_spo2_uncal.png |

### Heart rate by condition

| What                       |   n | MAE      | Bias      | 95% LoA        | ARMS     |   Pearson r |
|:---------------------------|----:|:---------|:----------|:---------------|:---------|------------:|
| Heart rate: after_exercise |  15 | 1.53 BPM | -1.25 BPM | -3.99 to +1.50 | 1.84 BPM |       0.923 |
| Heart rate: rest           |  20 | 1.57 BPM | -1.06 BPM | -4.44 to +2.32 | 1.99 BPM |       0.923 |

## SpO2 calibration

Fitted on 21 'cal' readings (reference SpO2 95-100%, R 0.224-0.514), tested on 14 'test' readings that were not used for fitting.

| What                      |   n | MAE    | Bias    | 95% LoA        | ARMS   |   Pearson r | Within 2%   |
|:--------------------------|----:|:-------|:--------|:---------------|:-------|------------:|:------------|
| Textbook (110 - 25R)      |  14 | 2.06 % | +2.06 % | +1.04 to +3.07 | 2.12 % |       0.882 | 36%         |
| Offset only (106.9 - 25R) |  14 | 1.02 % | -0.84 % | -2.42 to +0.74 | 1.14 % |       0.908 | 93%         |
| Full fit (103.4 - 15.8R)  |  14 | 0.35 % | +0.02 % | -0.77 to +0.80 | 0.39 % |       0.908 | 100%        |

Recommended: **full fit** 

Put this in `firmware/08_ble/spo2.h`:

```cpp
constexpr float   SPO2_A           = 103.4f;
constexpr float   SPO2_B           = 15.8f;
```

Plot: `bland_altman_spo2_cal.png`
