# assets/source

The art is **not** in this repository: the skull this project was built around is other artists' ASCII art
and is not redistributable. Put your own images here, then run `.\build.ps1`.

| File | What it is | Used for |
|---|---|---|
| `impure_ref.png` | a side-view ASCII skull, light text on black (this project's was 500x358, ~3.92 x 7.89 px per character) | the left / right poses |
| `reference.png` | a front-view ASCII skull, light text on black (this project's was 1000x1000, ~12.05 x 23.11 px per character) | the centre pose's face |

The tools measure each image's character grid and transcribe it cell by cell (`tools\gen_impure.py`,
`tools\transcribe.py`). If your images use a different character size, change the pitch values in `build.ps1`
and the eye positions in `tools\gen_sprites.py`. Any ASCII art with a clear eye socket will work.
