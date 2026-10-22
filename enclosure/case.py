"""
PulseLab case (v2): a plain box. The finger goes in through a slot in the
front face and lies on the sensor inside, under the lid.

    pip install cadquery pyvista
    python enclosure/case.py

Makes STEP files (to open in Fusion 360) and STL files (to slice for the
Neptune 4) in enclosure/, plus a fit-test plate and preview pictures.

For this version the MAX30100 is soldered straight into the zero PCB (its own
header pins, no female header), so it sits low, and the finger has room above
it inside the box. Same holes as before (Q12-W12), so the wiring doesn't change.

All sizes are parameters at the top. They come from hardware/layout.py (where
everything sits on the board) and from my measurements of the built board.
Heights are measured from the TOP surface of the zero PCB (z = 0).

Coordinates: x to the right, y towards the back (USB side), z up, looking at
the component side. The board's front-left corner is at (0, 0).
The script also builds simple box models of the modules (and of the space the
finger needs) and checks that no part of the case gets in their way.
"""

import os
import cadquery as cq

OUT = os.path.dirname(os.path.abspath(__file__))

# ---------------- board (from hardware/layout.py) ----------------
P = 2.54
EDGE = 1.27
BW, BD, BT = 61.0, 48.3, 1.6          # board width (x), depth (y), thickness


def at(c, r):
    """Hole (column 0-23, row 0-18 as in layout.py) -> case x, y."""
    return EDGE + c * P, BD - (EDGE + r * P)


def board_xy(x_from_left, y_from_back):
    """Point measured on the board drawing (from the top-left corner, y down) -> case x, y."""
    return x_from_left, BD - y_from_back


# ---------------- heights above the board (mm) ----------------
# measured on the female headers: module boards ~12, OLED glass 14, sensor window 13.5-14, USB-C top 15
MODULE_PCB_TOP = 12.0      # OLED / ESP32 boards on their female headers
OLED_RAISE = 2.5           # OLED lifted by an empty male-header plastic strip on its pins (0 = not used)
OLED_GLASS_TOP = 14.0 + OLED_RAISE
USB_TOP = 15.0             # top of the ESP32 USB-C socket
BUTTON_TOP = 13.0          # 6x6x13 button: top of the plunger
# sensor soldered straight into the board with its header pins (2.5 mm plastic under the module)
SENSOR_PCB_BOTTOM = 2.5
SENSOR_PCB_TOP = SENSOR_PCB_BOTTOM + 1.6
SENSOR_TOP = 6.0           # CHECK after soldering: top of the black window (expected 5.5-6)

# ---------------- case parameters ----------------
WALL = 2.0
FLOOR = 2.0
GAP = 0.5                  # board edge to wall
UNDER = 4.0                # room under the board for wires and solder
FINGER_ROOM = 14.0         # height inside the finger slot, above the sensor window
FINGER_W = 20.0           # width of the finger slot (sensor board 19 + clearance)
LID_T = 2.0
LID_Z = max(SENSOR_TOP + FINGER_ROOM, OLED_GLASS_TOP + 0.3, USB_TOP + 0.3)   # underside of the lid
CORNER_R = 3.0

# screw columns (M3 screws, 8-10 mm), outside the board on the left and right
COL_OFF = 4.0              # column centre from the board edge
COL_R = 3.0
SCREW_HOLE = 3.4           # in the lid
SCREW_HEAD = 6.0           # counterbore for the screw head
SCREW_HEAD_DEPTH = 1.0
PILOT = 2.5                # in the base column
COL_Y = [4.0, BD - 4.0]
X_MARGIN = COL_OFF + 1.5   # extra room inside the case on the left and right (for the columns)

# OLED (1.3" SH1106 module, 35.4 x 33.5, pins along the back edge)
OLED_CX = EDGE + 7.5 * P
OLED_TOP_EDGE = EDGE + P - 1.3              # back edge of the module, from the board's back edge
OLED_W, OLED_D = 35.4, 33.5
OLED_ACTIVE_W, OLED_ACTIVE_D = 29.42, 14.7  # lit area of a 1.3" 128x64
OLED_ACTIVE_FROM_TOP = 16.0                 # CHECK: back edge of module -> centre of the lit area
OLED_WIN_MARGIN = 0.8
OLED_FRAME = 1.5                            # frame around the window, from the lid down to just above the glass

