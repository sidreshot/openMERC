# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc I1b - single-player map sweep: every map of the list through sprun.ps1 (sp_sweep.cfg), one engine;
# prints one line per map (exit code, crash report, error lines of the log) and keeps the captures in
# openmerc-data\i1b\<prefix>_<map>\.
#   sweep.ps1 -Engine om -Prefix sw1 [-Maps kos1,kos2] [-Extra '+set sv_watchdog 8']
# Implements: SPEC-001 / SPEC-003 black-box test harness (no game code)
param(
    [ValidateSet('om', 'orig')][string]$Engine = 'om',
    [string]$Prefix = 'sw',
    [string[]]$Maps = @('tut1', 'tsr1', 'tsr2', 'trn1', 'kos1', 'kos2', 'kos3', 'nyc1', 'nyc2', 'nyc3', 'sib1', 'sib2',
        'sib3', 'irq1a', 'irq1b', 'irq2a', 'irq2b', 'irq3a', 'irq3b', 'sud1', 'sud2', 'sud3', 'jpn1', 'jpn2', 'jpn3',
        'ger1', 'ger2', 'ger3', 'ger4', 'arm1', 'arm2', 'arm3'),
    [string]$Extra = '',
    [int]$Timeout = 120,
    # session script (default sp_sweep.cfg); sp_idle.cfg with -TimedShots for time-aligned pictures
    [string]$Script = '',
    [string]$TimedShots = ''
)
if (-not $Script) { $Script = Join-Path $PSScriptRoot 'sp_sweep.cfg' }
# -File passes '-Maps a,b,c' as one string
$Maps = @($Maps | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$here = $PSScriptRoot
$first = $true
foreach ($m in $Maps) {
    $tag = "${Prefix}_$m"
    $args2 = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $here 'sprun.ps1'), '-Engine', $Engine,
        '-Script', $Script, '-Map', $m, '-Tag', $tag, '-Timeout', $Timeout)
    if ($Extra) { $args2 += @('-Extra', $Extra) }
    if ($TimedShots -and $Engine -eq 'orig') { $args2 += @('-TimedShots', $TimedShots) }
    if (-not $first -and $Engine -eq 'om') { $args2 += '-NoCopy' }
    $first = $false
    $o = & powershell.exe @args2 *>&1
    $exit = ($o | Select-String 'exit code' | Select-Object -First 1).Line
    $caps = ($o | Select-String 'capture' | Select-Object -First 1).Line
    $data = "C:\openmerc-data\i1b\$tag"
    $note = ''
    $wd = Join-Path $data 'user\watchdog.txt'
    if (Test-Path $wd) {
        $note = ' CRASH: ' + ((Get-Content $wd | Select-String 'crash:|stall:|last filtered|offered' | ForEach-Object { $_.Line.Trim() }) -join ' | ')
    }
    $log = Join-Path $data 'client.log'
    if (Test-Path $log) {
        $err = Get-Content $log -Encoding Default | Select-String -Pattern 'ERROR|Error|error|fatal|Couldn''t|failed' |
            Where-Object { $_.Line -notmatch 'conback|primary sound format|Error during loading chunk 0' } |
            Select-Object -First 3 | ForEach-Object { $_.Line.Trim() }
        if ($err) { $note += ' LOG: ' + ($err -join ' | ') }
    }
    Write-Host ("{0,-6} {1} {2}{3}" -f $m, $exit, $caps, $note)
}
