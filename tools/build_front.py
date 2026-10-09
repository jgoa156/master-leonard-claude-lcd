# Centered sprite: horns from view_front_a.txt + the skull centre of reference.png.
#   python build_front.py      -> view_front.txt / view_front.png
# ref_front.txt is reference.png transcribed cell-for-cell (transcribe.py).
# Its centre skull (rows REF_ROWS, cols REF_COLS) is pasted with its nasal
# suture on the sprite axis; rows in STRETCH are repeated to make the face a
# little taller, matching the proportions of the side sprites.
import os
from make_views import render

import paths as P
REF_ROWS = range(12, 37)              # crown .. snout tip
REF_COLS = (21, 61)                   # widest crop window, left .. right (exclusive)
# skull outline per reference row (left, right exclusive); cells outside are
# the reference's own horns and are dropped
BOUNDS = {12: (23, 58), 13: (23, 61), 14: (22, 58), 15: (23, 57), 16: (24, 58), 17: (24, 58),
          18: (24, 58), 19: (26, 58), 20: (26, 58), 21: (31, 53), 22: (30, 52), 23: (30, 52),
          24: (31, 52), 25: (31, 54), 26: (30, 52), 27: (30, 52), 28: (31, 51), 29: (34, 51)}
REF_AXIS = 40                         # the '|' suture in ref_front.txt
AXIS = 74                             # sprite column the suture lands on
TOP = 13                              # sprite row of the first pasted row
CLEAR = (TOP, 44, 45, 105)            # rows / cols of the old face to blank
STRETCH = {22, 26, 27, 32, 33}        # mostly-vertical snout rows, shown twice

def build():
    ref = open(P.ART / "ref_front.txt").read().splitlines()
    rows = open(P.ART / "view_front_a.txt").read().splitlines()
    face = []
    for r in REF_ROWS:
        lo, hi = BOUNDS.get(r, (36, 50))
        src = ref[r].ljust(REF_COLS[1])
        line = "".join(ch if lo <= c < hi else " " for c, ch in enumerate(src))[REF_COLS[0]:REF_COLS[1]]
        face.append(line)
        if r in STRETCH: face.append(line)
    w = max(max(len(r) for r in rows), CLEAR[3])
    rows += [""] * max(0, TOP + len(face) - len(rows))
    rows = [r.ljust(w) for r in rows]
    for i in range(CLEAR[0], min(CLEAR[1], len(rows))):
        rows[i] = rows[i][:CLEAR[2]] + " " * (CLEAR[3] - CLEAR[2]) + rows[i][CLEAR[3]:]
    c0 = AXIS - (REF_AXIS - REF_COLS[0])
    for i, line in enumerate(face):
        r = rows[TOP + i]
        merged = "".join(n if n != " " else o for n, o in zip(line, r[c0:c0 + len(line)]))
        rows[TOP + i] = r[:c0] + merged + r[c0 + len(line):]
    return [r.rstrip() for r in rows]

if __name__ == "__main__":
    v = build()
    open(P.ART / "view_front.txt", "w").write("\n".join(v) + "\n")
    render(v, str(P.BUILD / "view_front.png"))
    print(max(len(r) for r in v), "x", len(v))