# MAX30100 module (assumed 19 x 15.5, pins at the back edge, window 8.5 mm in front of the pin row)
SENS_CX = EDGE + 19 * P
SENS_PIN_Y = EDGE + 11 * P
SENS_W, SENS_D = 19.0, 15.5
SENS_BACK = SENS_PIN_Y - 1.5
SENS_WINDOW_FROM_PINS = 8.5

# finger slot: walls hang down from the lid; the front wall of the base has the opening
SLOT_WALL = 1.2
SLOT_STOP_GAP = 0.3                          # fingertip stop wall, just behind the sensor board
SLOT_EDGE_R = 1.5

# ESP32-C3 SuperMini (18 x 22.5) and its USB-C socket
ESP_CX = EDGE + 19 * P
ESP_BACK = EDGE + P - 2.36
ESP_W, ESP_D = 18.0, 22.5
USB_W, USB_D, USB_H = 9.0, 7.3, 3.2
USB_OVERHANG = 0.7
USB_PLUG_W, USB_PLUG_H = 13.0, 7.5

# button (6x6x13 at F17/I19) and its printed cap, guided by a tube under the lid
BTN_X, BTN_Y = board_xy(EDGE + 6.5 * P, EDGE + 17 * P)
BTN_HOLE = 5.6
CAP_D, CAP_FLANGE_D = 5.0, 8.0
CAP_PROUD = 1.0
TUBE_OD = 8.6
TUBE_BOTTOM = BUTTON_TOP + 2.5              # guide tube ends here; the cap's flange sits below it
CAP_FLANGE_GAP = 0.5
PLUNGER_D = 4.2

# ---------------- derived ----------------
Z_FLOOR_TOP = -BT - UNDER
Z_BOTTOM = Z_FLOOR_TOP - FLOOR
X0, X1 = -X_MARGIN - WALL, BW + X_MARGIN + WALL
Y0, Y1 = -GAP - WALL, BD + GAP + WALL
LID_TOP = LID_Z + LID_T
COLS = [(-COL_OFF, y) for y in COL_Y] + [(BW + COL_OFF, y) for y in COL_Y]

oled_y1 = BD - OLED_TOP_EDGE
oled_win_cy = BD - (OLED_TOP_EDGE + OLED_ACTIVE_FROM_TOP)
win_w = OLED_ACTIVE_W + 2 * OLED_WIN_MARGIN
win_d = OLED_ACTIVE_D + 2 * OLED_WIN_MARGIN
sens_x0, sens_x1 = SENS_CX - SENS_W / 2, SENS_CX + SENS_W / 2
sens_y1 = BD - SENS_BACK
sens_y0 = sens_y1 - SENS_D
sens_win = (SENS_CX, BD - (SENS_PIN_Y + SENS_WINDOW_FROM_PINS))
esp_y1 = BD - ESP_BACK
usb_y1 = esp_y1 + USB_OVERHANG
slot_x0, slot_x1 = SENS_CX - FINGER_W / 2, SENS_CX + FINGER_W / 2
slot_stop = sens_y1 + SLOT_STOP_GAP          # fingertip stop (inside face)
slot_floor = SENSOR_TOP - 1.0                # bottom of the opening in the front wall
oled_x1 = OLED_CX + OLED_W / 2


def box(x0, x1, y0, y1, z0, z1):
    return cq.Workplane("XY").box(x1 - x0, y1 - y0, z1 - z0, centered=False).translate((x0, y0, z0))


def cyl(x, y, z0, z1, d):
    return cq.Workplane("XY").circle(d / 2).extrude(z1 - z0).translate((x, y, z0))


