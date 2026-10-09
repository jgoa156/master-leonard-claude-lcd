# Animated mocks of every mood, rendered by impure_panel.exe itself.
#   python mock_states.py -> mock_<state>.gif (centre pose, 2 s) + mock_all.png
import os, subprocess, tempfile
from PIL import Image

import paths as P
EXE = str(P.EXE)
STATES = ["idle", "thinking", "speaking", "happy", "alert", "sleepy"]
tmp = tempfile.mkdtemp()

def frame(state, d, ms):
    p = os.path.join(tmp, "f.ppm")
    subprocess.run([EXE, "--dump", state, str(d), p, str(ms)], check=True)
    return Image.open(p).convert("RGB").copy()

sheet = Image.new("RGB", (480 * 3 + 40, 330 * 2 + 10), (40, 40, 40))
for i, st in enumerate(STATES):
    frames = [frame(st, 0, ms).resize((960, 640), Image.NEAREST) for ms in range(0, 2000, 80)]
    frames[0].save(str(P.DOCS / f"mock_{st}.gif"), save_all=True, append_images=frames[1:], duration=80, loop=0)
    sheet.paste(frame(st, 0, 0), (10 + (i % 3) * 490, 10 + (i // 3) * 330))
sheet.save(str(P.DOCS / "mock_all.png"))
print("ok")
