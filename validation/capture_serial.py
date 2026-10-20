"""
Records PulseLab's Serial output into a text file for a set time.

    pip install pyserial
    python validation/capture_serial.py COM9 phase3.txt 120

  COM9        the board's port (same as in Arduino IDE > Tools > Port)
  phase3.txt  file to save into
  120         how many seconds to record

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
    end = time.time() + seconds
    lines = 0
    partial = ""
    with open(out_file, "w", encoding="utf-8") as f:
        while time.time() < end:
            raw = ser.readline()
            if not raw:
                continue
            partial += raw.decode("utf-8", errors="ignore")
            if not partial.endswith("\n"):
                continue   # half a line arrived (the rest comes in the next read), don't split it
            line = partial.rstrip()
            partial = ""
            f.write(line + "\n")
            lines += 1
            if "Beat" in line:
                print(line)
            left = int(end - time.time())
            if lines % 20 == 0:
                print(f"  ... {left} s left")
    ser.close()
    print(f"Done: {lines} lines saved to {out_file}")


if __name__ == "__main__":
    main()