# ---------------- base ----------------
def make_base():
    outer = (cq.Workplane("XY").box(X1 - X0, Y1 - Y0, LID_Z - Z_BOTTOM, centered=False)
             .translate((X0, Y0, Z_BOTTOM)).edges("|Z").fillet(CORNER_R).edges("<Z").fillet(1.0))
    base = outer.cut(box(X0 + WALL, X1 - WALL, Y0 + WALL, Y1 - WALL, Z_FLOOR_TOP, LID_Z + 1))
    for x, y in COLS:
        base = base.union(cyl(x, y, Z_FLOOR_TOP - 0.1, LID_Z, 2 * COL_R))
        base = base.cut(cyl(x, y, LID_Z - 12, LID_Z + 1, PILOT))
    # board supports: 4 corners + one under the sensor (finger pressure), all on unused holes
    s = 3.5
    for x, y in [(0, 0), (BW - s, 0), (0, BD - s), (BW - s, BD - s)]:
        base = base.union(box(x, x + s, y, y + s, Z_FLOOR_TOP - 0.1, -BT))
    tx, ty = at(19, 15)
    base = base.union(box(tx - s / 2, tx + s / 2, ty - s / 2, ty + s / 2, Z_FLOOR_TOP - 0.1, -BT))
    # USB-C opening in the back wall
    usb_cz = USB_TOP - USB_H / 2
    base = base.cut(box(ESP_CX - USB_PLUG_W / 2, ESP_CX + USB_PLUG_W / 2, BD, Y1 + 1,
                        usb_cz - USB_PLUG_H / 2, usb_cz + USB_PLUG_H / 2).edges("|Y").fillet(1.0))
    # finger opening in the front wall, rounded edges so it's comfortable
    opening = box(slot_x0, slot_x1, Y0 - 1, Y0 + WALL + 0.5, slot_floor, LID_Z + 1)
    base = base.cut(opening)
    try:
        base = base.edges(cq.selectors.BoxSelector((slot_x0 - 0.1, Y0 - 0.1, slot_floor - 0.1),
                                                   (slot_x1 + 0.1, Y0 + WALL + 0.1, slot_floor + 0.1))).fillet(SLOT_EDGE_R * 0.6)
    except Exception as e:
        print("finger opening edge not rounded:", e)
    return base


# ---------------- lid ----------------
def lid_plate(thickness, full=True):
    """full=False: just the plate with its holes (fit test)."""
    top = LID_Z + thickness
    lid = (cq.Workplane("XY").box(X1 - X0, Y1 - Y0, thickness, centered=False)
           .translate((X0, Y0, LID_Z)).edges("|Z").fillet(CORNER_R))
    if full:
        lid = lid.faces(">Z").edges().fillet(0.8)
        # OLED frame: walls around the window, down to just above the glass (keeps the inside of the box hidden)
        fw, fd = win_w + 2 * OLED_FRAME, win_d + 2 * OLED_FRAME
        lid = lid.union(box(OLED_CX - fw / 2, OLED_CX + fw / 2, oled_win_cy - fd / 2, oled_win_cy + fd / 2,
                            OLED_GLASS_TOP + 0.3, LID_Z + 0.1))
        # finger slot walls (left, right, fingertip stop), from the lid down to just above the board
        zb = 0.5
        lid = lid.union(box(slot_x0 - SLOT_WALL, slot_x0, Y0 + WALL + 0.25, slot_stop + SLOT_WALL, zb, LID_Z + 0.1))
        lid = lid.union(box(slot_x1, slot_x1 + SLOT_WALL, Y0 + WALL + 0.25, slot_stop + SLOT_WALL, zb, LID_Z + 0.1))
        lid = lid.union(box(slot_x0 - SLOT_WALL, slot_x1 + SLOT_WALL, slot_stop, slot_stop + SLOT_WALL, zb, LID_Z + 0.1))
        # button guide tube
        lid = lid.union(cyl(BTN_X, BTN_Y, TUBE_BOTTOM, LID_Z + 0.1, TUBE_OD))
    for x, y in COLS:
        lid = lid.cut(cyl(x, y, LID_Z - 1, top + 1, SCREW_HOLE))
        if full:
            lid = lid.cut(cyl(x, y, top - SCREW_HEAD_DEPTH, top + 1, SCREW_HEAD))
    # OLED window, chamfered at the top
    ch = min(1.2, thickness * 0.6)
    win = box(OLED_CX - win_w / 2, OLED_CX + win_w / 2, oled_win_cy - win_d / 2, oled_win_cy + win_d / 2,
              OLED_GLASS_TOP - 1, top + 1)
    cham = (cq.Workplane("XY").rect(win_w, win_d).workplane(offset=ch + 0.02).rect(win_w + 2 * ch, win_d + 2 * ch)
            .loft().translate((OLED_CX, oled_win_cy, top - ch)))
    lid = lid.cut(win).cut(cham)
    lid = lid.cut(cyl(BTN_X, BTN_Y, TUBE_BOTTOM - 1, top + 1, BTN_HOLE))
    if full:
        try:
            ty = (BTN_Y + BTN_HOLE / 2 + oled_win_cy - (win_d / 2 + ch)) / 2
            txt = (cq.Workplane("XY").workplane(offset=top - 0.6)
                   .text("PulseLab", 5.0, 1.0, kind="bold", halign="center", valign="center")
                   .translate((OLED_CX + 4, ty, 0)))
            lid = lid.cut(txt)
        except Exception as e:
            print("text skipped:", e)
    return lid


