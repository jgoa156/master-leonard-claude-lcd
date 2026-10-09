# install.ps1 - set Master Leonard up for the current user (no admin needed). Safe to run again.
#   .\install.ps1              install
#   .\install.ps1 -Uninstall   remove the startup shortcut and the Claude hooks
#
# 1. a Startup-folder shortcut, so leonard.exe runs hidden at every login
# 2. Claude Code hooks in ~\.claude\settings.json, so every session reports to it
param([switch]$Uninstall)
$ErrorActionPreference = 'Stop'
$exe  = Join-Path $PSScriptRoot 'bin\leonard.exe'
$lnk  = Join-Path ([Environment]::GetFolderPath('Startup')) 'Master Leonard.lnk'
$cfg  = Join-Path $env:USERPROFILE '.claude\settings.json'
$cmd  = ($exe -replace '\\', '/') + ' --hook'          # forward slashes: Claude Code runs hooks through bash
$events = 'SessionStart','UserPromptSubmit','PreToolUse','PostToolUse','Notification','Stop','StopFailure','SubagentStop','PreCompact','SessionEnd'
$withMatcher = 'PreToolUse', 'PostToolUse'
$ours = 'leonard|phillip'                               # any earlier hook command of ours matches this

if (-not $Uninstall -and -not (Test-Path $exe)) { throw "bin\leonard.exe not found - run .\build.ps1 first" }

# ---- Claude Code hooks ----
if (-not (Test-Path $cfg)) { New-Item -ItemType Directory -Force (Split-Path $cfg) | Out-Null; '{}' | Set-Content $cfg -Encoding utf8 }
Copy-Item $cfg "$cfg.bak" -Force
$j = Get-Content $cfg -Raw | ConvertFrom-Json
if (-not $j.PSObject.Properties['hooks']) { $j | Add-Member -NotePropertyName hooks -NotePropertyValue ([pscustomobject]@{}) }
foreach ($e in $events) {
    $keep = @()
    if ($j.hooks.PSObject.Properties[$e]) {                # keep other people's hooks, drop earlier copies of ours
        foreach ($g in @($j.hooks.$e)) {
            $rest = @($g.hooks | Where-Object { $_.command -notmatch $ours })
            if ($rest.Count) { $g.hooks = $rest; $keep += $g }
        }
    }
    if (-not $Uninstall) {
        $entry = [ordered]@{ hooks = @([ordered]@{ type = 'command'; command = $cmd }) }
        if ($withMatcher -contains $e) { $entry = [ordered]@{ matcher = '*'; hooks = $entry.hooks } }
        $keep += [pscustomobject]$entry
    }
    if ($j.hooks.PSObject.Properties[$e]) { $j.hooks.PSObject.Properties.Remove($e) }
    if ($keep.Count) { $j.hooks | Add-Member -NotePropertyName $e -NotePropertyValue $keep }
}
if (-not @($j.hooks.PSObject.Properties).Count) { $j.PSObject.Properties.Remove('hooks') }
($j | ConvertTo-Json -Depth 20) | Set-Content $cfg -Encoding utf8
Get-Content $cfg -Raw | ConvertFrom-Json | Out-Null      # must still parse
Write-Host ("hooks:    {0}  ({1})" -f $(if ($Uninstall) { 'removed' } else { "installed -> $cmd" }), $cfg)

# ---- startup shortcut ----
if ($Uninstall) { if (Test-Path $lnk) { Remove-Item $lnk }; Write-Host 'startup:  shortcut removed' }
else {
    $s = (New-Object -ComObject WScript.Shell).CreateShortcut($lnk)
    $s.TargetPath = $exe; $s.Arguments = '--background'; $s.WorkingDirectory = Split-Path $exe; $s.WindowStyle = 7     # hidden at login; a double-click shows the window
    $s.Description = 'Master Leonard - Claude status avatar (background, no window)'; $s.Save()
    Write-Host "startup:  $lnk"
}
Write-Host 'Claude Code reads hooks when a session starts: open a new session to pick them up.'
