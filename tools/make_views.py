# Build view variants from impure.txt (the transcribed base art).
#   python make_views.py
# Writes view_left.txt (whole art mirrored), view_front_a.txt (left half mirrored
# onto the right) and view_front_b.txt (right half mirrored onto the left), plus
# PNG previews of each, rendered like the terminal.
import os
from PIL import Image, ImageDraw, ImageFont

import paths as P
SWAP = {"/": "\\", "\\": "/", "(": ")", ")": "(", "[": "]", "]": "[", "<": ">", ">": "<",
        "`": "'", "L": "J", "J": "L", "{": "}", "}": "{", "7": "r", "r": "7"}
TAG = "impure"

def mirror(s):
    return "".join(SWAP.get(c, c) for c in reversed(s))

def load():
    rows = open(P.ART / "impure.txt").read().splitlines()
    tag_row = next(i for i, r in enumerate(rows) if "i      m" in r)
    rows[tag_row] = rows[tag_row][:rows[tag_row].index("i      m")].rstrip()   # art only; tag is placed separately
    w = max(len(r) for r in rows)
    return [r.ljust(w) for r in rows], tag_row

def tidy(rows):
    rows = [r.rstrip() for r in rows]
    while rows and not rows[-1].strip(): rows.pop()
    while rows and not rows[0].strip(): rows.pop(0)
    lead = min(len(r) - len(r.lstrip()) for r in rows if r.strip())
    return [r[lead:] for r in rows]

def front(rows, axis, keep_left):
    out = []
    for r in rows:
        half = r[:axis] if keep_left else r[axis:]
        out.append(half + mirror(half) if keep_left else mirror(half) + half)
    return tidy(out)

def render(rows, path):
    font = ImageFont.truetype("consola.ttf", 16)
    w = max(len(r) for r in rows)
    img = Image.new("RGB", (w * 9 + 20, len(rows) * 18 + 20), (12, 12, 12))
    d = ImageDraw.Draw(img)
    for i, r in enumerate(rows):
        d.text((10, 10 + i * 18), r, font=font, fill=(225, 225, 225))
    img.save(path)

if __name__ == "__main__":
    rows, tag_row = load()
    views = {
        "front_a": front(rows, 75, True),
    }
    for name, v in views.items():
        open(P.ART / f"view_{name}.txt", "w").write("\n".join(v) + "\n")
        render(v, str(P.BUILD / f"view_{name}.png"))
        print(name, max(len(r) for r in v), "x", len(v))
