#!/usr/bin/env python3
"""Generate wiring.html: 10x10 isolated-pad perfboard layout (top + mirrored bottom)."""
import sys

OUT, TEMPLATE = sys.argv[1], sys.argv[2]
COLS = "ABCDEFGHIJ"
NROWS = 10
PITCH = 32
X0, Y0 = 216, 60

def pos(col, row, mirror=False):
    i = COLS.index(col[0])
    if mirror:
        i = 9 - i
    frac = float(col[1:]) if len(col) > 1 else 0.0  # allow "I.5"
    return X0 + (i + (-frac if mirror else frac)) * PITCH, Y0 + (row - 1) * PITCH

def pad(name):
    return name[0], int(name[1:])

def base(svg, mirror):
    add = svg.append
    add('<rect x="192" y="36" width="344" height="344" rx="8" fill="var(--board)" stroke="var(--board-edge)" stroke-width="3"/>')
    for r in range(1, NROWS + 1):
        for c in COLS:
            x, y = pos(c, r, mirror)
            add(f'<rect x="{x-9}" y="{y-9}" width="18" height="18" rx="4" fill="var(--copper)" opacity=".55"/>')
            add(f'<circle cx="{x}" cy="{y}" r="3" fill="var(--hole)"/>')
    for c in COLS:
        x, _ = pos(c, 1, mirror)
        add(f'<text x="{x}" y="28" class="axis" text-anchor="middle">{c}</text>')
    if mirror:
        for r in range(1, NROWS + 1):
            _, y = pos("A", r, mirror)
            add(f'<text x="550" y="{y+4}" class="axis" text-anchor="middle">{r}</text>')

# ---------------- Top view (parts side) ----------------
# Symmetric layout: chip centred on D/G, notch toward row 10; red LED B2/C2, blue LED H2/I2.
top = []
add = top.append
base(top, False)

for r in range(4, 8):
    for c in ("D", "G"):
        x, y = pos(c, r)
        add(f'<rect x="{x-6}" y="{y-4}" width="12" height="8" rx="1" fill="var(--pin)"/>')
dx0, ry0 = pos("D", 4); gx, ry1 = pos("G", 7)
add(f'<rect x="{dx0+10}" y="{ry0-16}" width="{gx-dx0-20}" height="{ry1-ry0+32}" rx="4" fill="var(--chip)"/>')
mx = (dx0 + gx) / 2
add(f'<path d="M{mx-7} {ry1+16} a7 7 0 0 1 14 0" fill="var(--board)"/>')
add(f'<circle cx="{gx-19}" cy="{ry1+6}" r="2.5" fill="var(--chip-ink)"/>')
add(f'<text x="{mx}" y="{(ry0+ry1)/2}" class="chip-name" text-anchor="middle" dominant-baseline="middle" transform="rotate(-90 {mx} {(ry0+ry1)/2})">ATtiny85</text>')
for i, r in enumerate(range(4, 8)):
    _, y = pos("D", r)
    add(f'<text x="{dx0+20}" y="{y+3}" class="pinno" text-anchor="middle">{5+i}</text>')
    add(f'<text x="{gx-20}" y="{y+3}" class="pinno" text-anchor="middle">{4-i}</text>')

def standing_res(body, other, label):
    (bx, by), (ox, oy) = pos(*pad(body)), pos(*pad(other))
    q = f"{(bx+ox)/2} {by-14}" if by == oy else f"{bx-14} {(by+oy)/2}"
    add(f'<path d="M{bx} {by} Q{q} {ox} {oy}" class="lead"/>')
    add(f'<circle cx="{bx}" cy="{by}" r="11" fill="var(--res)" stroke="var(--res-ink)" stroke-width=".8"/>')
    add(f'<text x="{bx}" y="{by+3}" class="rv" text-anchor="middle">{label}</text>')

standing_res("C4", "C3", "1k")
standing_res("H4", "H3", "470")
standing_res("G8", "G10", "10k")

