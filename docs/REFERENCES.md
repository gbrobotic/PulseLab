# References

Papers and documents I used while building PulseLab, and where each one is used. Most of them are open access.

## Sensor and PPG signal

1. **MAX30100 datasheet**, Maxim Integrated (now Analog Devices).
   https://www.analog.com/en/products/max30100.html
   Used for: registers, FIFO, LED current, sample rate (Phase 1-4).

2. J. Allen, "Photoplethysmography and its application in clinical physiological measurement", *Physiological Measurement*, 28(3), R1-R39, 2007.
   https://pubmed.ncbi.nlm.nih.gov/17322588/
   Used for: what the PPG signal is (AC/DC parts, why the pulse shows up as a dip in IR), and the 3 ways breathing changes it (Phase 2, Phase 6).

3. M. Elgendi, "On the analysis of fingertip photoplethysmogram signals", *Current Cardiology Reviews*, 8(1), 14-25, 2012.
   https://www.ncbi.nlm.nih.gov/pmc/articles/PMC3394104/
   Used for: pulse shape, the second bump (dicrotic notch / diastolic peak), why the slope is a good place to time a beat (Phase 2, DEBUG_LOG #12).

## SpO2

4. M. Nitzan, A. Romem, R. Koppel, "Pulse oximetry: fundamentals and technology update", *Medical Devices: Evidence and Research*, 7, 231-239, 2014.
   https://doaj.org/article/0eb0cb26e0a94c36b003924bec1dc840
   Used for: the ratio of ratios R = (AC_red/DC_red) / (AC_ir/DC_ir), why SpO2 needs calibration, perfusion index (Phase 3, Phase 11).

## Heart rate variability

5. Task Force of the European Society of Cardiology and the North American Society of Pacing and Electrophysiology, "Heart rate variability: standards of measurement, physiological interpretation and clinical use", *Circulation*, 93(5), 1043-1065, 1996.
   https://pubmed.ncbi.nlm.nih.gov/8598068/
   Used for: definitions of RMSSD, SDNN, pNN50, the standard 5-minute recording, and that artifacts must be removed before calculating (Phase 5).

6. F. Shaffer, J. P. Ginsberg, "An overview of heart rate variability metrics and norms", *Frontiers in Public Health*, 5, 258, 2017.
   https://www.frontiersin.org/journals/public-health/articles/10.3389/fpubh.2017.00258
   Used for: what each HRV number means, typical values, and why RMSSD is used for short recordings (Phase 5).

7. R. Munoz et al., "Validity of (ultra-)short recordings for heart rate variability measurements", *PLoS ONE*, 10(9), e0138921, 2015.
   https://www.ncbi.nlm.nih.gov/pmc/articles/PMC4586373/
   Used for: checking that RMSSD from shorter recordings (like my 60 s window) is still close to the 5-minute value (Phase 5).

8. P. M. Lehrer, R. Gevirtz, "Heart rate variability biofeedback: how and why does it work?", *Frontiers in Psychology*, 5, 756, 2014.
   https://www.ncbi.nlm.nih.gov/pmc/articles/PMC4104929/
   Used for: why slow breathing at about 6 breaths per minute makes HRV go up so much (resonance breathing). That's what I saw in my test (Phase 5) and what Phase 7 is based on.

## Breathing rate from PPG

9. W. Karlen, S. Raman, J. M. Ansermino, G. A. Dumont, "Multiparameter respiratory rate estimation from the photoplethysmogram", *IEEE Transactions on Biomedical Engineering*, 60(7), 1946-1953, 2013.
   https://doi.org/10.1109/TBME.2013.2246160
   Used for: getting breathing rate from 3 effects (beat timing, pulse height, baseline) and only trusting it when they agree ("smart fusion") (Phase 6).

10. P. H. Charlton et al., "An assessment of algorithms to estimate respiratory rate from the electrocardiogram and photoplethysmogram", *Physiological Measurement*, 37(4), 610-626, 2016.
    https://www.ncbi.nlm.nih.gov/pmc/articles/PMC5390977/
    Used for: the methods I compared (spectrum peak vs counting breaths), and reporting the error as mean absolute error and % within 2 breaths/min (Phase 6).

11. P. H. Charlton et al., "Breathing rate estimation from the electrocardiogram and photoplethysmogram: a review", *IEEE Reviews in Biomedical Engineering*, 11, 2-20, 2018.
    https://www.ncbi.nlm.nih.gov/pmc/articles/PMC7612521/
    Used for: overview of the whole processing chain (Phase 6).

## Checking accuracy

12. J. M. Bland, D. G. Altman, "Statistical methods for assessing agreement between two methods of clinical measurement", *The Lancet*, 327(8476), 307-310, 1986.
    https://www-users.york.ac.uk/~mb55/meas/ba.htm
    Used for: the Bland-Altman method in `validation/analyze.py`: mean difference and limits of agreement instead of just correlation (Phase 11).
