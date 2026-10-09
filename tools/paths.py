# paths.py - the one place that knows the project layout. Every tool imports this.
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "assets" / "source"      # the source images (impure_ref.png, reference.png)
ART = ROOT / "assets" / "art"            # transcribed ASCII art (generated, but kept: it is the approved art)
GEN = ROOT / "gen"                       # generated C headers (panel.h, sprites.h, impure.c)
BIN = ROOT / "bin"                       # the finished program: leonard.exe
DEV = ROOT / "build" / "dev"             # dev-only exes (terminal versions)
BUILD = ROOT / "build"                   # scratch output, safe to delete
DOCS = ROOT / "docs" / "previews"        # mockups and preview images
EXE = BIN / "leonard.exe"

for d in (ART, GEN, BIN, DEV, BUILD, DOCS):
    d.mkdir(parents=True, exist_ok=True)