# 0.1 uF: H9 and J9, disc just below
(hx, hy), (jx, jy) = pos("H", 9), pos("J", 9)
add(f'<line x1="{hx}" y1="{hy}" x2="{(hx+jx)/2-5}" y2="{hy-14}" class="lead"/>')
add(f'<line x1="{jx}" y1="{jy}" x2="{(hx+jx)/2+5}" y2="{jy-14}" class="lead"/>')
add(f'<ellipse cx="{(hx+jx)/2}" cy="{hy-18}" rx="11" ry="8" fill="var(--cap)"/>')
add(f'<text x="{(hx+jx)/2}" y="{hy-15}" class="rv" text-anchor="middle">104</text>')

def led(anode, cathode, color, label):
    (ax, ay), (kx, ky) = pos(*pad(anode)), pos(*pad(cathode))
    cx, cy = (ax + kx) / 2, ay
    s_ = 1 if kx > ax else -1
    add(f'<circle cx="{cx}" cy="{cy}" r="29" fill="var({color})" fill-opacity=".28" stroke="var({color})" stroke-width="2.5"/>')
    add(f'<line x1="{cx+25*s_}" y1="{cy-14}" x2="{cx+25*s_}" y2="{cy+14}" stroke="var({color})" stroke-width="2.5"/>')
    add(f'<circle cx="{ax}" cy="{ay}" r="5" fill="none" stroke="var({color})" stroke-width="2"/>')
    add(f'<text x="{ax}" y="{ay+20}" class="plus" text-anchor="middle">+</text>')
    if cx < 360:
        add(f'<text x="142" y="{cy+4}" class="lbl" text-anchor="end">{label}</text>')
    else:
        add(f'<text x="574" y="{cy+4}" class="lbl">{label}</text>')

led("C2", "B2", "--led-red", "Red LED")
led("H2", "I2", "--led-blue", "Blue LED")

def flying(p_, dy, side, color, label):
    x, y = pos(*pad(p_))
    yy = y + dy * PITCH
    ex = 150 if side == "L" else 590
    pts = [(x, y)] + ([(x, yy)] if dy else []) + [(ex, yy)]
    d = "M" + " L".join(f"{px} {py}" for px, py in pts)
    add(f'<path d="{d}" class="wire" stroke="var({color})"/>')
    add(f'<circle cx="{ex}" cy="{yy}" r="3.5" fill="var(--bg)" stroke="var({color})" stroke-width="2"/>')
    if side == "L":
        add(f'<text x="{ex-8}" y="{yy+4}" class="lbl" text-anchor="end">{label}</text>')
    else:
        add(f'<text x="{ex+8}" y="{yy+4}" class="lbl">{label}</text>')

flying("B6", -0.5, "L", "--w-a", "Tilt switch A")
flying("A6", 0, "L", "--w-a", "Tilt A, 2nd leg")
flying("I6", -0.5, "R", "--w-b", "Tilt switch B")
flying("J6", 0, "R", "--w-b", "Tilt B, 2nd leg")
flying("I7", -0.5, "R", "--w-reset", "Reset button")
flying("J7", 0, "R", "--w-reset", "Reset, 2nd leg")
flying("I10", -0.5, "R", "--w-pos", "Battery + (red)")
flying("J10", 0, "R", "--w-neg", "Battery − (black)")

for r in range(1, NROWS + 1):
    _, y = pos("A", r)
    add(f'<rect x="168" y="{y-8}" width="20" height="16" rx="3" fill="var(--panel)"/>')
    add(f'<text x="178" y="{y+4}" class="axis" text-anchor="middle">{r}</text>')

used = ["A6", "B6", "I6", "J6", "I7", "J7", "I10", "J10", "C3", "H3", "G10", "H9", "J9", "C2", "B2", "H2", "I2"]
for p_ in used:
    x, y = pos(*pad(p_))
    add(f'<circle cx="{x}" cy="{y}" r="3.2" fill="var(--solder)"/>')
TOP_SVG = "\n".join(top)

# ---------------- Bottom view (solder side, mirrored) ----------------
bot = []
add = bot.append
base(bot, True)

