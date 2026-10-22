"""
PulseLab zero PCB layout, drawn to scale on a 2.54 mm hole grid.

    pip install matplotlib
    python hardware/layout.py

Makes hardware/layout_top.png (component side) and hardware/layout_bottom.png
(solder side, mirrored, the way you see it when you turn the board over).
Holes are named by column letter + row number, like on a chess board: A1 is
the top-left hole when looking at the component side with the back edge (USB)
at the top.

Everything is in this file as data, so if a module turns out to be a different
size, change the numbers here and run it again. It also checks that no two
raised modules overlap and that every pin of every net is wired.
"""

import string
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle, Circle, FancyBboxPatch

P = 2.54                      # hole pitch (mm)
COLS, ROWS = 24, 19           # holes used: A..X, 1..19
EDGE = 1.27                   # board edge beyond the outer holes (half a pitch)


def col_name(c):
    return string.ascii_uppercase[c]


def hole(c, r):
    return f"{col_name(c)}{r + 1}"


def xy(c, r):
    return c * P, r * P


# ---------- parts: pins (col, row, label, net) and outlines (mm) ----------
# Raised modules sit on female headers (~8.5 mm), low parts go underneath.

NET_COLORS = {"3V3": "#d62728", "GND": "#222222", "SDA": "#1f77b4", "SCL": "#2ca02c", "BTN": "#9467bd", "": "#aaaaaa"}

parts = []

# 1.3" OLED (SH1106), 4 pins along its top edge: GND VCC SCL SDA. Board 35.4 x 33.5 mm.
oled_pins = [(6, 1, "GND", "GND"), (7, 1, "VCC", "3V3"), (8, 1, "SCL", "SCL"), (9, 1, "SDA", "SDA")]
cx = 7.5 * P
parts.append(dict(name="OLED 1.3\" (on female header)", pins=oled_pins, raised=True,
                  box=(cx - 35.4 / 2, 1 * P - 1.3, 35.4, 33.5), color="#4a90d9"))

# ESP32-C3 SuperMini, USB to the back edge. 2 rows of 8 pins, rows 6 holes (15.24 mm) apart.
# Pin labels as printed on the board (USB at the top, component side up). CHECK against your board.
left = ["5V", "GND", "3V3", "4", "3", "2", "1", "0"]
right = ["5", "6", "7", "8", "9", "10", "20", "21"]
nets_left = {"GND": "GND", "3V3": "3V3", "4": "BTN"}
nets_right = {"6": "SDA", "7": "SCL"}
esp_pins = [(16, 1 + i, l, nets_left.get(l, "")) for i, l in enumerate(left)] + \
           [(22, 1 + i, l, nets_right.get(l, "")) for i, l in enumerate(right)]
usb_end = 1 * P - 3.5
parts.append(dict(name="ESP32-C3 SuperMini (on female headers)", pins=esp_pins, raised=True,
                  box=(16 * P - 1.38, usb_end, 18.0, 22.5), color="#3b3b3b", usb=True))

# MAX30100 module, soldered straight into the board (low, so the finger fits above it in the case).
# 7 pins in one row: VIN SCL SDA INT IRD RD GND (CHECK the order on yours).
# Body size varies by seller; ~15 x 18 mm drawn here, pins along its back edge.
sens = ["VIN", "SCL", "SDA", "INT", "IRD", "RD", "GND"]
sens_nets = {"VIN": "3V3", "SCL": "SCL", "SDA": "SDA", "GND": "GND"}
sens_pins = [(16 + i, 11, l, sens_nets.get(l, "")) for i, l in enumerate(sens)]
sx = 19 * P
parts.append(dict(name="MAX30100 (soldered in)", pins=sens_pins, raised=True,
                  box=(sx - 9.5, 11 * P - 1.5, 19.0, 15.5), color="#2e8b57", window=(sx, 11 * P + 8.5)))

# Pull-up resistors 4.7k, lying flat under the OLED (low, fits under the raised OLED).
parts.append(dict(name="R1 4.7k (SDA pull-up)", pins=[(11, 5, "", "3V3"), (14, 5, "", "SDA")], raised=False,
                  resistor=True, color="#c8a165"))
