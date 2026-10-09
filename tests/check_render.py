# Pixel-level check of leonard.exe frames: every mood x pose, the alert blink and the dead goat.
#   python tests\check_render.py   -> prints PASS/FAIL per check, writes build\render_check.png
import os, subprocess, tempfile
from PIL import Image
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "bin", "leonard.exe")
tmp = tempfile.mkdtemp()
MOODS = {"idle": (255, 150, 0), "thinking": (51, 214, 255), "speaking": (255, 236, 150),
         "happy": (93, 255, 122), "alert": (255, 30, 30), "sleepy": (150, 100, 20), "dead": (210, 20, 20)}
bad = 0

def frame(mood, d, ms=0, lev=None, sessions=None):
    p = os.path.join(tmp, "f.ppm")
    extra = [str(lev[0]), str(lev[1])] if lev else (["0", "0"] if sessions is not None else [])
    if sessions is not None: extra.append(str(sessions))
    subprocess.run([EXE, "--dump", mood, str(d), p, str(ms)] + extra, check=True)
    return np.asarray(Image.open(p).convert("RGB"), np.float32)

def check(name, ok, detail=""):
    global bad
    print(("[PASS] " if ok else "[FAIL] ") + name + (f"  ({detail})" if detail else ""))
    bad += not ok

def tint(img):
    """coloured (non-grey) pixels: the eye LEDs"""
    m = (img.max(2) - img.min(2)) > 25
    return m, (img[m].mean(0) if m.any() else np.zeros(3))

H, W = frame("idle", 0).shape[:2]
print(f"panel {W}x{H}")
sheet = Image.new("RGB", (W * 3 + 40, (H + 10) * len(MOODS) + 10), (40, 40, 40))
for r, (mood, col) in enumerate(MOODS.items()):
    for c, d in enumerate((-1, 0, 1)):
        img = frame(mood, d, 100)
        sheet.paste(Image.fromarray(img.astype(np.uint8)), (10 + c * (W + 10), 10 + r * (H + 10)))
        m, mean = tint(img); n = int(m.sum())
        want = int(np.argmax(col)); got = int(np.argmax(mean)) if n else -1
        ok = n >= 6 and (want == got or mood in ("idle", "speaking", "sleepy") and got in (0, 1))
        check(f"{mood:9s} pose {d:+d}: eyes lit", ok, f"{n} px, mean RGB {mean.round().astype(int).tolist()}")
        ys, xs = np.nonzero(m)
        if n: check(f"{mood:9s} pose {d:+d}: eyes inside skull area", 0.25 * W < xs.mean() < 0.75 * W and 0.19 * H < ys.mean() < 0.81 * H,
                    f"centre ({xs.mean():.0f},{ys.mean():.0f})")

# alert: the GOAT blinks white <-> red every 0.5 s; the background stays black (no screen negative)
def goat(img):                                   # the bright stroke pixels, minus the eye glow
    return img[(img.max(2) > 150)]
a0, a1, a2 = frame("alert", 0, 100), frame("alert", 0, 600), frame("alert", 0, 1100)
w0, r1 = goat(a0), goat(a1)
check("alert phase 1: goat is white", len(w0) > 500 and float(w0[:, 0].mean() - w0[:, 1].mean()) < 40, f"{len(w0)} px, mean RGB {w0.mean(0).round().astype(int).tolist()}")
check("alert phase 2: goat is red (the mock's red)", len(r1) > 100 and float(r1[:, 0].mean()) > 150 and float(r1[:, 0].mean()) > 8 * float(r1[:, 1].mean()) and float(r1[:, 0].mean()) > 6 * float(r1[:, 2].mean()),
      f"{len(r1)} px, mean RGB {r1.mean(0).round().astype(int).tolist()}")
check("alert background stays black in both phases", a0[:20, :20].mean() < 10 and a1[:20, :20].mean() < 10, f"corners {a0[:20,:20].mean():.0f} / {a1[:20,:20].mean():.0f}")
check("alert flips back to white 0.5 s later", float(goat(a2)[:, 1].mean()) > 150)
check("alert is NOT a screen negative", a1[:20, :20].mean() < 10 and a1.mean() < a0.mean() + 5)