def make_lid():
    return lid_plate(LID_T)


# ---------------- button cap ----------------
def make_cap():
    z_bottom = BUTTON_TOP - 1.0
    z_flange_top = TUBE_BOTTOM - CAP_FLANGE_GAP
    z_top = LID_TOP + CAP_PROUD
    cap = cyl(BTN_X, BTN_Y, z_bottom, z_flange_top, CAP_FLANGE_D)
    cap = cap.union(cyl(BTN_X, BTN_Y, z_flange_top - 0.01, z_top, CAP_D))
    cap = cap.faces(">Z").edges().fillet(0.6)
    cap = cap.cut(cyl(BTN_X, BTN_Y, z_bottom - 0.1, BUTTON_TOP, PLUNGER_D))
    return cap


# ---------------- simple models of what's inside, for the check ----------------
def make_inside():
    parts = {}
    parts["zero PCB"] = box(0, BW, 0, BD, -BT, 0)
    s = 1.75
    tx, ty = at(19, 15)
    under = box(0, BW, 0, BD, -BT - 2.0, -BT)
    for x, y in [(0, 0), (BW - 3.5, 0), (0, BD - 3.5), (BW - 3.5, BD - 3.5)]:
        under = under.cut(box(x, x + 3.5, y, y + 3.5, -9, 0))
    parts["wires under the board"] = under.cut(box(tx - s, tx + s, ty - s, ty + s, -9, 0))
    r = OLED_RAISE
    parts["OLED board"] = box(OLED_CX - OLED_W / 2, oled_x1, oled_y1 - OLED_D, oled_y1, MODULE_PCB_TOP - 1.6 + r, MODULE_PCB_TOP + r)
    parts["OLED glass"] = box(OLED_CX - 17.25, OLED_CX + 17.25, oled_y1 - 28.5, oled_y1 - 5.5, MODULE_PCB_TOP + r, OLED_GLASS_TOP)
    parts["OLED pin solder"] = box(at(6, 1)[0] - 1.5, at(9, 1)[0] + 1.5, at(6, 1)[1] - 1.5, at(6, 1)[1] + 1.5,
                                   MODULE_PCB_TOP + r, MODULE_PCB_TOP + r + 1.5)
    parts["OLED header"] = box(at(6, 1)[0] - 1.27, at(9, 1)[0] + 1.27, at(6, 1)[1] - 1.27, at(6, 1)[1] + 1.27,
                               0, MODULE_PCB_TOP - 1.6 + r)
    parts["ESP32 board"] = box(ESP_CX - ESP_W / 2, ESP_CX + ESP_W / 2, esp_y1 - ESP_D, esp_y1, MODULE_PCB_TOP - 1.6, MODULE_PCB_TOP)
    parts["ESP32 parts"] = box(ESP_CX - ESP_W / 2 + 0.5, ESP_CX + ESP_W / 2 - 0.5, esp_y1 - ESP_D + 0.5, esp_y1 - USB_D,
                               MODULE_PCB_TOP, MODULE_PCB_TOP + 1.8)
    parts["USB-C socket"] = box(ESP_CX - USB_W / 2, ESP_CX + USB_W / 2, usb_y1 - USB_D, usb_y1, USB_TOP - USB_H, USB_TOP)
    for name, c in [("ESP32 header L", 16), ("ESP32 header R", 22)]:
        x = at(c, 0)[0]
        parts[name] = box(x - 1.27, x + 1.27, at(0, 8)[1] - 1.27, at(0, 1)[1] + 1.27, 0, MODULE_PCB_TOP - 1.6)
    parts["sensor pins plastic"] = box(at(16, 11)[0] - 1.27, at(22, 11)[0] + 1.27, at(16, 11)[1] - 1.27, at(16, 11)[1] + 1.27,
                                       0, SENSOR_PCB_BOTTOM)
    parts["sensor board"] = box(sens_x0, sens_x1, sens_y0, sens_y1, SENSOR_PCB_BOTTOM, SENSOR_PCB_TOP)
    parts["sensor parts"] = box(sens_x0 + 0.5, sens_x1 - 0.5, sens_y0 + 0.5, sens_y1 - 0.5, SENSOR_PCB_TOP, SENSOR_PCB_TOP + 1.5)
    parts["sensor chip"] = box(sens_win[0] - 2.8, sens_win[0] + 2.8, sens_win[1] - 1.4, sens_win[1] + 1.4, SENSOR_PCB_TOP, SENSOR_TOP)
    parts["button"] = box(BTN_X - 3, BTN_X + 3, BTN_Y - 3, BTN_Y + 3, 0, 3.5).union(cyl(BTN_X, BTN_Y, 3.5, BUTTON_TOP, 3.5))
    # the space the finger needs: from the front face to the fingertip stop, above the sensor window
    parts["finger space"] = box(slot_x0 + 0.05, slot_x1 - 0.05, Y0 - 1, slot_stop - 0.05, SENSOR_TOP + 0.05, LID_Z - 0.05)
    return parts