parts.append(dict(name="R2 4.7k (SCL pull-up)", pins=[(11, 7, "", "3V3"), (14, 7, "", "SCL")], raised=False,
                  resistor=True, color="#c8a165"))

# Push button 6x6 mm (tall version, 6x6x13 mm, so it reaches the lid). Its 4 legs are connected in pairs,
# and which pairs depends on the button. Two DIAGONAL legs are always switched, so only those are wired.
parts.append(dict(name="Button 6x6x13 (GPIO4)", pins=[(5, 16, "", "BTN"), (5, 18, "", ""), (8, 16, "", ""), (8, 18, "", "GND")],
                  raised=True, box=(5 * P + 0.3, 16 * P - 0.8, 7.1, 6.7), color="#777777", button=True))

# ---------- wires (solder side) ----------
# Insulated wire (e.g. 0.5 mm solid core / 30 AWG wire-wrap), soldered ONLY at the
# two pins it connects. Each net is a chain from pin to pin, so no hole ever has
# more than two wires. Wires may cross each other (they're insulated).
chains = [
    ("3V3", [(7, 1), (11, 5), (11, 7), (16, 3), (16, 11)]),   # OLED VCC -> R1 -> R2 -> ESP 3V3 -> sensor VIN
    ("GND", [(6, 1), (16, 2), (22, 11), (8, 18)]),            # OLED GND -> ESP GND -> sensor GND -> button
    ("SDA", [(9, 1), (14, 5), (22, 2), (18, 11)]),            # OLED SDA -> R1 -> ESP GPIO6 -> sensor SDA
    ("SCL", [(8, 1), (14, 7), (22, 3), (17, 11)]),            # OLED SCL -> R2 -> ESP GPIO7 -> sensor SCL
    ("BTN", [(16, 4), (5, 16)]),                              # ESP GPIO4 -> button
]
wires = []          # (net, start, end) one entry per wire
for net, pts in chains:
    for a, b in zip(pts, pts[1:]):
        wires.append((net, a, b))


def check():
    problems = []
    # raised modules must not overlap
    boxes = [(p["name"], p["box"]) for p in parts if p.get("raised") and "box" in p]
    for i in range(len(boxes)):
        for j in range(i + 1, len(boxes)):
            (n1, (x1, y1, w1, h1)), (n2, (x2, y2, w2, h2)) = boxes[i], boxes[j]
            if x1 < x2 + w2 - 0.01 and x2 < x1 + w1 - 0.01 and y1 < y2 + h2 - 0.01 and y2 < y1 + h1 - 0.01:
                problems.append(f"overlap: {n1} / {n2}")
    pins = {}
    for p in parts:
        for (c, r, label, net) in p["pins"]:
            if (c, r) in pins:
                problems.append(f"two pins in one hole {hole(c, r)}")
            pins[(c, r)] = (p["name"], label, net)
    ends = {}
    for net, a, b in wires:
        for e in (a, b):
            if e not in pins:
                problems.append(f"{net} wire ends at {hole(*e)}, which is not a pin")
            elif pins[e][2] != net:
                problems.append(f"{net} wire ends on {pins[e][0]} {pins[e][1]} ({pins[e][2]})")
            ends[e] = ends.get(e, 0) + 1
    for k, n in ends.items():
        if n > 2:
            problems.append(f"{n} wires at {hole(*k)}")
    for k, (name, label, net) in pins.items():
        if net and k not in ends:
            problems.append(f"not wired: {name} {label} at {hole(*k)} ({net})")
    for net, a, b in wires:
        n = crossings(route(a, b), a, b)
        if n:
            problems.append(f"{net} wire {hole(*a)} -> {hole(*b)} passes over {n} other pin(s)")
    return problems


PIN_HOLES = {(c, r) for p in parts for (c, r, label, net) in p["pins"]}


