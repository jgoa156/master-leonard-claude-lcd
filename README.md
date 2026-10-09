# Master Leonard

A ram-skull avatar for a small USB panel (5" 800x480, or 3.5" 480x320). Its eyes show what your Claude Code
sessions are doing, and its head follows your mouse. One exe does everything: `bin\leonard.exe`.

> **About the art.** The skull is plain text in `assets/art` (`right.txt`, `front.txt`) and `.\build.ps1` turns it into the
> sprites. It was transcribed from two ASCII-art pieces by other artists (tagged "impure" and "lbs 12-21") and is **their
> work, not covered by this repository's code licence** - see the credit in `assets/art/README.md`. Swap in your own
> art by replacing those two files.
> Needs Windows, a C compiler (gcc / MinGW) and Python 3 with Pillow and numpy.

## Moods

| Mood | What you see | When |
|---|---|---|
| **dead** | eyes are red crosses, head always centred | out of credits / usage limit / account on hold |
| **alert** | the whole goat blinks white <-> red every 0.5 s | a session needs you (permission / input), or another API error |
| **happy** | green eyes | a run that used tools finished (a task) |
| **speaking** | pale yellow flicker | a plain chat answer finished |
| **thinking** | cyan eyes | a session is working |
| **idle** | amber eyes | sessions open, quiet |
| **sleepy** | dim amber line | no Claude session open |

With several sessions, the highest in this order is shown (the table is `PRIORITY` in `src\avatar.h`):
**dead > alert > happy > speaking > thinking > idle > sleepy**. A finished task or answer shows over sessions that are
still working; happy / speaking last about 6 s, then that session counts as idle. A "thinking" session that goes
silent for 2 minutes (Esc, closed window, crash: Claude Code has no hook for those) settles to idle. Dead clears when
that session sends its next event, i.e. when credits are back.

**Audio ghosts.** Behind the white skull, a **cyan** copy glows with the **left** audio channel and a **#dc4583** copy with the **right** one: louder = brighter and further out, silence = no ghosts. The level is Windows' own output meter for the default speakers / headphones (what's playing; no microphone, no recording). `--no-audio` turns it off; `leonard.exe --meter 3` records the raw left / right levels for 3 s into `meter.txt`.

**The head follows the mouse.** The primary monitor is cut into three vertical slices; the head looks left, centre or
right depending on where the cursor is (a cursor on another monitor counts as left / right of the primary one).
Dead is always centred. `leonard.exe --no-follow` turns this off (the head then glances around by mood).

## Use

```powershell
.\build.ps1                 # text art -> sprites -> bin\leonard.exe   (.\build.ps1 -Panel 480x320 for the 3.5")
.\install.ps1               # startup shortcut + Claude Code hooks (run once; -Uninstall to remove)
.\bin\leonard.exe           # or just double-click it: shows the window (a copy already running shows its own)
.\bin\leonard.exe --stop    # stop the running copy
.\tests\test.ps1 -Real -Audio  # full test (-Real: two short headless Claude sessions; -Audio: two quiet test tones)
```

`leonard.exe` modes: *(none)* show the window · `--background` hidden, no window (the startup shortcut uses it) · `--hook` the Claude Code hook (reads JSON on stdin, sends one UDP
packet, exits in ~6 ms) · `--set <mood> [id]` set a mood from a script, no Claude needed (`id` defaults to `cli`; unknown mood -> exit 2) · `--stop` · `--stale N` · `--no-follow` · `--no-audio` · `--meter N` · `--dump mood dir out.ppm [ms]`.

- Double-clicking (or `--preview`) while a copy is already running just brings **that** copy's window up, so it keeps its
  sessions. The window's X only **hides** it (the app keeps listening, e.g. after a login start); `q` / Esc in the window
  quit the app; keys 1-7 force a mood for 15 s (`a` = automatic again). The window is 12 x 7 cm on screen.
- `--hook` only acts when hook data is piped in (which is how Claude Code calls it). Typed by hand it is ignored, so
  `leonard.exe --hook --preview` simply opens the preview.

## Layout

```
bin/leonard.exe        the program (built, not committed)
src/                   leonard.c (app + hook + preview), avatar.h (moods, priority, cursor logic)
gen/                   generated headers: panel.h (the panel poses), sprites.h (not committed)
assets/art/            the art, plain text: right.txt, front.txt (not original: see its README for the credit)
tools/                 gen_sprites.py, gen_panel.py (art -> headers) and preview tools; paths.py knows the layout
tests/                 test.ps1 (behaviour), check_render.py (pixels)
legacy/terminal/       the terminal version of the avatar (dev tool)
build/                 scratch output - safe to delete
```

Runtime files: `%LOCALAPPDATA%\MasterLeonard\status.txt` (shown mood, where the head looks, each session's mood) and
`events.log` (every event received, for debugging).

## How it fits together

`Claude Code hook` -> `leonard.exe --hook` -> UDP 127.0.0.1:47474 -> `leonard.exe` (background) merges all
sessions -> picks the mood -> draws the frame (the panel size, 800x480 by default). **Sending that frame to the USB
panel is not written yet**: it goes at the marked spot in `main` (`src\leonard.c`) once the hardware is here
(Turing / TURZX serial protocol).

The hooks: `SessionStart UserPromptSubmit PreToolUse PostToolUse Notification Stop StopFailure SubagentStop
PreCompact SessionEnd`. `StopFailure` fires when a turn ends on an API error: `billing_error`, `rate_limit` and
`account_on_hold` make the goat dead; any other error is an alert.

Claude Code on Windows runs hooks through bash, so the hook path in `settings.json` uses forward slashes.
Hooks are read when a session starts.
