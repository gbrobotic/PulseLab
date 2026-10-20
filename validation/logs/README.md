Raw Serial recordings (from capture_serial.py).

| File | What | Code |
|---|---|---|
| phase3.txt | 2 min sitting still, no OLED | Phase 3 (intervals rounded to 10 ms) |
| phase4.txt | 2 min sitting still, with OLED | Phase 4 (intervals rounded to 10 ms) |
| hrv_normal.txt | sitting, with some fast breathing | first Phase 5 version |
| hrv_slow.txt | slow breathing | first Phase 5 version (rejected real beats, see DEBUG_LOG #11) |
| hrv_sit2.txt | 2.5 min sitting still | Phase 5 final |
| hrv_slow2.txt | 2.5 min slow breathing, 5 s in / 5 s out | Phase 5 final |
| raw_free.txt | 2.5 min breathing normally, pacer off, not counted | Phase 6 raw (SERIAL_MODE 3) |
| raw_pace6/10/15/20.txt | 2.5 min breathing with the pacer at 6/10/15/20 per min (15 and 20 right after coming in from outside, HR ~110) | Phase 6 raw |
| raw_free2.txt | 2.5 min breathing normally after eating, counted ~80 breaths (~32 /min) | Phase 6 raw, with breathing code |