def crossings(path, a, b):
    """How many other pins a drawn route passes over (the wire's own two ends don't count)."""
    n = 0
    for (c1, r1), (c2, r2) in zip(path, path[1:]):
        if r1 == r2:
            cells = [(c, r1) for c in range(min(c1, c2), max(c1, c2) + 1)]
        else:
            cells = [(c1, r) for r in range(min(r1, r2), max(r1, r2) + 1)]
        n += sum(1 for h in cells if h in PIN_HOLES and h not in (a, b))
    return n


def route(a, b):
    """Pick a route made of straight pieces that passes over as few other pins as
    possible: two L shapes, or a Z through any free row or column."""
    (c1, r1), (c2, r2) = a, b
    options = [[a, (c2, r1), b], [a, (c1, r2), b]]
    options += [[a, (c1, rr), (c2, rr), b] for rr in range(ROWS)]
    options += [[a, (cc, r1), (cc, r2), b] for cc in range(COLS)]
    # three bends: out along a free row, across, then in along a free column
    options += [[a, (c1, rr), (cc, rr), (cc, r2), b] for rr in range(ROWS) for cc in range(COLS)]
    options += [[a, (cc, r1), (cc, rr), (c2, rr), b] for rr in range(ROWS) for cc in range(COLS)]
    def length(pth):
        return sum(abs(x1 - x2) + abs(y1 - y2) for (x1, y1), (x2, y2) in zip(pth, pth[1:]))
    return min(options, key=lambda pth: (crossings(pth, a, b), length(pth), len(pth)))


def draw(mirror, path):
    fig, ax = plt.subplots(figsize=(11, 9.2))
    W, H = (COLS - 1) * P, (ROWS - 1) * P
    X = (lambda x: W - x) if mirror else (lambda x: x)
    ax.add_patch(Rectangle((-EDGE, -EDGE), W + 2 * EDGE, H + 2 * EDGE, fc="#f3e3b5", ec="#8a6d2b", lw=1.5))
    for c in range(COLS):
        for r in range(ROWS):
            ax.add_patch(Circle((X(c * P), r * P), 0.45, fc="#ffffff", ec="#b89a52", lw=0.5))
    for c in range(COLS):
        ax.text(X(c * P), -EDGE - 1.2, col_name(c), ha="center", va="bottom", fontsize=7, color="#6b5520")
    for r in range(ROWS):
        ax.text(X(-EDGE - 1.0) if not mirror else W + EDGE + 1.0, r * P, str(r + 1), ha="center", va="center", fontsize=7, color="#6b5520")

    # wires (solder side) - drawn full on the bottom view, faint on the top view
    for k, (net, a, b) in enumerate(wires):
        pth = route(a, b)
        off = (k % 3 - 1) * 0.35          # small offset so parallel wires don't sit on top of each other
        xs = [X(c * P + off) for c, r in pth]
        ys = [r * P + off for c, r in pth]
        ax.plot(xs, ys, color=NET_COLORS[net], lw=2.6 if mirror else 1.2, alpha=0.95 if mirror else 0.35,
                solid_capstyle="round", zorder=3)

    for p in parts:
        if "box" in p and not mirror:
            x, y, w, h = p["box"]
            ax.add_patch(FancyBboxPatch((X(x) if not mirror else X(x + w), y), w, h, boxstyle="round,pad=0,rounding_size=0.8",
                                        fc=p["color"], ec="black", alpha=0.18 if p.get("raised") else 0.5, lw=1.2, ls="--", zorder=2))
            ax.text(X(x + w / 2), y + h / 2 + (3 if p.get("usb") else 0), p["name"], ha="center", va="center", fontsize=8, zorder=6,
                    bbox=dict(fc="white", ec="none", alpha=0.8, pad=1))
            if p.get("usb"):
                ax.add_patch(Rectangle((X(x + w / 2 - 4.5), y - 0.5), 9.0, 3.5, fc="#bbbbbb", ec="black", zorder=4))
                ax.text(X(x + w / 2), y + 1.2, "USB-C", ha="center", va="center", fontsize=6, zorder=5)
            if p.get("window"):
                wx, wy = p["window"]
                ax.add_patch(Rectangle((X(wx) - 2.8, wy - 2.8), 5.6, 5.6, fc="#ff9999", ec="#aa0000", zorder=4))
                ax.text(X(wx), wy + 4.3, "finger here", ha="center", fontsize=7, color="#aa0000", zorder=5)
        if p.get("resistor"):
            (c1, r1, *_), (c2, r2, *_) = p["pins"]
            ax.plot([X(c1 * P), X(c2 * P)], [r1 * P, r2 * P], color="#8b5a2b", lw=1, zorder=4)
            ax.add_patch(Rectangle((X((c1 + c2) / 2 * P) - 2.6, r1 * P - 0.9), 5.2, 1.8, fc=p["color"], ec="black", zorder=5))
            ax.text(X((c1 + c2) / 2 * P), r1 * P - 1.6, p["name"].split(" (")[0], ha="center", fontsize=6.5, zorder=6)
        for (c, r, label, net) in p["pins"]:
            ax.add_patch(Circle((X(c * P), r * P), 0.8, fc=NET_COLORS[net] if net else "#cccccc", ec="black", lw=0.6, zorder=7))
            if label:
                if p["name"].startswith("ESP"):
                    dx = -2.3 if c == 16 else 2.3          # outside the module, next to the pin
                    tx, ty = X(c * P + dx), r * P
                else:
                    tx, ty = X(c * P), r * P + (-1.6 if r == 1 else 1.75)
                ax.text(tx, ty, label, ha="center", va="center", fontsize=5.6, zorder=8,
                        bbox=dict(fc="white", ec="none", alpha=0.9, pad=0.4))

    handles = [plt.Line2D([0], [0], color=v, lw=3, label=k) for k, v in NET_COLORS.items() if k]
    ax.legend(handles=handles, loc="lower center", bbox_to_anchor=(0.5, -0.11), ncol=5, frameon=False, fontsize=9)
    side = "SOLDER SIDE (board turned over: left and right are swapped)" if mirror else "COMPONENT SIDE (top) - dashed boxes: modules on female headers, lines: wires underneath"
    ax.set_title(f"PulseLab zero PCB - {side}\n{COLS} x {ROWS} holes = {W + 2 * EDGE:.1f} x {H + 2 * EDGE:.1f} mm      back edge (USB) at the top", fontsize=10)
    ax.set_xlim(-EDGE - 3, W + EDGE + 3)
    ax.set_ylim(H + EDGE + 3, -EDGE - 4)
    ax.set_aspect("equal")
    ax.axis("off")
    fig.tight_layout()
    fig.savefig(path, dpi=130)
    plt.close(fig)


