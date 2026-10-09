# Render impure_avatar.exe --dump frames (pose x state) into avatar_preview.png
#   python preview_avatar.py [state ...]
import os, sys, subprocess
from PIL import Image, ImageDraw, ImageFont

import paths as P
COL = {"idle": (255, 150, 0), "thinking": (51, 214, 255), "speaking": (255, 236, 150),
       "happy": (93, 255, 122), "alert": (255, 30, 30), "sleepy": (150, 100, 20)}
EYE = set(".:+*<>=-/\\\"")
states = sys.argv[1:] or ["idle"]
font = ImageFont.truetype("consola.ttf", 12)
tiles = []
for st in states:
    for d in (-1, 0, 1):
        txt = subprocess.run([str(P.DEV / "impure_avatar.exe"), "--dump", st, str(d)],
                             capture_output=True, text=True).stdout.splitlines()
        ref = subprocess.run([str(P.DEV / "impure_avatar.exe"), "--dump", "sleepy", str(d)],
                             capture_output=True, text=True).stdout.splitlines() if False else None
        img = Image.new("RGB", (150 * 7 + 10, 47 * 14 + 10), (12, 12, 12)); dr = ImageDraw.Draw(img)
        for r, l in enumerate(txt):
            dr.text((5, 5 + r * 14), l, font=font, fill=(220, 220, 220))
        dr.text((5, 5), f"{st} dir={d}", font=font, fill=COL[st])
        tiles.append(img)
w, h = tiles[0].size
out = Image.new("RGB", (w * 3, h * len(states)))
for i, t in enumerate(tiles): out.paste(t, ((i % 3) * w, (i // 3) * h))
out.save(str(P.DOCS / "avatar_preview.png"))
