# Render impure.exe --plain at the source's native cell size, beside a contrast-boosted reference.
import os, subprocess
from PIL import Image, ImageDraw, ImageFont, ImageOps
import paths as P
PX, PY, K = 3.92, 7.89, 4
ox, oy = map(float, open(P.ART / "impure_grid.txt").read().split())
txt = subprocess.run([str(P.DEV / "impure.exe"), "--plain"], capture_output=True, text=True).stdout.splitlines()
ref = ImageOps.autocontrast(Image.open(P.SOURCE / "impure_ref.png").convert("L"), cutoff=0.3)
W, H = ref.size
font = ImageFont.truetype("consola.ttf", int(round(PY * K) * 0.92))
img = Image.new("L", (W * K, H * K)); d = ImageDraw.Draw(img)
asc = font.getbbox("Mg")[1] - 2
for r, l in enumerate(txt):
    for c, ch in enumerate(l):
        if ch != " ": d.text(((ox + c * PX) * K, (oy + r * PY) * K - asc), ch, font=font, fill=255)
img = img.resize((W, H), Image.LANCZOS)
out = Image.new("L", (W * 2, H)); out.paste(ref, (0, 0)); out.paste(img, (W, 0))
out.resize((W * 4, H * 2), Image.LANCZOS).save(P.DOCS / "impure_compare.png")
# full-size detail crop of the face for close inspection
det = Image.new("L", (260, 140)); det.paste(ref.crop((200, 120, 330, 260)), (0, 0)); det.paste(img.crop((200, 120, 330, 260)), (130, 0))
det.resize((1040, 560), Image.NEAREST).save(P.DOCS / "impure_detail.png")
