# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc mode-B audit - runs many xrun.ps1 sessions in parallel on the never-shown desktop against an open-only
# copy (no original DLL), each with its own port pair, then writes one summary line per run (xsummary.py).
#   xaudit.ps1 -Game D:\Games\OpenMerc-modeB-audit [-Parallel 6] [-Only sp,chain,dm,save,probe] [-PortBase 29700]
# probe = xprobe.py --make session (real-time om_delay chain) on every SP map (stuck / sliding / floating / crash), analysed by xprobe.py; xclip_scan.py is
# the static clip census of the same maps. -PortBase moves the port pairs (base+2k, base+2k+1) for a second audit.
# Ports: audit bases 29700 and 29710 (pairs base+0/+1 ... base+8/+9), not the integrator's 29680/29681.
# Generated per-run cfgs go to <DataRoot>\cfg (never into the repository).
# Implements: black-box test harness (no game code)
param(
    [string]$Game = 'D:\Games\OpenMerc-modeB-audit',
    [string]$DataRoot = 'C:\openmerc-data\aud\runs',
    [int]$Parallel = 6,
    [string[]]$Only = @('sp', 'chain', 'dm', 'save'),
    [string[]]$Maps = @(),
    [int]$Intro = 6,
    [int]$PortBase = 29700,
    [int]$ProbeIntro = 25,
    [int]$ProbeCycles = 14
)
$ErrorActionPreference = 'Stop'
$Only = @($Only | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$here = $PSScriptRoot
New-Item -ItemType Directory -Force $DataRoot, (Join-Path $DataRoot 'cfg') | Out-Null
$campaign = @('tsr1', 'tsr2', 'trn1', 'arm1', 'kos1', 'kos2', 'kos3', 'sib1', 'sib2', 'sib3', 'irq1a', 'irq2a', 'irq3a',
    'arm2', 'nyc1', 'nyc2', 'nyc3', 'sud1', 'sud2', 'sud3', 'arm3', 'jpn1', 'jpn2', 'jpn3', 'irq1b', 'irq2b', 'irq3b',
    'ger1', 'ger2', 'ger3', 'ger4')
$sp = @('tut1') + $campaign
if ($Maps) { $sp = @($Maps | ForEach-Object { $_ -split ',' } | Where-Object { $_ }) }
# long opening cinematics (measured: tsr1 ~70 s of credits before the player gets control)
$introLong = @{ 'tsr1' = 22 }
$hdr = @('alias w10 "wait;wait;wait;wait;wait;wait;wait;wait;wait;wait"',
    'alias w100 "w10;w10;w10;w10;w10;w10;w10;w10;w10;w10"',
    'alias w1k "w100;w100;w100;w100;w100;w100;w100;w100;w100;w100"')
function IntroLines([int]$n) { @(1..$n | ForEach-Object { 'w1k' }) }
$jobs = New-Object System.Collections.ArrayList
if ($Only -contains 'sp') {
    $body = Get-Content (Join-Path $here 'x1_audit.cfg') | Where-Object { $_ -notmatch '^alias w1' }
    foreach ($m in $sp) {
        $n = if ($introLong.ContainsKey($m)) { $introLong[$m] } else { $Intro }
        $f = Join-Path $DataRoot "cfg\sp_$m.cfg"
        Set-Content $f -Encoding ASCII -Value ($hdr + @('echo OMTEST intro') + (IntroLines $n) + $body)
        [void]$jobs.Add([pscustomobject]@{ Tag = "sp_$m"; Map = $m; Script = $f; Extra = '+set cheats 1'; Timeout = 200 + 4 * $n })
    }
}
if ($Only -contains 'probe') {
    # real seconds (om_delay chain), not frames: hidden runs are uncapped and frame waits end inside the cinematic
    foreach ($m in $sp) {
        $s = if ($m -eq 'tsr1') { 85 } else { $ProbeIntro }
        $f = Join-Path $DataRoot "cfg\probe_$m.cfg"
        & python (Join-Path $here 'xprobe.py') --make $s $ProbeCycles $f
        if ($LASTEXITCODE) { throw "xprobe.py --make failed" }
        [void]$jobs.Add([pscustomobject]@{ Tag = "probe_$m"; Map = $m; Script = $f; Extra = '+set cheats 1'; Timeout = 90 + $s + 10 * $ProbeCycles })
    }
}
if ($Only -contains 'chain') {
    for ($i = 0; $i -lt $campaign.Count; $i += 4) {
        $seg = $campaign[$i..([Math]::Min($i + 4, $campaign.Count - 1))]
        $l = $hdr + @('alias stay "w1k;w1k;w1k;viewpos;+forward;w100;w100;-forward;+attack;w10;-attack;w1k"',
            "echo OMTEST chain $($seg[0])") + (IntroLines $(if ($introLong.ContainsKey($seg[0])) { $introLong[$seg[0]] } else { 3 })) +
            @('stay', 'echo OMTEST inventory', 'weapnext', 'w100', "omshot c_$($seg[0])")
        foreach ($t in $seg[1..($seg.Count - 1)]) {
            $l += @("echo OMTEST gamemap $t", "gamemap $t", 'w1k', 'w1k', 'stay', "omshot c_$t")
        }
        $l += @('echo OMTEST chain end', 'w100', 'quit')
        $f = Join-Path $DataRoot "cfg\chain_$($seg[0]).cfg"
        Set-Content $f -Encoding ASCII -Value $l
        [void]$jobs.Add([pscustomobject]@{ Tag = "chain_$($seg[0])"; Map = $seg[0]; Script = $f; Extra = ''; Timeout = 120 + 40 * $seg.Count })
    }
}
if ($Only -contains 'save') {
    # save / load mid-level, after a level change, the automatic save (x1_save.cfg's sequence, started on kos1)
    $l = $hdr + @('alias walk "+forward;w100;w100;w100;-forward;w100"', 'echo OMTEST intro') + (IntroLines 5) +
        @('viewpos', 'omshot s0_before', 'echo OMTEST save mid', 'save x1mid', 'w1k', 'walk', 'viewpos', 'omshot s1_walked',
          'echo OMTEST load mid', 'load x1mid', 'w1k', 'w1k', 'viewpos', 'omshot s2_loaded', 'walk', '+attack', 'w10', '-attack',
          'w1k', 'viewpos', 'echo OMTEST gamemap kos2', 'gamemap kos2', 'w1k', 'w1k', 'w1k', 'w1k', 'w1k', 'viewpos', 'omshot s4_kos2',
          'echo OMTEST save after change', 'save x1chg', 'w1k', 'walk', 'viewpos', 'echo OMTEST load after change', 'load x1chg',
          'w1k', 'w1k', 'viewpos', 'omshot s5_loaded2', 'echo OMTEST load auto', 'load auto', 'w1k', 'w1k', 'viewpos',
          'omshot s7_auto', 'walk', 'viewpos', 'echo OMTEST quit', 'w100', 'quit')
    $f = Join-Path $DataRoot 'cfg\save_kos1.cfg'
    Set-Content $f -Encoding ASCII -Value $l
    [void]$jobs.Add([pscustomobject]@{ Tag = 'save_kos1'; Map = 'kos1'; Script = $f; Extra = ''; Timeout = 300 })
}
if ($Only -contains 'dm') {
    # deathmatch N: 1 DM, 2 Assassin, 3 Arsenal, 4 CTF, 5 Realistic, 6 Control, 7 Conquer (game/src/modes)
    foreach ($d in @(@('dm/nycdm4', 1), @('dm/jpndm2', 1), @('dm/suddm1', 2), @('dm/irqdm1', 3), @('dm/gerctf1', 4),
            @('dm/sudctf2', 4), @('dm/kosdm1', 5), @('dm/koscon1', 6), @('dm/sibctb1', 7), @('dm/airportdm', 1))) {
        $tag = 'dm_' + ($d[0] -replace '^dm/', '') + "_m$($d[1])"
        [void]$jobs.Add([pscustomobject]@{ Tag = $tag; Map = $d[0]; Script = (Join-Path $here 'x1_dmaudit.cfg')
                Extra = "+set deathmatch $($d[1]) +set maxclients 6 +set bot_fillserv 1 +set cheats 1"; Timeout = 200 })
    }
}
Write-Host ("{0} runs, {1} in parallel" -f $jobs.Count, $Parallel)
$slots = @{}
$running = @{}
$queue = [System.Collections.Queue]::new($jobs)
# xrun.ps1 stops every openMerc.exe of its own exe path that it did not see at its start, so parallel runs need
# separate exe paths: one clone per slot (binaries copied, paks hard-linked) under <Game>-slots\s<k>
function SlotGame([int]$k) {
    $d = "$Game-slots\s$k"
    New-Item -ItemType Directory -Force (Join-Path $d 'base') | Out-Null
    foreach ($x in Get-ChildItem $Game -File) { Copy-Item $x.FullName $d -Force }
    foreach ($x in Get-ChildItem (Join-Path $Game 'base') -File) {
        $t = Join-Path $d "base\$($x.Name)"
        if ($x.Extension -eq '.pak' -and $x.Name -ne 'zz_openmerc.pak') { if (-not (Test-Path $t)) { New-Item -ItemType HardLink -Path $t -Target $x.FullName | Out-Null } }
        else { Copy-Item $x.FullName $t -Force }
    }
    return $d
}
for ($k = 0; $k -lt $Parallel; $k++) { [void](SlotGame $k) }
function PortOf([int]$k) { $PortBase + 2 * $k }
while ($queue.Count -gt 0 -or $running.Count -gt 0) {
    foreach ($k in @($running.Keys)) {
        $p = $running[$k]
        if ($p.Proc.HasExited) {
            $line = & python -I (Join-Path $here 'xsummary.py') (Join-Path $DataRoot $p.Job.Tag)
            Add-Content -Path (Join-Path $DataRoot 'summary.txt') -Value ("{0,-22} {1}" -f $p.Job.Tag, $line) -Encoding utf8
            & python -I (Join-Path $here 'xsummary.py') (Join-Path $DataRoot $p.Job.Tag) --json | Add-Content -Path (Join-Path $DataRoot 'summary.jsonl') -Encoding utf8
            Write-Host ("done {0,-22} {1}" -f $p.Job.Tag, $line)
            $running.Remove($k)
        }
    }
    while ($queue.Count -gt 0 -and $running.Count -lt $Parallel) {
        $k = 0; while ($running.ContainsKey($k)) { $k++ }
        $j = $queue.Dequeue()
        $sp0 = PortOf $k
        $a = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $here 'xrun.ps1'), '-Script', $j.Script, '-Map', $j.Map,
            '-Tag', $j.Tag, '-Timeout', $j.Timeout, '-Game', "$Game-slots\s$k", '-ServerPort', $sp0, '-ClientPort', ($sp0 + 1), '-DataRoot', $DataRoot)
        if ($j.Extra) { $a += @('-Extra', "`"$($j.Extra)`"") }
        $out = Join-Path $DataRoot "$($j.Tag).xrun.out"
        $proc = Start-Process powershell.exe -ArgumentList $a -NoNewWindow -PassThru -RedirectStandardOutput $out -RedirectStandardError "$out.err"
        $running[$k] = [pscustomobject]@{ Proc = $proc; Job = $j }
        Write-Host ("start {0,-22} port {1}" -f $j.Tag, $sp0)
        Start-Sleep -Seconds 2
    }
    Start-Sleep -Seconds 2
}
Write-Host 'all runs finished'

