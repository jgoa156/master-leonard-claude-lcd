# assets/art

The avatar's art, as **plain text**. This is the source: edit these files by hand, then run `.\build.ps1`.

| File | What it is |
|---|---|
| `right.txt` | the head turned to the right (the left-facing pose is generated as its mirror image) |
| `front.txt` | the head facing the front |

Where the eyes sit is set in `tools\gen_sprites.py` (`EYE_R` and `EYES`: socket centre and half-size, in characters).
To use your own art instead, replace both files: any ASCII art works. Make `right.txt` ~110 characters wide and
`front.txt` ~148, about 44 lines tall, with an empty eye socket where the LEDs should glow.

## Credit

The skull is **not original to this project**. It was transcribed character by character from two ASCII-art pieces
by other artists: the side view carried the tag **"impure"**, the front view the signature **"lbs 12-21"**. That art
is their work and is not covered by this repository's code licence. If you made it and want it credited differently,
or removed, please open an issue and it will be taken care of.
