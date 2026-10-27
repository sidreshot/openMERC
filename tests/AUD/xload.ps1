# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc AUD - OMsnd on a real output device under CPU load: one X1 run (tests/X1/xrun.ps1, dlg.cfg) with the
# game's own process audio recorded by proccap.exe (process loopback, so nothing else on the PC is captured), while
# -Burn busy-loop processes load every core. Then wavgaps.py on the recording, proccap's packet counters and the
# s_om_debug 8/16 log lines. Output: openmerc-data\aud\<Tag>\ (cap.wav, cap.wav.txt, client log) and a summary line.
#   xload.ps1 -Tag load4_tsr1 -Map tsr1 -Burn 4 [-Device "OpenAL Soft on ..."] [-Extra 'set s_om_refresh 50']
# Implements: black-box test harness (no game code)
param(
    [string]$Tag = 'load',
    [string]$Map = 'tsr1',
    [string]$Game = 'D:\Games\OpenMerc-audio-test',
    [string]$Device = 'OpenAL Soft on 1 - 27M1N3500LS (2- AMD High Definition Audio Device)',
    [int]$Burn = 0,
    [string]$BurnPriority = 'Normal',
    [string]$Fps = '165',
    [int]$Timeout = 110,
    [int]$Port = 29900,
    [int]$Debug = 24,
    [string]$ProcCap = 'C:\openmerc\build-I5s2-x86\test\proccap.exe',
    [string]$Python = 'python',
    [string[]]$Extra = @()
)
$ErrorActionPreference = 'Stop'
# machine safety: busy loops above Normal starve the rest of the PC (a High-priority run once took the machine off
# the network), and the PC is shared, so at most half of the logical processors are loaded
if ($BurnPriority -notin @('Normal', 'BelowNormal', 'Idle')) { throw "-BurnPriority $BurnPriority refused (Normal, BelowNormal or Idle only)" }
$maxBurn = [Math]::Max(1, [int]([Environment]::ProcessorCount / 2))
if ($Burn -gt $maxBurn) { throw "-Burn $Burn refused (at most $maxBurn, half of the logical processors)" }
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$xrun = Join-Path $here '..\X1\xrun.ps1'
$root = 'C:\openmerc-data\aud'
$data = Join-Path $root $Tag
$exe = Join-Path $Game 'openMerc.exe'
$cfg = @('set s_om_reverb 0', 'set s_om_wavefile ""', "set s_om_device `"$Device`"", 'set r_vsync 0',
    "set vid_maxfps $Fps", "set s_om_debug $Debug") + $Extra
$burners = @()
for ($i = 0; $i -lt $Burn; $i++) {
    $p = Start-Process -FilePath $Python -ArgumentList '-I', '-c', '"while True: pass"' -PassThru -WindowStyle Hidden
    $p.PriorityClass = $BurnPriority
    $burners += $p
}
$cap = $null
try {
    $job = Start-Job -ScriptBlock {
        param($xrun, $script, $map, $tag, $to, $game, $sp, $cfg, $root)
        & $xrun -Script $script -Map $map -Tag $tag -Timeout $to -Game $game -ServerPort $sp -ClientPort ($sp + 1) `
            -Config $cfg -DataRoot $root *>&1 | Out-String
    } -ArgumentList $xrun, (Join-Path $here 'dlg.cfg'), $Map, $Tag, $Timeout, $Game, $Port, $cfg, $root
    $gp = $null
    for ($i = 0; $i -lt 300 -and -not $gp; $i++) {
        Start-Sleep -Milliseconds 100
        $gp = Get-Process -Name openMerc -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe } | Select-Object -First 1
    }
    if (-not $gp) { throw "no game process" }
    while (-not (Test-Path $data)) { Start-Sleep -Milliseconds 100 }
    $wav = Join-Path $data 'cap.wav'
    $cap = Start-Process -FilePath $ProcCap -ArgumentList "$($gp.Id) `"$wav`" 48000 $($Timeout + 30)" -PassThru `
        -WindowStyle Hidden -RedirectStandardError (Join-Path $data 'proccap.txt')
    $out = $job | Wait-Job | Receive-Job
    $out | Set-Content (Join-Path $data 'xrun.txt')
    if ($cap) { $cap.WaitForExit(15000) | Out-Null }
}
finally {
    foreach ($p in $burners) { if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force } }
    if ($cap -and -not $cap.HasExited) { New-Item -ItemType File "$wav.stop" -Force | Out-Null; $cap.WaitForExit(5000) | Out-Null }
}
$line = "$Tag (burn $Burn $BurnPriority, $Fps fps): "
if (Test-Path $wav) { $line += (& python -I (Join-Path $here 'wavgaps.py') $wav | Select-Object -First 1) }
if (Test-Path "$wav.txt") {
    $t = Get-Content "$wav.txt" -Raw
    if ($t -match 'discontinuities (\d+)') { $line += " | discont $($Matches[1])" }
    if ($t -match 'devpos_gap_frames (\d+)') { $line += ", devgap $($Matches[1])" }
    if ($t -match 'gap_fill_frames (\d+)') { $line += ", gapfill $($Matches[1])" }
}
$log = Get-ChildItem $data -Recurse -Filter 'sof.log' | Select-Object -First 1 -ExpandProperty FullName
if ($log) {
    $cuts = @(Select-String -Path $log -Pattern 'OMsnd: \[p=\d+\] cut ')
    $adp = @($cuts | Where-Object { $_.Line -match '\.adp' })
    $dry = @(Select-String -Path $log -Pattern 'ran dry')
    $kinds = ($cuts | ForEach-Object { if ($_.Line -match 'cut \((\w+)\)') { $Matches[1] } } | Group-Object |
            ForEach-Object { "$($_.Name) $($_.Count)" }) -join ', '
    $line += " | cuts $($cuts.Count) (adp $($adp.Count); $kinds), music dry $($dry.Count)"
}
$line | Add-Content (Join-Path $root 'xload.txt')
$line
