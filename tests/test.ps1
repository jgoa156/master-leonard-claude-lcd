# test.ps1 - full test of Master Leonard (single exe: bin\leonard.exe).
#   .\tests\test.ps1 [-Real]
# -Real also runs two short headless Claude Code sessions (uses a few hundred tokens).
param([switch]$Real)
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
$exe = Join-Path $root 'bin\leonard.exe'
$rt  = Join-Path $env:LOCALAPPDATA 'MasterLeonard'
$status = Join-Path $rt 'status.txt'
$lnk = Join-Path ([Environment]::GetFolderPath('Startup')) 'Master Leonard.lnk'
$testDir = Join-Path $env:TEMP 'leonard_test'
# The hook sections run a private copy of the app on its own port (LEONARD_PORT / LEONARD_DIR), so the real
# Claude sessions on this machine, which report to the normal instance, cannot disturb the expected moods.
function Use-Isolated { $env:LEONARD_PORT = '47999'; $env:LEONARD_DIR = $testDir; $script:status = Join-Path $testDir 'status.txt' }
function Use-Normal   { $env:LEONARD_PORT = $null;   $env:LEONARD_DIR = $null;    $script:status = Join-Path $env:LOCALAPPDATA 'MasterLeonard\status.txt' }
$fail = 0
function Check($name, $ok, $detail = '') { "{0} {1}{2}" -f $(if ($ok) { '[PASS]' } else { '[FAIL]' }), $name, $(if ($detail) { "  ($detail)" }); if (-not $ok) { $script:fail++ } }
function Send-Hook($ev, $sid) { "{`"session_id`":`"$sid`",`"hook_event_name`":`"$ev`"}" | & $exe --hook }
function Status { Start-Sleep -Milliseconds 500; ((Get-Content $status -ErrorAction SilentlyContinue) | Select-Object -First 1) }
function Stop-App { if (Test-Path $exe) { & $exe --stop }; Start-Sleep -Milliseconds 800 }

"== layout"
Check 'single exe exists' (Test-Path $exe) ("{0:N0} KB" -f ((Get-Item $exe).Length / 1KB))
Check 'no stray files in the project root' (@(Get-ChildItem $root -File | Where-Object { $_.Name -notin 'build.ps1', 'install.ps1', 'README.md' }).Count -eq 0)

"== installation"
$cfg = Get-Content (Join-Path $env:USERPROFILE '.claude\settings.json') -Raw | ConvertFrom-Json
$expected = ($exe -replace '\\', '/') + ' --hook'
$events = 'SessionStart','UserPromptSubmit','PreToolUse','PostToolUse','Notification','Stop','SubagentStop','PreCompact','SessionEnd'
$wrong = @($events | Where-Object { -not ($cfg.hooks.$_ | ForEach-Object { $_.hooks } | Where-Object { $_.command -eq $expected }) })
Check 'settings.json has all 9 hooks -> leonard.exe --hook' ($wrong.Count -eq 0) ($wrong -join ',')
Check 'no leftover hooks from the old names' (-not ((Get-Content (Join-Path $env:USERPROFILE '.claude\settings.json') -Raw) -match 'phillip|leonard_hook'))
Check 'startup shortcut exists' (Test-Path $lnk)
if (Test-Path $lnk) { Check 'startup shortcut targets bin\leonard.exe' (((New-Object -ComObject WScript.Shell).CreateShortcut($lnk)).TargetPath -eq $exe) }

"== frames (every pose)"
foreach ($d in -1, 0, 1) {
  $ppm = Join-Path $env:TEMP 'lt.ppm'; if (Test-Path $ppm) { Remove-Item $ppm }
  Start-Process $exe -ArgumentList "--dump idle $d `"$ppm`"" -Wait          # GUI exe: PowerShell will not wait unless told
  Check "leonard.exe --dump pose $d" ((Test-Path $ppm) -and (Get-Item $ppm).Length -eq 15 + 480 * 320 * 3)
}
if (Test-Path build\dev\impure.exe) {
  $plain = (& .\build\dev\impure.exe --plain) -join "`n"
  $txt = ((Get-Content assets\art\impure.txt) | ForEach-Object { $_.TrimEnd() }) -join "`n"
  Check 'dev impure.exe prints the transcribed art' ($plain.TrimEnd() -eq $txt.TrimEnd())
}

"== background mode (as started at login)"
Stop-App
if (Test-Path $status) { Remove-Item $status }
Start-Process $lnk; Start-Sleep 2
$p = @(Get-Process leonard -ErrorAction SilentlyContinue)
Check 'running after the shortcut' ($p.Count -eq 1)
Check 'no visible window' ($p.Count -eq 1 -and $p[0].MainWindowHandle -eq 0)
Start-Process $exe -Wait
Check 'second launch exits (single instance)' (@(Get-Process leonard).Count -eq 1)

