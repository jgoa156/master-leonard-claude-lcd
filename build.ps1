# build.ps1 - regenerate every derived file and rebuild the program.
#   .\build.ps1
# Art pipeline:  assets\source\impure_ref.png -> tools\gen_impure.py  -> assets\art\impure.txt   (side view)
#                                             -> tools\make_views.py  -> assets\art\view_front_a.txt (horns)
#                assets\source\reference.png  -> tools\transcribe.py  -> assets\art\ref_front.txt
#                                             -> tools\build_front.py -> assets\art\view_front.txt (centre)
#                assets\art\*                 -> tools\gen_sprites.py -> gen\sprites.h
#                                             -> tools\gen_panel.py   -> gen\panel.h (480x320 bitmaps)
# Programs:      bin\leonard.exe               the one exe: app + Claude hook + preview + stop
#                build\dev\*.exe               terminal versions, dev only
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

function Step($name, [scriptblock]$run) {
    Write-Host ("== {0}" -f $name)
    $global:LASTEXITCODE = 0
    & $run
    if ($LASTEXITCODE) { throw "$name failed (exit $LASTEXITCODE)" }
}

# a running copy locks its .exe; stop it first
if (Test-Path bin\leonard.exe) { & .\bin\leonard.exe --stop; Start-Sleep -Milliseconds 800 }
Get-Process leonard, impure_avatar -ErrorAction SilentlyContinue | Stop-Process
New-Item -ItemType Directory -Force bin, build\dev | Out-Null

Step 'transcribe impure_ref.png'    { python tools\gen_impure.py | Out-Null }
Step 'side + front-horn views'      { python tools\make_views.py | Out-Null }
Step 'transcribe reference.png'     { python tools\transcribe.py assets\source\reference.png 12.05 23.11 assets\art\ref_front.txt }
Step 'centre sprite'                { python tools\build_front.py }
Step 'terminal sprites (sprites.h)' { python tools\gen_sprites.py }
Step 'panel bitmaps (panel.h)'      { python tools\gen_panel.py | Out-Null }

Step 'bin\leonard.exe'              { gcc -O2 -Wall -mwindows -I gen -I src -o bin\leonard.exe src\leonard.c -lgdi32 -lws2_32 -lm }
Step 'dev: impure.exe'              { gcc -O2 -Wall -I gen -o build\dev\impure.exe gen\impure.c }
Step 'dev: impure_avatar.exe'       { gcc -O2 -Wall -I gen -I src -o build\dev\impure_avatar.exe legacy\terminal\impure_avatar.c -lm }
Write-Host '== build OK  ->  bin\leonard.exe'
