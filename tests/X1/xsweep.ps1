# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc X1 - single-player map sweep: every map of the list through xrun.ps1 (x1_sweep.cfg by default), one
# line per map (exit code, crash reports, console errors/warnings, captures, sound) in
# openmerc-data\x1\<Prefix>.txt, details per map in openmerc-data\x1\<Prefix>.jsonl, run directories
# openmerc-data\x1\<Prefix>_<map>\. A map that crashes or hangs is recorded and the sweep goes on.
#   xsweep.ps1 -Prefix sw1                                      open stack (D:\Games\SoF-open-X1)
#   xsweep.ps1 -Prefix or1 -Game D:\Games\SoF-test-X1 -ExeSub om  original gamex86/player on the same engine build
# Implements: black-box test harness (no game code)
param(
    [string]$Prefix = 'sw',
    [string[]]$Maps = @('tut1', 'tsr1', 'tsr2', 'trn1', 'kos1', 'kos2', 'kos3', 'nyc1', 'nyc2', 'nyc3', 'sib1', 'sib2',
        'sib3', 'irq1a', 'irq1b', 'irq2a', 'irq2b', 'irq3a', 'irq3b', 'sud1', 'sud2', 'sud3', 'jpn1', 'jpn2', 'jpn3',
        'ger1', 'ger2', 'ger3', 'ger4', 'arm1', 'arm2', 'arm3'),
    [string]$Script = '',
    [string]$Game = 'D:\Games\SoF-open-X1',
    [string]$ExeSub = '',
    [string]$Extra = '',
    [int]$Timeout = 110
)
if (-not $Script) { $Script = Join-Path $PSScriptRoot 'x1_sweep.cfg' }
$Maps = @($Maps | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$root = 'C:\openmerc-data\x1'
$sum = Join-Path $root "$Prefix.txt"
$js = Join-Path $root "$Prefix.jsonl"
foreach ($m in $Maps) {
    $tag = "${Prefix}_$m"
    $a = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'xrun.ps1'), '-Script', $Script,
        '-Map', $m, '-Tag', $tag, '-Timeout', $Timeout, '-Game', $Game, '-Unlock')
    if ($ExeSub) { $a += @('-ExeSub', $ExeSub) }
    if ($Extra) { $a += @('-Extra', $Extra) }
    $o = & powershell.exe @a *>&1
    $o | Out-File -Encoding utf8 (Join-Path (Join-Path $root $tag) 'xrun.out')
    if ($o -match 'NOT identical') { Write-Warning "lock key not restored after $m"; break }
    $l = & python -I (Join-Path $PSScriptRoot 'xsummary.py') (Join-Path $root $tag)
    $j = & python -I (Join-Path $PSScriptRoot 'xsummary.py') (Join-Path $root $tag) --json
    Add-Content -Path $sum -Value $l -Encoding utf8
    Add-Content -Path $js -Value $j -Encoding utf8
    Write-Host $l
}