"== the hook (private instance)"
Use-Isolated; Stop-App
if (Test-Path $status) { Remove-Item $status }
Start-Process $exe; Start-Sleep 2
Check 'private instance is up' ((Get-Process leonard -ErrorAction SilentlyContinue).Count -eq 2)
$sw =[Diagnostics.Stopwatch]::StartNew(); Send-Hook SessionStart SPEED; $ms = $sw.ElapsedMilliseconds
Check 'hook is fast' ($ms -lt 150) "$ms ms"; Send-Hook SessionEnd SPEED
$bash = 'C:\Program Files\Git\bin\bash.exe'
if (Test-Path $bash) {                                   # Claude Code on this machine runs hooks through bash
  $hookcmd = ($exe -replace '\\', '/') + ' --hook'
  $jf = Join-Path $env:TEMP 'leonard_hook_in.json'
  '{"session_id":"BASH1","hook_event_name":"UserPromptSubmit"}' | Set-Content $jf -Encoding ascii
  & $bash -c ("cat '" + ($jf -replace '\\', '/') + "' | " + $hookcmd)
  Start-Sleep -Milliseconds 500
  Check 'hook works when run through bash (as Claude Code does)' ((Get-Content $status -Raw) -match 'BASH1 thinking'); Send-Hook SessionEnd BASH1
}
Send-Hook SessionEnd GARBAGE; "not json" | & $exe --hook
Check 'hook survives garbage input and exits 0' ($LASTEXITCODE -eq 0)

"== command line (private instance, run from a real console)"
Add-Type -Namespace W -Name U -MemberDefinition '[DllImport("user32.dll")] public static extern bool PostMessage(System.IntPtr h, uint m, System.IntPtr w, System.IntPtr l); [DllImport("user32.dll")] public static extern bool IsWindowVisible(System.IntPtr h);'
function Private-Procs { @(Get-Process leonard -ErrorAction SilentlyContinue | Where-Object { $_.Id -ne $loginPid }) }
$loginPid = (Get-Process leonard | Sort-Object StartTime | Select-Object -First 1).Id        # the login instance started above
Stop-App                                                  # stops the private instance only
Start-Process powershell -WindowStyle Hidden -ArgumentList '-NoProfile', '-Command', "& '$exe' --hook --preview"; Start-Sleep 3
Check 'leonard.exe --hook --preview typed in a console opens the preview' ((Private-Procs | Where-Object { $_.MainWindowTitle -like 'Master Leonard*' }).Count -eq 1)
Stop-App; Start-Process $exe; Start-Sleep 1.5
Start-Process powershell -WindowStyle Hidden -ArgumentList '-NoProfile', '-Command', "& '$exe' --preview"; Start-Sleep 3
Check '--preview takes over from a running hidden copy' ((Private-Procs | Where-Object { $_.MainWindowTitle -like 'Master Leonard*' }).Count -eq 1)
$pp = Private-Procs | Select-Object -First 1
[W.U]::PostMessage($pp.MainWindowHandle, 0x10, [IntPtr]0, [IntPtr]0) | Out-Null; Start-Sleep 1
$pp = Private-Procs | Select-Object -First 1
Check 'closing the window hides it; the app keeps running' ($pp -and -not [W.U]::IsWindowVisible($pp.MainWindowHandle))
Stop-App; Start-Process $exe; Start-Sleep 2               # hidden private instance for the sections below

"== moods from simulated hook events"
$seq = @(
  @('no sessions',               @(),                                              'sleepy'),
  @('session opens',             @(,@('SessionStart','T1')),                       'idle'),
  @('prompt',                    @(,@('UserPromptSubmit','T1')),                   'thinking'),
  @('tools running',             @(@('PreToolUse','T1'), @('PostToolUse','T1')),  'thinking'),
  @('task finished',             @(,@('Stop','T1')),                               'happy'),
  @('chat answer, no tools',     @(@('UserPromptSubmit','T1'), @('Stop','T1')),    'speaking'),
  @('needs permission',          @(,@('Notification','T1')),                       'alert'),
  @('2nd session busy too',      @(@('SessionStart','T2'), @('UserPromptSubmit','T2')), 'alert'),
  @('first closes',              @(,@('SessionEnd','T1')),                         'thinking'),
  @('all closed',                @(,@('SessionEnd','T2')),                         'sleepy'))
foreach ($s in $seq) { foreach ($e in $s[1]) { Send-Hook $e[0] $e[1] }; $st = Status; Check $s[0] ($st -like "$($s[2]) *") $st }
Send-Hook SessionStart T3; Send-Hook UserPromptSubmit T3; Send-Hook Stop T3; Start-Sleep 7
Check 'speaking fades to idle after ~6 s' ((Status) -like 'idle *')
Send-Hook SessionEnd T3

