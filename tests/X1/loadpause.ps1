# SPDX-License-Identifier: GPL-2.0-or-later
# Regression check t91u-6: after Load and New Game from the in-game menu the game runs (paused 0, menus closed).
# Before the fix the loading screen was begun twice on a load, its menu closed early and `paused` 1 of the load
# stayed until the menu was opened and closed again.
#   loadpause.ps1 -Game <test copy that is not in use>   (xrun stops openMerc.exe processes it did not see at start)
param([Parameter(Mandatory)][string]$Game, [string]$Tag = 'loadpause')
$ErrorActionPreference = 'Stop'
& powershell -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot\xrun.ps1" -Script "$PSScriptRoot\x1_loadpause.cfg" -Map kos1 `
  -Tag $Tag -Game $Game -Timeout 200 -Extra '+set busywait 0' -NoSound | Select-Object -Last 1
$log = Join-Path 'C:\openmerc-data\x1' "$Tag\client.log"
$lines = Get-Content $log
$fail = 0
foreach ($m in 'LP_AFTER_LOAD', 'LP_AFTER_NEWGAME') {
    $i = ($lines | Select-String -SimpleMatch "PROBE $m" | Select-Object -First 1).LineNumber
    if (-not $i) { Write-Host "FAIL $m`: no probe"; $fail++; continue }
    $st = $lines[$i - 2]
    if ($st -match 'key_dest 0 cls\.state 4 active 0 .*paused 0 cinfreeze') { Write-Host "ok   $m`: $st" }
    else { Write-Host "FAIL $m`: $st"; $fail++ }
}
if ($fail) { exit 1 } else { Write-Host 'loadpause: all passed' }