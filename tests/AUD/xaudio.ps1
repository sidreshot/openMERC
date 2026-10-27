# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc AUD - dialogue-heavy maps through tests/X1/xrun.ps1 with the OMsnd wave writer, at an uncapped frame
# rate and at 60 fps, several sessions in parallel (own ports each), then wavgaps.py and the s_om_debug 8/16 log
# lines per run. Output: openmerc-data\aud\<Prefix>_<map>_<fps>\ and <Prefix>.txt (-Extra: more config lines)
#   xaudio.ps1 -Prefix before [-Game D:\Games\OpenMerc-audio-test] [-Maps trn1,tsr1,kos1,nyc1]
# Implements: black-box test harness (no game code)
param(
    [string]$Prefix = 'run',
    [string]$Game = 'D:\Games\OpenMerc-audio-test',
    [string[]]$Maps = @('trn1', 'tsr1', 'kos1', 'nyc1'),
    [string[]]$Fps = @('300', '165', '60'),
    [int]$Timeout = 110,
    [int]$Parallel = 4,
    [int]$PortBase = 29700,
    [int]$Debug = 24,
    [string[]]$Extra = @()
)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$xrun = Join-Path $here '..\X1\xrun.ps1'
$root = 'C:\openmerc-data\aud'
New-Item -ItemType Directory -Force $root | Out-Null
$jobs = @()
$k = 0
foreach ($m in $Maps) {
    foreach ($f in $Fps) {
        $tag = "${Prefix}_${m}_$f"
        $cfg = @('set s_om_reverb 0') + $Extra
        # vid_maxfps 0 follows the hidden desktop's 60 Hz, so the uncapped rates are set explicitly (300; 165 is the
        # owner's monitor); 60 also caps cl_maxfps
        $cfg += @('set r_vsync 0', "set vid_maxfps $f", "set s_om_debug $Debug")
        if ($f -eq '60') { $cfg += 'set cl_maxfps 60' }
        $sp = $PortBase + 4 * $k
        while (@($jobs | Where-Object { $_.State -eq 'Running' }).Count -ge $Parallel) { Start-Sleep -Seconds 2 }
        $jobs += Start-Job -ScriptBlock {
            param($xrun, $script, $map, $tag, $to, $game, $sp, $cfg, $root)
            & $xrun -Script $script -Map $map -Tag $tag -Timeout $to -Game $game -ServerPort $sp -ClientPort ($sp + 1) `
                -Config $cfg -DataRoot $root *>&1 | Out-String
        } -ArgumentList $xrun, (Join-Path $here 'dlg.cfg'), $m, $tag, $Timeout, $Game, $sp, $cfg, $root
        $k++
        Start-Sleep -Seconds 3
    }
}
$jobs | Wait-Job | Out-Null
$sum = @()
foreach ($m in $Maps) {
    foreach ($f in $Fps) {
        $d = Join-Path $root "${Prefix}_${m}_$f"
        $wav = Join-Path $d 'snd.wav'
        $log = Join-Path $d 'client.log'
        $line = "$m $f : "
        if (Test-Path $wav) { $line += (& python -I (Join-Path $here 'wavgaps.py') $wav | Select-Object -First 1) }
        if (Test-Path $log) {
            $cuts = @(Select-String -Path $log -Pattern 'OMsnd: \[p=\d+\] cut ')
            $adp = @($cuts | Where-Object { $_.Line -match ' adp ' })
            $sec = @(Select-String -Path $log -Pattern '1s: voices')
            $ls = 0; $gs = 0; $fl = 0
            foreach ($x in $sec) {
                if ($x.Line -match 'loop stops (\d+), gain steps (\d+), 3d flips (\d+)') {
                    $ls += [int]$Matches[1]; $gs += [int]$Matches[2]; $fl += [int]$Matches[3]
                }
            }
            $kinds = ($cuts | ForEach-Object { if ($_.Line -match 'cut \((\w+)\)') { $Matches[1] } } | Group-Object |
                    ForEach-Object { "$($_.Name) $($_.Count)" }) -join ', '
            $line += " | cuts $($cuts.Count) (adp $($adp.Count); $kinds), loop stops $ls, gain steps $gs, 3d flips $fl, $($sec.Count) s"
        }
        $sum += $line
    }
}
$sum | Set-Content (Join-Path $root "$Prefix.txt")
$sum
