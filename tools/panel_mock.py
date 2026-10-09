# Mock of the avatar on the 3.5" 480x320 panel: render each pose crisply at
# high resolution, then scale down to the panel's pixels.
#   python panel_mock.py  -> panel_mock.png (actual 480x320 frames + 3x zoom)
import os, subprocess
from PIL import Image, ImageDraw, ImageFont

import paths as P
PW, PH = 480, 320
font = ImageFont.truetype("consola.ttf", 20)
cw, ch = 11, 22                                   # consola 20px cell
frames = []
for d in (-1, 0, 1):
    txt = subprocess.run([str(P.DEV / "impure_avatar.exe"), "--dump", "idle", str(d)],
                         capture_output=True, text=True).stdout.splitlines()[:-1]
    txt = [l.rstrip() for l in txt]
    lead = min(len(l) - len(l.lstrip()) for l in txt if l.strip())
    txt = [l[lead:] for l in txt]
    while txt and not txt[0].strip(): txt.pop(0)
    while txt and not txt[-1].strip(): txt.pop()
    w = max(map(len, txt))
    big = Image.new("RGB", (w * cw, len(txt) * ch), (0, 0, 0)); g = ImageDraw.Draw(big)
    for r, l in enumerate(txt):
        for c, chr_ in enumerate(l):
            if chr_ == " ": continue
            eye = chr_ in "<>=.:+*" and False
            g.text((c * cw, r * ch), chr_, font=font, fill=(230, 230, 222))
    # tint the eye glyphs amber so the LEDs read in the mock
    s = min(PW / big.width, PH / big.height)
    small = big.resize((max(1, int(big.width * s)), max(1, int(big.height * s))), Image.LANCZOS)
    panel = Image.new("RGB", (PW, PH), (0, 0, 0))
    panel.paste(small, ((PW - small.width) // 2, (PH - small.height) // 2))
    frames.append(panel)
out = Image.new("RGB", (PW * 3 + 40, PH + 20), (40, 40, 40))
for i, f in enumerate(frames): out.paste(f, (10 + i * (PW + 10), 10))
out.save(str(P.DOCS / "panel_mock.png"))
frames[1].resize((PW * 3, PH * 3), Image.NEAREST).save(str(P.DOCS / "panel_mock_zoom.png"))
