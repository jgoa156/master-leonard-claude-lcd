# Generic ASCII-art image transcriber (same method as gen_impure.py).
#   python transcribe.py <image> <pitch_x> <pitch_y> <out.txt> [stretch_y]
# Every cell is upscaled, contrast-stretched and matched against the glyph set
# with a small position jitter; the grid origin is fitted first. stretch_y > 1
# stretches the image vertically before reading, giving a taller transcript.
import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageOps, ImageFilter

CHARS = list(" .,:;'`-_~^\"|/\\()[]<>=+*!YVLJjlvirXAbdsy7")

def norm(a):
    a = a - a.mean(); n = np.linalg.norm(a); return a / n if n > 1e-6 else a

def load(path, PX, PY, stretch=1.0, K=None):
    K = K or max(1, round(32 / PY))
    TW, TH = round(PX * K), round(PY * K)
    font = ImageFont.truetype("consola.ttf", int(TH * 0.92))
    top = font.getbbox("Mg")[1] - 2
    T = []
    for ch in CHARS:
        im = Image.new("L", (TW * 3, TH * 2)); d = ImageDraw.Draw(im)
        d.text((TW, TH // 2), ch, font=font, fill=255)
        adv = font.getlength(ch) or TW
        box = im.crop((TW, TH // 2 + top, TW + int(adv), TH // 2 + top + TH))
        T.append(norm(np.asarray(box.resize((TW, TH), Image.BILINEAR).filter(ImageFilter.GaussianBlur(1.2)), np.float32) / 255))
    src = ImageOps.autocontrast(Image.open(path).convert("L"), cutoff=0.3)
    W, H = src.size
    H = round(H * stretch)
    big = np.asarray(src.resize((round(W * K), round(H * K)), Image.BICUBIC).filter(ImageFilter.GaussianBlur(1.2)), np.float32) / 255
    return dict(T=np.stack(T), big=big, K=K, TW=TW, TH=TH, W=W, H=H, PX=PX, PY=PY)

def transcribe(S, ox, oy, jit=2):
    T, big, K, TW, TH, PX, PY = S["T"], S["big"], S["K"], S["TW"], S["TH"], S["PX"], S["PY"]
    cols, rows = int((S["W"] - ox) / PX), int((S["H"] - oy) / PY)
    out, score = [], 0.0
    for r in range(rows):
        line = ""
        for c in range(cols):
            x0, y0 = round((ox + c * PX) * K), round((oy + r * PY) * K)
            cell = big[y0:y0 + TH, x0:x0 + TW]
            if cell.shape != (TH, TW) or cell.max() < 0.25:
                line += " "; continue
            best, bs = " ", 0.0
            for dy in range(-jit, jit + 1):
                for dx in range(-jit, jit + 1):
                    p = big[max(0, y0 + dy):y0 + dy + TH, max(0, x0 + dx):x0 + dx + TW]
                    if p.shape != (TH, TW): continue
                    s = (T * norm(p)).sum((1, 2)); g = int(s.argmax())
                    if s[g] > bs: bs, best = s[g], CHARS[g]
            if bs < 0.35: best = " "
            line += best; score += bs
        out.append(line.rstrip())
    return out, score

def fit(S):
    best = None
    for ox in np.arange(0, S["PX"], S["PX"] / 4):
        for oy in np.arange(0, S["PY"], S["PY"] / 4):
            _, s = transcribe(S, ox, oy, jit=0)
            if not best or s > best[0]: best = (s, ox, oy)
    return best[1], best[2]

if __name__ == "__main__":
    path, PX, PY, out = sys.argv[1], float(sys.argv[2]), float(sys.argv[3]), sys.argv[4]
    stretch = float(sys.argv[5]) if len(sys.argv) > 5 else 1.0
    S = load(path, PX, PY * 1.0, stretch)
    ox, oy = fit(S)
    rows, _ = transcribe(S, ox, oy)
    open(out, "w").write("\n".join(rows) + "\n")
    print(f"origin {ox:.2f},{oy:.2f}  {max(map(len, rows))}x{len(rows)}", file=sys.stderr)
