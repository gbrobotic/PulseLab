"""
Checks the beat-to-beat intervals in a PulseLab Serial Monitor log.

Copy the Serial Monitor output into a text file, then:
    python ibi_check.py log.txt

It looks at the "Beat 812 ms ok" lines and reports:
  - median interval and heart rate
  - how many intervals were rejected
  - "short-long pairs": an interval more than 15% shorter than normal, followed
    by one more than 10% longer, where the two add up to about two normal
    beats. That pattern means one beat in the middle came early.

An early beat can be a measurement problem (finger moving, a wrong beat
detection) or a real early heartbeat. Early heartbeats are common and most
people have some without noticing. The script can't tell which, it just
finds them, so two recordings (e.g. with and without the OLED, or with the
hand completely still) can be compared.
"""

import re
import statistics
import sys

BEAT = re.compile(r"Beat\s+(\d+)\s+ms\s+(ok|rej)")


def main() -> None:
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(1)

    beats = []
    with open(sys.argv[1], encoding="utf-8", errors="ignore") as f:
        for line in f:
            m = BEAT.search(line)
            if m:
                beats.append((int(m.group(1)), m.group(2) == "ok"))

    if len(beats) < 5:
        print("Not enough 'Beat ... ms' lines found.")
        sys.exit(1)

    ok = [ms for ms, good in beats if good]
    med = statistics.median(ok)
    print(f"Intervals: {len(beats)}  (rejected {len(beats) - len(ok)})")
    print(f"Median interval {med:.0f} ms = {60000 / med:.1f} BPM")

    pairs = []
    for i in range(len(beats) - 1):
        a, a_ok = beats[i]
        b, b_ok = beats[i + 1]
        if not (a_ok and b_ok):
            continue
        if a < 0.85 * med and b > 1.10 * med and abs((a + b) - 2 * med) < 0.12 * 2 * med:
            pairs.append((i, a, b))

    print(f"Short-long pairs: {len(pairs)}")
    for i, a, b in pairs:
        print(f"  beat {i + 1:3d}: {a} ms then {b} ms  (sum {a + b}, two normal beats = {2 * med:.0f})")

    if ok:
        diffs = [abs(ok[i + 1] - ok[i]) for i in range(len(ok) - 1)]
        print(f"Average change from one interval to the next: {statistics.mean(diffs):.0f} ms")


if __name__ == "__main__":
    main()