"== priority when sessions disagree (alert > happy > speaking > thinking > idle > sleepy)"
function Session($id, $kind) {                           # put a session into a known mood
  Send-Hook SessionStart $id
  switch ($kind) {
    'idle'     { }
    'thinking' { Send-Hook UserPromptSubmit $id }
    'speaking' { Send-Hook UserPromptSubmit $id; Send-Hook Stop $id }
    'happy'    { Send-Hook UserPromptSubmit $id; Send-Hook PreToolUse $id; Send-Hook Stop $id }
    'alert'    { Send-Hook UserPromptSubmit $id; Send-Hook Notification $id } } }
function End-All { foreach ($i in 'P1', 'P2', 'P3', 'P4', 'P5') { Send-Hook SessionEnd $i } }
Session P1 idle;     Session P2 thinking;                       Check 'thinking beats idle'              ((Status) -like 'thinking *'); End-All
Session P1 thinking; Session P2 speaking;                       Check 'speaking beats thinking'          ((Status) -like 'speaking *'); End-All
Session P1 thinking; Session P2 happy;                          Check 'happy beats thinking'             ((Status) -like 'happy *'); End-All
Session P1 speaking; Session P2 happy;                          Check 'happy beats speaking'             ((Status) -like 'happy *'); End-All
Session P1 thinking; Session P2 happy; Session P3 alert;        Check 'alert beats happy and thinking'   ((Status) -like 'alert *'); End-All
Session P1 alert;    Session P2 thinking; Session P3 idle;      Check 'alert stays on top of busy sessions' ((Status) -like 'alert *')
Send-Hook UserPromptSubmit P1                                   # the alerted session gets answered and works again
Check 'alert clears when that session continues' ((Status) -like 'thinking *'); End-All
Session P1 thinking; Session P2 happy; Start-Sleep 7
Check 'happy is momentary: back to thinking after ~6 s' ((Status) -like 'thinking *'); End-All
Check 'no sessions = sleepy, the bottom' ((Status) -like 'sleepy *')

"== interrupted session (no Stop hook exists for Esc)"
Stop-App; Start-Process $exe -ArgumentList '--stale', '3'; Start-Sleep 2      # still the private instance
Send-Hook SessionStart I1; Send-Hook UserPromptSubmit I1; Send-Hook PreToolUse I1
Check 'working session shows thinking' ((Status) -like 'thinking *')
Start-Sleep 4
Check 'silent thinking session settles to idle' ((Status) -like 'idle *')
Send-Hook PreToolUse I1
Check 'it wakes up on the next event' ((Status) -like 'thinking *')
Send-Hook SessionEnd I1
Stop-App; Use-Normal                                    # private instance gone; the login instance is still running
Check 'login instance still running, untouched' ((Get-Process leonard -ErrorAction SilentlyContinue).Count -eq 1)

if ($Real) {
  "== real Claude Code sessions (hooks from settings.json)"
  $sid1 = [guid]::NewGuid().ToString(); $sid2 = [guid]::NewGuid().ToString()     # known ids: other real sessions cannot fool the check
  $tag1 = $sid1.Substring($sid1.Length - 8); $tag2 = $sid2.Substring($sid2.Length - 8)
  $log = Join-Path $env:TEMP 'leonard_real.log'; if (Test-Path $log) { Remove-Item $log }
  $job = Start-Job -ArgumentList $log, $status -ScriptBlock { param($log, $f)
    $last = ''; $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt 90) { $c = Get-Content $f -Raw -ErrorAction SilentlyContinue; if ($c -and $c -ne $last) { Add-Content $log $c; $last = $c }; Start-Sleep -Milliseconds 50 } }
  Start-Sleep 1
  $o1 = & claude -p "Answer with exactly one word: hello" --session-id $sid1 2>&1
  Start-Sleep 2
  $o2 = & claude -p "Use the Bash tool to run: echo leonard-test. Then say done." --allowedTools "Bash" --session-id $sid2 2>&1
  Start-Sleep 2; Stop-Job $job; Remove-Job $job -Force
  $l = Get-Content $log -Raw
  Check 'real chat session reached the app as speaking' ($l -match "\.\.\.$tag1 speaking\b")
  Check 'real task session reached the app as happy'    ($l -match "\.\.\.$tag2 happy \(worked\)")
  Check 'real hooks report no errors' (-not ((@($o1) + @($o2)) -join ' ' -match 'hook.*failed'))
}

"== result: $(if ($fail) { "$fail FAILED" } else { 'ALL PASSED' })"
exit $fail