def run(points, color, width):
    pts = [pos(*pad(p_), True) for p_ in points]
    d = "M" + " L".join(f"{x} {y}" for x, y in pts)
    add(f'<path d="{d}" fill="none" stroke="var({color})" stroke-width="{width+4}" stroke-linecap="round" stroke-linejoin="round" opacity=".9"/>')
    add(f'<path d="{d}" fill="none" stroke="var(--solder)" stroke-width="{width}" stroke-linecap="round" stroke-linejoin="round"/>')

run(["D10", "I10"], "--w-pos", 4)
run(["A6", "A1", "J1", "J10"], "--w-neg", 4)

bridges = [("D4","C4"),("C3","C2"),("B2","B1"),("D6","C6"),("C6","B6"),
           ("D7","D8"),("D8","D9"),("D9","D10"),
           ("G4","F4"),("F4","F3"),("F3","F2"),("F2","F1"),
           ("G5","H5"),("H5","H4"),("H3","H2"),("I2","I1"),
           ("G6","H6"),("H6","I6"),("G7","H7"),("H7","I7"),("G7","G8"),("H9","H10")]
for a_, b_ in bridges:
    (x1, y1), (x2, y2) = pos(*pad(a_), True), pos(*pad(b_), True)
    add(f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="var(--solder)" stroke-width="7" stroke-linecap="round"/>')



solder = set(used) | {f"{c}{r}" for c in "DG" for r in range(4, 8)} | {"C4", "H4", "G8", "A1", "J1"}
for a_, b_ in bridges:
    solder |= {a_, b_}
solder -= {"C1","D1","E1","G1","H1"}
for p_ in sorted(solder):
    x, y = pos(*pad(p_), True)
    add(f'<circle cx="{x}" cy="{y}" r="6" fill="var(--solder)" stroke="var(--board-edge)" stroke-width="1"/>')
for i, r in enumerate(range(4, 8)):
    for c, n in (("D", 5 + i), ("G", 4 - i)):
        x, y = pos(c, r, True)
        dx = -17 if c == "D" else 17
        dy = -12 if (c, r) == ("G", 4) else 3   # keep clear of the G4–F4 ground bridge
        add(f'<text x="{x+dx}" y="{y+dy}" class="pinback" text-anchor="middle">p{n}</text>')
_, y1_ = pos("A", 1, True); _, y10_ = pos("A", 10, True)
add(f'<text x="574" y="{y1_+4}" class="lbl">− bus</text>')
add(f'<text x="174" y="{y10_+4}" class="lbl" text-anchor="end">+ bus</text>')

BOTTOM_SVG = "\n".join(bot)

# ---------------- Pinout ----------------
pin_l = [(1, "RESET", "10 kΩ to +, reset button", "--w-reset"), (2, "PB3", "Tilt switch B (blue)", "--w-b"),
         (3, "PB4", "470 Ω → blue LED", "--led-blue"), (4, "GND", "Battery −", "--w-neg")]
pin_r = [(8, "VCC", "Battery + (4.5 V)", "--w-pos"), (7, "PB2", "Tilt switch A (red)", "--w-a"),
         (6, "PB1", "not used", "--muted"), (5, "PB0", "1 kΩ → red LED", "--led-red")]
p = ['<rect x="250" y="20" width="80" height="140" rx="6" fill="var(--chip)"/>',
     '<path d="M282 20 a8 8 0 0 0 16 0" fill="var(--panel)"/>',
     '<circle cx="262" cy="34" r="3" fill="var(--chip-ink)"/>',
     '<text x="290" y="96" class="chip-name" text-anchor="middle" dominant-baseline="middle" transform="rotate(-90 290 96)">ATtiny85</text>']
for i, (n, sig, desc, col) in enumerate(pin_l):
    y = 42 + i * 32
    p += [f'<rect x="236" y="{y-4}" width="14" height="8" fill="var(--pin)"/>',
          f'<text x="258" y="{y+4}" class="pinno2">{n}</text>',
          f'<line x1="150" y1="{y}" x2="236" y2="{y}" stroke="var({col})" stroke-width="3"/>',
          f'<text x="228" y="{y-6}" class="sig" text-anchor="end">{sig}</text>',
          f'<text x="144" y="{y+4}" class="lbl" text-anchor="end">{desc}</text>']
for i, (n, sig, desc, col) in enumerate(pin_r):
    y = 42 + i * 32
    p += [f'<rect x="330" y="{y-4}" width="14" height="8" fill="var(--pin)"/>',
          f'<text x="322" y="{y+4}" class="pinno2" text-anchor="end">{n}</text>',
          f'<line x1="344" y1="{y}" x2="430" y2="{y}" stroke="var({col})" stroke-width="3"/>',
          f'<text x="352" y="{y-6}" class="sig">{sig}</text>',
          f'<text x="436" y="{y+4}" class="lbl">{desc}</text>']
PIN_SVG = "\n".join(p)

html = open(TEMPLATE).read()
html = html.replace("{{TOP_SVG}}", TOP_SVG).replace("{{BOTTOM_SVG}}", BOTTOM_SVG).replace("{{PINOUT_SVG}}", PIN_SVG)
open(OUT, "w").write(html)
print("wrote", OUT, len(html))

# ======================= Layout B: 20x80 mm, A-F x 1-28 =======================
BC = "ABCDEF"
BR = 28
BP = 28
BX0, BY0 = 150, 46

def bpos(col, row, mirror=False):
    i = BC.index(col)
    if mirror:
        i = 5 - i
    return BX0 + i * BP, BY0 + (row - 1) * BP

def bpad(name):
    return name[0], int(name[1:])

def bbase(svg, mirror):
    a = svg.append
    x0, y0 = BX0 - 20, BY0 - 20
    w, h = 5 * BP + 40, (BR - 1) * BP + 40
    a(f'<rect x="{x0}" y="{y0}" width="{w}" height="{h}" rx="8" fill="var(--board)" stroke="var(--board-edge)" stroke-width="3"/>')
    for r in range(1, BR + 1):
        for c in BC:
            x, y = bpos(c, r, mirror)
            op = ".55" if r <= 19 else ".25"
            a(f'<rect x="{x-8}" y="{y-8}" width="16" height="16" rx="4" fill="var(--copper)" opacity="{op}"/>')
            a(f'<circle cx="{x}" cy="{y}" r="2.8" fill="var(--hole)"/>')
    for c in BC:
        x, _ = bpos(c, 1, mirror)
        a(f'<text x="{x}" y="{BY0-26}" class="axis" text-anchor="middle">{c}</text>')
    for r in range(1, BR + 1):
        _, y = bpos("A", r, mirror)
        a(f'<text x="{BX0 + 5*BP + 26}" y="{y+4}" class="axis" text-anchor="middle">{r}</text>')
    # optional snap line between rows 19 and 20
    _, y19 = bpos("A", 19); _, y20 = bpos("A", 20)
    ys = (y19 + y20) / 2
    a(f'<line x1="{x0-6}" y1="{ys}" x2="{x0+w+6}" y2="{ys}" stroke="var(--fg)" stroke-width="1.5" stroke-dasharray="6 5"/>')
    a(f'<text x="{x0 + w/2}" y="{ys + BP*1.6}" class="lbl small" text-anchor="middle">rows 20–28 spare:</text>')
    a(f'<text x="{x0 + w/2}" y="{ys + BP*1.6 + 15}" class="lbl small" text-anchor="middle">score and snap off</text>')

# ---- B top ----
bt = []
a = bt.append
bbase(bt, False)

# Rotated socket: pins 8..5 on row 11 (B..E), pins 1..4 on row 14 (B..E), notch toward column A
for i, c in enumerate("BCDE"):
    x, y = bpos(c, 11); a(f'<rect x="{x-4}" y="{y-6}" width="8" height="12" rx="1" fill="var(--pin)"/>')
    x, y = bpos(c, 14); a(f'<rect x="{x-4}" y="{y-6}" width="8" height="12" rx="1" fill="var(--pin)"/>')
xb, y11 = bpos("B", 11); xe, y14 = bpos("E", 14)
a(f'<rect x="{xb-14}" y="{y11+9}" width="{xe-xb+28}" height="{y14-y11-18}" rx="4" fill="var(--chip)"/>')
ym = (y11 + y14) / 2
a(f'<path d="M{xb-14} {ym-7} a7 7 0 0 1 0 14" fill="var(--board)"/>')
a(f'<circle cx="{xb-5}" cy="{y14-17}" r="2.5" fill="var(--chip-ink)"/>')
a(f'<text x="{(xb+xe)/2 + 4}" y="{ym+4}" class="chip-name" text-anchor="middle">ATtiny85</text>')
for i, c in enumerate("BCDE"):
    x, _ = bpos(c, 11); a(f'<text x="{x}" y="{y11+20}" class="pinno" text-anchor="middle">{8-i}</text>')
    x, _ = bpos(c, 14); a(f'<text x="{x}" y="{y14-13}" class="pinno" text-anchor="middle">{i+1}</text>')

def bres(body, other, label):
    (bx, by), (ox, oy) = bpos(*bpad(body)), bpos(*bpad(other))
    if by == oy:
        q = f"{(bx+ox)/2} {by-14}"
    else:
        q = f"{bx+14} {(by+oy)/2}"
    a(f'<path d="M{bx} {by} Q{q} {ox} {oy}" class="lead"/>')
    a(f'<circle cx="{bx}" cy="{by}" r="10" fill="var(--res)" stroke="var(--res-ink)" stroke-width=".8"/>')
    a(f'<text x="{bx}" y="{by+3}" class="rv" text-anchor="middle">{label}</text>')

bres("B15", "A15", "10k")
bres("D9", "D8", "470")
bres("E10", "E9", "1k")

# Cap A16/A18, disc over the empty A17 pad
(cx1, cy1), (cx2, cy2) = bpos("A", 16), bpos("A", 18)
a(f'<line x1="{cx1}" y1="{cy1}" x2="{cx1}" y2="{cy2}" class="lead"/>')
a(f'<circle cx="{cx1}" cy="{(cy1+cy2)/2}" r="8.5" fill="var(--cap)"/>')
a(f'<text x="{cx1}" y="{(cy1+cy2)/2+2.5}" class="rv" text-anchor="middle">104</text>')

def bled(anode, cathode, color, label):
    (ax, ay), (kx, ky) = bpos(*bpad(anode)), bpos(*bpad(cathode))
    cx, cy = (ax + kx) / 2, ay
    a(f'<circle cx="{cx}" cy="{cy}" r="31" fill="var({color})" fill-opacity=".28" stroke="var({color})" stroke-width="2.5"/>')
    a(f'<line x1="{cx+27}" y1="{cy-14}" x2="{cx+27}" y2="{cy+14}" stroke="var({color})" stroke-width="2.5"/>')
    a(f'<circle cx="{ax}" cy="{ay}" r="5" fill="none" stroke="var({color})" stroke-width="2"/>')
    a(f'<text x="{ax-9}" y="{ay+4}" class="plus" text-anchor="end">+</text>')
    a(f'<text x="{BX0 + 5*BP + 44}" y="{cy+4}" class="lbl">{label}</text>')

bled("D4", "F4", "--led-blue", "Blue LED")
bled("E7", "F7", "--led-red", "Red LED")

# Off-board wires: half-row offsets keep them between pads
def bwire(p_, dy, side, color, label):
    x, y = bpos(*bpad(p_))
    yy = y + dy * BP
    ex = BX0 - 40 if side == "L" else BX0 + 5 * BP + 44
    pts = [(x, y)] + ([(x, yy)] if dy else []) + [(ex, yy)]
    d = "M" + " L".join(f"{px} {py}" for px, py in pts)
    a(f'<path d="{d}" class="wire" stroke="var({color})"/>')
    a(f'<circle cx="{ex}" cy="{yy}" r="3.5" fill="var(--bg)" stroke="var({color})" stroke-width="2"/>')
    if side == "L":
        a(f'<text x="{ex-8}" y="{yy+4}" class="lbl" text-anchor="end">{label}</text>')
    else:
        a(f'<text x="{ex+8}" y="{yy+4}" class="lbl">{label}</text>')

bwire("A13", 0, "L", "--w-pos", "Battery + (red)")
bwire("B16", -0.5, "L", "--w-reset", "Reset button")
bwire("C16", 0.5, "L", "--w-a", "Tilt switch A")
bwire("B18", -0.5, "L", "--w-reset", "Reset, 2nd leg")
bwire("C18", 0.5, "L", "--w-a", "Tilt A, 2nd leg")
bwire("D16", -0.5, "R", "--w-b", "Tilt switch B")
bwire("D18", -0.5, "R", "--w-b", "Tilt B, 2nd leg")
bwire("E18", 0.5, "R", "--w-neg", "Battery − (black)")

b_used = ["A13", "A15", "B16", "C16", "D16", "B18", "C18", "D18", "E18", "A16", "A18",
          "D8", "E9", "D4", "F4", "E7", "F7"]
for p_ in b_used:
    x, y = bpos(*bpad(p_))
    a(f'<circle cx="{x}" cy="{y}" r="3" fill="var(--solder)"/>')
B_TOP_SVG = "\n".join(bt)

# ---- B bottom (mirrored) ----
bb = []
a = bb.append
bbase(bb, True)

def brun(points, color, width=4):
    pts = [bpos(*bpad(p_), True) for p_ in points]
    d = "M" + " L".join(f"{x} {y}" for x, y in pts)
    a(f'<path d="{d}" fill="none" stroke="var({color})" stroke-width="{width+4}" stroke-linecap="round" stroke-linejoin="round" opacity=".9"/>')
    a(f'<path d="{d}" fill="none" stroke="var(--solder)" stroke-width="{width}" stroke-linecap="round" stroke-linejoin="round"/>')

brun(["A11", "A16"], "--w-pos")
brun(["F4", "F18", "A18"], "--w-neg")

b_bridges = [("B11","A11"), ("B14","B15"), ("B15","B16"), ("C14","C15"), ("C15","C16"),
             ("D14","D15"), ("D15","D16"), ("E14","F14"), ("E11","E10"), ("E9","E8"), ("E8","E7"),
             ("D11","D10"), ("D10","D9")]
for p1, p2 in b_bridges:
    (x1, y1), (x2, y2) = bpos(*bpad(p1), True), bpos(*bpad(p2), True)
    a(f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="var(--solder)" stroke-width="7" stroke-linecap="round"/>')

# Insulated jumper D8 -> D4 on the back
(x1, y1), (x2, y2) = bpos("D", 8, True), bpos("D", 4, True)
a(f'<path d="M{x1} {y1} C{x1+26} {y1-20} {x2+26} {y2+20} {x2} {y2}" fill="none" stroke="var(--led-blue)" stroke-width="3.4" stroke-linecap="round"/>')
a(f'<text x="{BX0 + 5*BP + 44}" y="{(y1+y2)/2+4}" class="lbl small">insulated</text>')
a(f'<text x="{BX0 + 5*BP + 44}" y="{(y1+y2)/2+19}" class="lbl small">wire D8→D4</text>')

b_solder = set(b_used) | {f"{c}{r}" for c in "BCDE" for r in (11, 14)} | {"B15", "D9", "E10"}
for p1, p2 in b_bridges:
    b_solder |= {p1, p2}
for p_ in sorted(b_solder):
    x, y = bpos(*bpad(p_), True)
    a(f'<circle cx="{x}" cy="{y}" r="5.5" fill="var(--solder)" stroke="var(--board-edge)" stroke-width="1"/>')
for i, c in enumerate("BCDE"):
    x, y = bpos(c, 11, True); a(f'<text x="{x}" y="{y+17}" class="pinback" text-anchor="middle">p{8-i}</text>')
    x, y = bpos(c, 14, True); a(f'<text x="{x}" y="{y-11}" class="pinback" text-anchor="middle">p{i+1}</text>')
_, y13 = bpos("A", 13, True); _, y18 = bpos("A", 18, True)
a(f'<text x="{BX0 + 5*BP + 44}" y="{y13+4}" class="lbl">+ bus</text>')
a(f'<text x="{BX0 - 40}" y="{y18+4}" class="lbl" text-anchor="end">− bus</text>')
B_BOTTOM_SVG = "\n".join(bb)

html = open(OUT).read().replace("{{B_TOP_SVG}}", B_TOP_SVG).replace("{{B_BOTTOM_SVG}}", B_BOTTOM_SVG)
open(OUT, "w").write(html)
print("layout B added")