def volume(wp):
    try:
        return sum(s.Volume() for s in wp.solids().vals())
    except Exception:
        return 0.0


def check(case_parts, inside):
    problems = []
    for cname, cpart in case_parts.items():
        for iname, ipart in inside.items():
            v = volume(cpart.intersect(ipart))
            if v > 0.01:
                problems.append(f"{cname} touches {iname} ({v:.2f} mm3)")
    for a, b in [("lid", "button cap"), ("lid", "base")]:
        v = volume(case_parts[a].intersect(case_parts[b]))
        if v > 0.01:
            problems.append(f"{a} touches {b} ({v:.2f} mm3)")
    return problems


def to_mesh(wp):
    import numpy as np
    import pyvista as pv
    verts, tris = wp.val().tessellate(0.05, 0.2)
    v = np.array([(p.x, p.y, p.z) for p in verts])
    f = np.hstack([[3, *t] for t in tris])
    return pv.PolyData(v, f)


def render(shapes, path, view, title, zoom=1.0):
    """shapes: list of (workplane, colour). view: camera direction (x, y, z) from the model."""
    import pyvista as pv
    pv.OFF_SCREEN = True
    pl = pv.Plotter(off_screen=True, window_size=(1300, 950))
    pl.set_background("white")
    for wp, color in shapes:
        m = to_mesh(wp)
        pl.add_mesh(m, color=color, smooth_shading=False, specular=0.2)
        pl.add_mesh(m.extract_feature_edges(35), color="#202020", line_width=1)
    pl.add_text(title, font_size=11, color="black")
    c = pl.renderer.ComputeVisiblePropBounds()
    ctr = ((c[0] + c[1]) / 2, (c[2] + c[3]) / 2, (c[4] + c[5]) / 2)
    pl.camera.focal_point = ctr
    pl.camera.position = (ctr[0] + view[0] * 300, ctr[1] + view[1] * 300, ctr[2] + view[2] * 300)
    pl.camera.up = (0, 0, 1)
    pl.reset_camera()
    pl.camera.zoom(zoom)
    pl.screenshot(path)
    pl.close()


