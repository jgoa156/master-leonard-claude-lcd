# Pixel-level check of impure_panel frames: every mood x pose.
#   python tests\check_render.py   -> prints PASS/FAIL per check, writes build\render_check.png
import os, subprocess, tempfile
from PIL import Image
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "bin", "leonard.exe")
tmp = tempfile.mkdtemp()
MOODS = {"idle": (255, 150, 0), "thinking": (51, 214, 255), "speaking": (255, 236, 150),
         "happy": (93, 255, 122), "alert": (255, 30, 30), "sleepy": (150, 100, 20)}
bad = 0

def frame(mood, d, ms=0):
    p = os.path.join(tmp, "f.ppm")
    subprocess.run([EXE, "--dump", mood, str(d), p, str(ms)], check=True)
    return np.asarray(Image.open(p).convert("RGB"), np.float32)

def check(name, ok, detail=""):
    global bad
    print(("[PASS] " if ok else "[FAIL] ") + name + (f"  ({detail})" if detail else ""))
    bad += not ok

def tint(img):
    """coloured (non-grey) pixels: the eye LEDs"""
    sat = img.max(2) - img.min(2)
    m = sat > 25
    return m, (img[m].mean(0) if m.any() else np.zeros(3))

sheet = Image.new("RGB", (480 * 3 + 40, 320 * 6 + 70), (40, 40, 40))
for r, (mood, col) in enumerate(MOODS.items()):
    for c, d in enumerate((-1, 0, 1)):
        img = frame(mood, d, 100)
        sheet.paste(Image.fromarray(img.astype(np.uint8)), (10 + c * 490, 10 + r * 330))
        m, mean = tint(img)
        n = int(m.sum())
        # hue check: the lit pixels should lean toward the mood colour's dominant channel
        want = int(np.argmax(col)); got = int(np.argmax(mean)) if n else -1
        ok = n >= 6 and (want == got or mood in ("idle", "speaking", "sleepy") and got in (0, 1))
        check(f"{mood:9s} pose {d:+d}: eyes lit", ok, f"{n} px, mean RGB {mean.round().astype(int).tolist()}")
        ys, xs = np.nonzero(m)
        if n: check(f"{mood:9s} pose {d:+d}: eyes inside skull area", 120 < xs.mean() < 360 and 60 < ys.mean() < 260,
                    f"centre ({xs.mean():.0f},{ys.mean():.0f})")

# alert: negative blink every 0.5 s, black & white
a0, a1, a2 = frame("alert", 0, 100), frame("alert", 0, 600), frame("alert", 0, 1100)
check("alert normal phase is dark", a0[:20, :20].mean() < 30, f"corner {a0[:20,:20].mean():.0f}")
check("alert negative phase is bright", a1[:20, :20].mean() > 225, f"corner {a1[:20,:20].mean():.0f}")
check("alert negative phase is black & white", float((a1.max(2) - a1.min(2)).max()) < 3)
check("alert flips back 0.5 s later", a2[:20, :20].mean() < 30)
check("alert negative = inverted skull", abs(float((255 - a1[::4, ::4].mean(2)).mean() - a0[::4, ::4].mean(2).mean())) < 12)
os.makedirs(os.path.join(ROOT, "build"), exist_ok=True)
sheet.save(os.path.join(ROOT, "build", "render_check.png"))
print("== render check:", "ALL PASSED" if not bad else f"{bad} FAILED")
raise SystemExit(bad)
