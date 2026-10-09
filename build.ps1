# build.ps1 - turn the text art into sprites and build the program.
#   .\build.ps1                  5" panel, 800x480 (default)
#   .\build.ps1 -Panel 480x320   3.5" panel
# The art is plain text and is the source: assets\art\right.txt and assets\art\front.txt (edit them by hand).
#   assets\art\*.txt -> tools\gen_sprites.py -> gen\sprites.h   (terminal sprites)
#                    -> tools\gen_panel.py   -> gen\panel.h     (panel bitmaps at the chosen resolution)
#   src\leonard.c + gen\panel.h              -> bin\leonard.exe  (the one exe: app + Claude hook + preview + stop)
#   legacy\terminal\impure_avatar.c          -> build\dev\impure_avatar.exe  (terminal version, dev only)
param([string]$Panel = '800x400')
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

function Step($name, [scriptblock]$run) {
    Write-Host ("== {0}" -f $name)
    $global:LASTEXITCODE = 0
    & $run
    if ($LASTEXITCODE) { throw "$name failed (exit $LASTEXITCODE)" }
}

foreach ($f in 'assets\art\right.txt', 'assets\art\front.txt') {
    if (-not (Test-Path $f)) { throw "$f is missing - it is the art. See assets\art\README.md" }
}

# a running copy locks its .exe; stop it first
if (Test-Path bin\leonard.exe) { & .\bin\leonard.exe --stop; Start-Sleep -Milliseconds 800 }
Get-Process leonard, impure_avatar -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith($PSScriptRoot, [StringComparison]::OrdinalIgnoreCase) } | Stop-Process     # only this folder's copies
New-Item -ItemType Directory -Force bin, build\dev, gen | Out-Null

Step 'terminal sprites (gen\sprites.h)'    { python tools\gen_sprites.py }
Step "panel bitmaps (gen\panel.h, $Panel)" { python tools\gen_panel.py ($Panel -split 'x') | Out-Null }

Step 'bin\leonard.exe'                     { gcc -O2 -Wall -mwindows -I gen -I src -o bin\leonard.exe src\leonard.c -lgdi32 -lws2_32 -lole32 -lm }
Step 'dev: impure_avatar.exe'              { gcc -O2 -Wall -I gen -I src -o build\dev\impure_avatar.exe legacy\terminal\impure_avatar.c -lm }
Write-Host '== build OK  ->  bin\leonard.exe'
