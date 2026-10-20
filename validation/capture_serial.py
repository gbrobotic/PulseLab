"""
Records PulseLab's Serial output into a text file for a set time.

    pip install pyserial
    python validation/capture_serial.py COM9 phase3.txt 120

  COM9        the board's port (same as in Arduino IDE > Tools > Port)
  phase3.txt  file to save into
  120         how many seconds to record

Works with SERIAL_MODE 2 (one line per beat) and SERIAL_MODE 3 (raw
recording, "index,ir,red" 100 times a second). In raw mode it shows
progress every 10 s and checks at the end that no samples were lost.

Close the Arduino IDE Serial Monitor / Plotter first, only one program
can use the port at a time.
"""

import sys
import time

import serial


def main() -> None:
    if len(sys.argv) != 4:
        print(__doc__)
        sys.exit(1)

    port, out_file, seconds = sys.argv[1], sys.argv[2], float(sys.argv[3])

    ser = serial.Serial()
    ser.port = port
    ser.baudrate = 115200
    ser.timeout = 0.5
    ser.dtr = False   # don't reset the board when the port opens
    ser.rts = False
    try:
        ser.open()
    except serial.SerialException as e:
        print(f"Could not open {port}: {e}")
        print("Is the Arduino Serial Monitor still open? Close it and try again.")
        sys.exit(1)

    print(f"Recording {port} for {seconds:.0f} s into {out_file} ... keep your hand still.")
    start = time.time()
    end = start + seconds
    lines = 0
    partial = ""
    # raw mode bookkeeping
    samples = 0
    last_index = None
    lost = 0
    bad_lines = 0
    next_progress = start + 10
    warned = False

    with open(out_file, "w", encoding="utf-8") as f:
        while time.time() < end:
            raw = ser.readline()
            if not raw:
                if lines == 0 and not warned and time.time() - start > 5:
                    warned = True
                    print("Nothing received in 5 s. Check (it keeps recording meanwhile, Ctrl+C to stop):")
                    print("  - Arduino IDE: Tools > USB CDC On Boot = Enabled, then upload again (DEBUG_LOG #7)")
                    print("  - the upload finished, and the board shows the PulseLab screen")
                    print("  - the right port: Arduino IDE > Tools > Port")
                continue
            partial += raw.decode("utf-8", errors="ignore")
            if not partial.endswith("\n"):
                continue   # half a line arrived (the rest comes in the next read), don't split it
            line = partial.rstrip()
            partial = ""
            f.write(line + "\n")
            lines += 1

            if line[:1].isdigit():
                # raw sample: index,ir,red
                parts = line.split(",")
                if len(parts) != 3 or not all(p.isdigit() for p in parts):
                    bad_lines += 1
                    continue
                index = int(parts[0])
                if last_index is not None and index != last_index + 1:
                    lost += max(index - last_index - 1, 0)
                last_index = index
                samples += 1
                if time.time() >= next_progress:
                    next_progress += 10
                    print(f"  {time.time() - start:4.0f} s: {samples} samples, IR {parts[1]}, lost {lost}")
            else:
                if "Beat" in line or line.startswith("#"):
                    print(line)
                elif lines % 20 == 0:
                    print(f"  ... {int(end - time.time())} s left")
    ser.close()

    print(f"Done: {lines} lines saved to {out_file}")
    if lines == 0:
        print("No data at all. Most likely Tools > USB CDC On Boot is Disabled for this sketch,")
        print("so Serial goes to pins GPIO20/21 instead of USB. Set it to Enabled and upload again.")
    if samples:
        expected = seconds * 100
        print(f"Raw samples: {samples} ({samples / 100:.0f} s of data, about {expected:.0f} expected)")
        print(f"Lost samples: {lost}, broken lines: {bad_lines}")
        if lost or bad_lines:
            print("Some data was lost. Short gaps are OK, the PC tools skip them, but if it's a lot,")
            print("close other programs and use a USB port directly on the PC (not a hub).")


if __name__ == "__main__":
    main()