# dead: red crosses for eyes, always centred
d_l, d_c, d_r = frame("dead", -1, 0), frame("dead", 0, 0), frame("dead", 1, 0)
check("dead ignores 'look left' (always centred)", bool((d_l == d_c).all()))
check("dead ignores 'look right' (always centred)", bool((d_r == d_c).all()))
m, mean = tint(d_c)
check("dead eyes are red", int(m.sum()) > 100 and mean[0] > 2.5 * max(mean[1], mean[2]), f"{int(m.sum())} px, mean RGB {mean.round().astype(int).tolist()}")
i_c = frame("idle", 0, 0)
check("dead differs from idle (crosses, not lenses)", float(np.abs(d_c - i_c).sum()) > 5000)
# an X is symmetric left-right and top-bottom inside each eye: check one eye's tinted pixels for a cross pattern
ys, xs = np.nonzero(m); left = xs < W / 2
ex, ey = xs[left], ys[left]
if len(ex) > 20:
    sub = np.zeros((int(ey.max() - ey.min() + 1), int(ex.max() - ex.min() + 1)), bool); sub[ey - ey.min(), ex - ex.min()] = True
    h, w = sub.shape; q = [sub[:h // 2, :w // 2].sum(), sub[:h // 2, w // 2:].sum(), sub[h // 2:, :w // 2].sum(), sub[h // 2:, w // 2:].sum()]
    check("dead eye has a cross shape (all four quadrants lit)", min(q) > 0.6 * max(q) and min(q) > 5, f"quadrants {[int(x) for x in q]}")
# audio ghosts: cyan = left channel, #dc4583 = right channel, nothing in silence
def ghosts(img, eyes):                           # tinted pixels that are not the eye LEDs
    m = ((img.max(2) - img.min(2)) > 25) & ~eyes
    return img[m]
_, eyes_mask = None, tint(frame("idle", 0, 0))[0]
s0, sl, sr = frame("idle", 0, 0, (0, 0)), frame("idle", 0, 0, (1, 0)), frame("idle", 0, 0, (0, 1))
g0, gl, gr = ghosts(s0, eyes_mask), ghosts(sl, eyes_mask), ghosts(sr, eyes_mask)
check("silence: no ghosts", len(g0) < 50, f"{len(g0)} tinted px")
check("left channel: a cyan ghost", len(gl) > 2000 and gl[:, 0].mean() < 0.35 * gl[:, 1].mean() and abs(gl[:, 1].mean() - gl[:, 2].mean()) < 25,
      f"{len(gl)} px, mean RGB {gl.mean(0).round().astype(int).tolist()}")
want = np.array([220, 69, 131], np.float32); got = gr.mean(0) if len(gr) else np.zeros(3)
ratio_ok = len(gr) > 2000 and abs(got[0] / got[2] - want[0] / want[2]) < 0.35 and abs(got[2] / got[1] - want[2] / want[1]) < 0.5
check("right channel: a #dc4583 ghost", ratio_ok, f"{len(gr)} px, mean RGB {got.round().astype(int).tolist()} (hue of 220,69,131)")
lx = np.nonzero(((sl.max(2) - sl.min(2)) > 25) & ~eyes_mask)[1].mean(); rx = np.nonzero(((sr.max(2) - sr.min(2)) > 25) & ~eyes_mask)[1].mean()
check("cyan sits left of the skull, #dc4583 right of it", lx < rx, f"ghost centres x {lx:.0f} / {rx:.0f}")
# session tally: ASCII '|' per session, four + a strike for the fifth, centred in the free strip at the bottom
def frame7(mood, d, ms, lev, sessions, glitch):
    p = os.path.join(tmp, "f.ppm")
    subprocess.run([EXE, "--dump", mood, str(d), p, str(ms), str(lev[0]), str(lev[1]), str(sessions), str(glitch)], check=True)
    return np.asarray(Image.open(p).convert("RGB"), np.float32)
base = frame("idle", 0, 0, (0, 0), 0)
def lit(n, d=0):
    return (np.abs(frame("idle", d, 0, (0, 0), n) - base).max(2) > 40)
def runs(line):
    return int(((line[1:] & ~line[:-1]).sum()) + (1 if line[0] else 0))
check("the bottom strip is free for the tally (no goat there)", float(base[int(0.915 * H):].max()) < 30, f"brightest {float(base[int(0.915 * H):].max()):.0f}")
check("0 sessions: no tally", int(lit(0).sum()) == 0)
ys4, xs4 = np.nonzero(lit(4)); row = (ys4.min() + ys4.max()) // 2
for n in (1, 2, 3, 4):
    m = lit(n)
    check(f"{n} session(s): {n} ASCII slash(es)", runs(m[row]) == n, f"{runs(m[row])} on the middle row")
check("tally sits in the bottom strip", ys4.min() > 0.85 * H, f"top of the tally at y={ys4.min()} of {H}")
check("tally is small (under 8% of the frame height)", (ys4.max() - ys4.min()) < 0.08 * H, f"{ys4.max() - ys4.min() + 1} px tall")
for n in (1, 4, 5, 6, 13):
    for d in (-1, 0, 1):
        ys, xs = np.nonzero(lit(n, d)); cx = (xs.min() + xs.max()) / 2
        check(f"{n:2d} sessions, pose {d:+d}: tally centred", abs(cx - W / 2) <= 6, f"centre x {cx:.0f} (frame centre {W/2:.0f})")
m4, m5, m6 = lit(4), lit(5), lit(6)
w = lambda m: np.nonzero(m)[1].max() - np.nonzero(m)[1].min()
check("5th session = a strike across the 4 slashes", int(m5.sum()) > int(m4.sum()) + 40 and w(m5) > w(m4) + 4, f"lit px {int(m4.sum())} -> {int(m5.sum())}, width {w(m4)} -> {w(m5)}")
check("6th session = a new group starts (wider again)", int(m6.sum()) > int(m5.sum()) and w(m6) > w(m5))
red = frame("alert", 0, 600, (0, 0), 3)[int(0.85 * H):]
check("the tally follows the alert blink colour (red phase)", float(red[..., 0].max()) > 150 and float(red[..., 1].max()) < 80)

# glitch: on a beat the ghosts jump and slices shear sideways; the skull itself never moves
n0, g0 = frame7("idle", 0, 0, (0, 0), 0, 0), frame7("idle", 0, 0, (0, 0), 0, 1)
check("glitch never moves the skull (silence: identical frames)", bool((n0 == g0).all()))
n7, g7 = frame7("idle", 0, 0, (0.7, 0.7), 0, 0), frame7("idle", 0, 0, (0.7, 0.7), 0, 1)
diff = int((np.abs(n7 - g7).max(2) > 30).sum())
check("on a beat the ghosts are displaced (glitch)", diff > 3000, f"{diff} px moved")

# the pulse: the more the channel is playing, the more the ghost glows (monotonic from silence to full)
def ghost_light(lv):
    g = np.clip(frame("idle", 0, 0, (lv, lv)) - s0, 0, None); return float(g.mean())
steps = [ghost_light(v) for v in (0, 0.15, 0.3, 0.5, 0.75, 1.0)]
check("ghost glow pulses with the level (silence < 15% < 30% < 50% < 75% < 100%)", all(b > a for a, b in zip(steps, steps[1:])) and steps[0] == 0, " < ".join(f"{x:.1f}" for x in steps))
os.makedirs(os.path.join(ROOT, "build"), exist_ok=True)
sheet.save(os.path.join(ROOT, "build", "render_check.png"))
print("== render check:", "ALL PASSED" if not bad else f"{bad} FAILED")
raise SystemExit(bad)