if __name__ == "__main__":
    base, lid, cap = make_base(), make_lid(), make_cap()
    case_parts = {"base": base, "lid": lid, "button cap": cap}
    inside = make_inside()
    problems = check(case_parts, inside)
    print("check:", "OK, nothing in the way" if not problems else "")
    for p in problems:
        print("  ", p)

    def save(wp, name, flip=False):
        cq.exporters.export(wp, os.path.join(OUT, name + ".step"))
        s = wp.rotate((0, 0, 0), (1, 0, 0), 180) if flip else wp
        bb = s.val().BoundingBox()
        s = s.translate((-bb.center.x, -bb.center.y, -bb.zmin))
        cq.exporters.export(s, os.path.join(OUT, name + ".stl"), tolerance=0.02, angularTolerance=0.1)
    save(base, "case_base")                 # prints upright
    save(lid, "case_lid", flip=True)        # prints upside down: flat top on the bed, walls going up
    save(cap, "button_cap")                 # prints flange down
    plate = lid_plate(1.2, full=False)
    bb = plate.val().BoundingBox()
    cq.exporters.export(plate.translate((0, 0, -bb.zmin)), os.path.join(OUT, "fit_test_plate.stl"), tolerance=0.02)

    print(f"case outside: {X1 - X0:.1f} x {Y1 - Y0:.1f} x {LID_TOP - Z_BOTTOM:.1f} mm (a plain box)")
    print(f"lid underside {LID_Z:.1f} mm above the board, lid top {LID_TOP:.1f}")
    print(f"finger slot: {slot_x1 - slot_x0:.1f} mm wide, {LID_Z - SENSOR_TOP:.1f} mm above the sensor window, "
          f"fingertip stop {slot_stop - sens_win[1]:.1f} mm behind the window centre, opening bottom {slot_floor:.1f} mm")
    print(f"OLED glass {LID_TOP - OLED_GLASS_TOP:.1f} mm below the lid top")
    print(f"button cap: {LID_TOP + CAP_PROUD - (BUTTON_TOP - 1.0):.1f} mm tall, sticks out {CAP_PROUD} mm")

    img = os.path.join(OUT, "images")
    os.makedirs(img, exist_ok=True)
    for f in os.listdir(img):
        os.remove(os.path.join(img, f))
    colors = {"zero PCB": "#d9c27a", "OLED board": "#2a5db0", "OLED glass": "#151515", "OLED header": "#222222",
              "ESP32 board": "#3a3a3a", "ESP32 parts": "#555555", "USB-C socket": "#c0c0c0",
              "ESP32 header L": "#222222", "ESP32 header R": "#222222", "sensor pins plastic": "#222222",
              "sensor board": "#2e8b57", "sensor chip": "#151515", "button": "#666666"}
    inside_vis = [(inside[k], c) for k, c in colors.items()]
    BASE_C, LID_C, CAP_C = "#8f99a6", "#3d434c", "#d04545"
    front_right = (0.55, -1.0, 0.75)
    render([(base, BASE_C), (lid, LID_C), (cap, CAP_C)], os.path.join(img, "case_closed.png"),
           front_right, "PulseLab case v2: finger goes into the slot in the front face")
    up = 32.0
    render([(base, BASE_C)] + inside_vis + [(cap.translate((0, 0, up * 0.5)), CAP_C), (lid.translate((0, 0, up)), LID_C)],
           os.path.join(img, "case_exploded.png"), front_right, "Exploded: base, board, button cap, lid with the finger slot walls")
    render([(lid, LID_C)], os.path.join(img, "case_lid_under.png"), (0.5, -0.8, -0.9),
           "Lid from below: finger slot walls, OLED frame, button guide")
    cutter = box(SENS_CX, X1 + 5, Y0 - 5, Y1 + 5, Z_BOTTOM - 5, 60)
    sec = [(wp.intersect(cutter), c) for wp, c in [(base, BASE_C), (lid, LID_C)] + inside_vis]
    sec = [(wp, c) for wp, c in sec if volume(wp) > 0.001]
    render(sec, os.path.join(img, "case_section.png"), (-1.0, -0.15, 0.12),
           "Cut through the sensor: finger slot from the front (right), sensor low on the board, USB-C at the back", zoom=1.25)
