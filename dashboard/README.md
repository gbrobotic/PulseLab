# PulseLab dashboard

A web page that connects to PulseLab over Bluetooth (Web Bluetooth) and shows heart rate, SpO2, HRV, breathing rate, the pulse wave, a 5 minute trend, and the guided breathing session. No app to install.

- `index.html`: the page
- `pulselab.js`: Bluetooth connection and decoding of the packets (same layout as `firmware/08_ble/ble_packets.h`), plus a demo mode

## Opening it

Web Bluetooth only works on a secure page (`https://...` or `http://localhost`) and in **Chrome or Edge** (Android, Windows, Mac, Linux). Not on iPhone. Double-clicking `index.html` (a `file://` address) is not enough for Bluetooth, but the Demo button works there.

**GitHub Pages (easiest, works on the phone too):** in the GitHub repo, Settings > Pages > Source: "Deploy from a branch", branch `main`, folder `/ (root)`, Save. After a minute the page is at:

```
https://gbrobotic.github.io/PulseLab/dashboard/
```

**On the laptop without GitHub:**

```
cd dashboard
python -m http.server 8000
```

then open `http://localhost:8000` in Chrome.

## Using it

1. Upload `firmware/08_ble` to the board. It shows up as "PulseLab".
2. Open the page, press **Connect**, choose PulseLab in the list.
3. Put a finger on the sensor. The numbers are the same as on the OLED (BPM and SpO2 are hidden in the same cases).
4. **Start session** runs the guided breathing session on the device; the circle on the page follows the one on the OLED.
5. **Download CSV** saves everything received (2 rows per second).

On Android, Chrome may ask for the "Nearby devices" permission and Bluetooth must be on.

**Demo** makes up data (through the same decoding code), so the page can be shown without the board. Add `?fast` to the address to make the demo guided session 10x faster.

## If it doesn't connect

- Only one phone or laptop can be connected at a time. Close the page on the other device first.
- Chrome or Edge (not Firefox, not on iPhone), on an `https://` or `localhost` address.
- Don't pair PulseLab in the Windows / Android Bluetooth settings; the page connects by itself. If it was paired there, remove it.
- The computer needs Bluetooth 4.0 (BLE) and Bluetooth switched on. `chrome://bluetooth-internals` shows if Chrome can use the adapter.
- Mac: System Settings > Privacy > Bluetooth must allow Chrome.