if __name__ == "__main__":
    probs = check()
    print("checks:", "OK" if not probs else "")
    for p in probs:
        print("  ", p)
    draw(False, "hardware/layout_top.png")
    draw(True, "hardware/layout_bottom.png")
    print("board:", f"{COLS} x {ROWS} holes = {(COLS - 1) * P + 2 * EDGE:.1f} x {(ROWS - 1) * P + 2 * EDGE:.1f} mm")
    pins = {(c, r): (p["name"].split(" (")[0], label) for p in parts for (c, r, label, net) in p["pins"]}
    short = {"OLED 1.3\"": "OLED", "ESP32-C3 SuperMini": "ESP32", "R1 4.7k": "R1", "R2 4.7k": "R2",
             "MAX30100": "MAX30100", "Button 6x6x13": "Button"}
    def pin_name(k):
        part, label = pins[k]
        part = short.get(part, part)
        return f"{part} {label}".strip()
    print("wires (cut length = route + 10 mm for bends and stripping):")
    total = 0
    for i, (net, a, b) in enumerate(wires, 1):
        pth = route(a, b)
        mm = sum(abs(x1 - x2) + abs(y1 - y2) for (x1, y1), (x2, y2) in zip(pth, pth[1:])) * P + 10
        total += mm
        print(f"  {i:2d}. {net:3s}  {hole(*a):4s} {pin_name(a):12s} -> {hole(*b):4s} {pin_name(b):14s} {mm:4.0f} mm")
    print(f"  total wire: about {total / 10:.0f} cm")
