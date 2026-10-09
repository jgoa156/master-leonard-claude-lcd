# assets/art

The avatar's art, as **plain text**. This is the source: edit these files by hand, then run `.\build.ps1`.

| File | What it is |
|---|---|
| `right.txt` | the head turned to the right (the left-facing pose is generated as its mirror image) |
| `front.txt` | the head facing the front |

Where the eyes sit is set in `tools\gen_sprites.py` (`EYE_R` and `EYES`: socket centre and half-size, in characters).
The art itself is not in this repository (it is other artists' work and not redistributable): supply your own.
Any ASCII art works. Make `right.txt` ~110 characters wide and `front.txt` ~148, about 44 lines tall, with an empty eye
socket where the LEDs should glow.
