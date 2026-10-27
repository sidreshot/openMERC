# SPDX-License-Identifier: GPL-2.0-or-later
# Black-box session of the ORIGINAL SoF.exe 1.07f in the I1c test copy (D:\Games\SoF-test-I1c): optionally an
# original dedicated server, then the original client running a console script. Both run on separate, never
# shown desktops (I4a's DeskRun.cs), so they neither take the focus nor receive the owner's input. The client
# renders through the capture shim ref_omcap.dll (I4a's refcap.c, built into build-I1c-x86\refcap); a script line
# `echo OMSHOT <name>` makes the harness save the next presented frame as <OutDir>\<Tag>_<name>.png.
# Logs: <DataDir>\<Tag>-client.log, <Tag>-server.log. Only the processes this script starts are stopped.
#
#   run_orig.ps1 -ClientScript tests\I1c\rec_orig.cfg -Tag rec            (server + client)
#   run_orig.ps1 -ClientScript tests\I1c\play_orig.cfg -Tag orig -NoServer (client only, e.g. demomap)
# Implements: SPEC-311, SPEC-310 (test harness, no game code)
param(
    [string]$Game = 'D:\Games\SoF-test-I1c',
    [Parameter(Mandatory = $true)][string]$ClientScript,
    [string]$ServerArgs = '+exec i1cdev.cfg +set deathmatch 1 +set maxclients 4 +set timelimit 0 +set fraglimit 0 +map dm/jpndm2',
    [switch]$NoServer,
    [int]$ServerPort = 29540,
    # join the server from the command line and send `exec omtest.cfg` through the out-of-band "cmd" packet once
    # the client log matches -ReadyPattern (a script holding waits would delay the server's stufftexts)
    [switch]$Connect,
    [string]$ReadyPattern = 'entered the game',
    [double]$ReadyDelay = 2,
    [int]$ClientPort = 29541,
    # log lines whose arrival time is written to <DataDir>\<Tag>-times.txt
    [string]$TimeLines = 'OMTEST|OMSHOT',
    # captures by time instead of OMSHOT lines: 'name@seconds,...' counted from the first log line matching
    # -TimedFrom (the harness writes the shim's request file itself)
    [string]$TimedShots = '',
    [string]$TimedFrom = '^Demo ',
    # send `quit` through the out-of-band cmd packet this many seconds after -TimedFrom (0 = never)
    [double]$QuitAfter = 0,
    # I1c: one extra console text sent through the out-of-band cmd packet -OobAfter seconds after -TimedFrom
    [string]$OobText = '',
    [double]$OobAfter = 0,
    [string]$Tag = 'orig',
    [string]$OutDir = 'C:\openmerc-data\shots\I1c',
    [string]$DataDir = 'C:\openmerc-data\I1c',
    [string]$CapBuild = 'C:\openmerc\build-I1c-x86\refcap',
    [string]$ClientExtra = '',
    [int]$Timeout = 180,
    [int]$ServerWait = 30
)
$ErrorActionPreference = 'Stop'

$full = [IO.Path]::GetFullPath($Game).TrimEnd('\')
if ($full -ne 'D:\Games\SoF-test-I1c') { throw "refusing to run outside D:\Games\SoF-test-I1c ($full)" }
$exe = Join-Path $full 'SoF.exe'
if (-not (Test-Path $exe)) { throw "no $exe" }

$deskRun = Join-Path $PSScriptRoot '..\..\engine\src\client\refresh\gl3\test\DeskRun.cs'
if (-not ('DeskRun' -as [type])) {
    Add-Type -Path $deskRun -ReferencedAssemblies System.Drawing
}

New-Item -ItemType Directory -Force $OutDir, $DataDir | Out-Null
Copy-Item (Join-Path $CapBuild 'ref_omcap.dll') (Join-Path $full 'ref_omcap.dll') -Force

$user = Join-Path $full 'user'
$usersrv = Join-Path $full 'usersrv'
New-Item -ItemType Directory -Force $user, $usersrv | Out-Null
Copy-Item $ClientScript (Join-Path $user 'omtest.cfg') -Force
$clog = Join-Path $user 'sof.log'
$slog = Join-Path $usersrv 'sof.log'
foreach ($f in @($clog, $slog, (Join-Path $full 'omshot.req'), (Join-Path $full 'omtrace.req'))) { if (Test-Path $f) { Remove-Item $f -Force } }
New-Item -ItemType Directory -Force (Join-Path $DataDir $Tag) | Out-Null

function Get-OwnSoF { @(Get-Process -Name SoF -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe }) }

$before = @(Get-OwnSoF | ForEach-Object { $_.Id })
$server = $null
$job = $null
try {
    if (-not $NoServer) {
        $sargs = "+set dedicated 1 +set user ./usersrv +set logfile 3 +set ip 127.0.0.1 +set port $ServerPort $ServerArgs"
        Write-Host "server: SoF.exe $sargs"
        $job = Start-Job -ScriptBlock {
            param($cs, $exe, $a, $dir, $to)
            Add-Type -Path $cs -ReferencedAssemblies System.Drawing
            [DeskRun]::Run('OMTestI1cSrv', $exe, $a, $dir, $to, $null, $null, 'SoF', $null)
        } -ArgumentList ([IO.Path]::GetFullPath($deskRun)), $exe, $sargs, $full, (($Timeout + 120) * 1000)
        $sw = [Diagnostics.Stopwatch]::StartNew()
        while ($sw.Elapsed.TotalSeconds -lt $ServerWait) {
            Start-Sleep -Milliseconds 300
            if (-not $server) { $server = Get-OwnSoF | Where-Object { $before -notcontains $_.Id } | Select-Object -First 1 }
            if ((Test-Path $slog) -and (Select-String -Path $slog -SimpleMatch -Pattern 'jpndm2', 'Soldier of Fortune Initialized' -Quiet)) {
                if ((Get-Content $slog -Raw) -match 'Initialized') { break }
            }
        }
        if (-not $server) { throw 'dedicated server did not start' }
        Start-Sleep -Seconds 3
        Write-Host ("server pid {0} up after {1:N1} s" -f $server.Id, $sw.Elapsed.TotalSeconds)
    }

    $cargs = "+set console 1 +set vid_ref omcap +set om_capref gl +set vid_fullscreen 0 +set gl_mode 3 " +
             "+set vid_xpos 0 +set vid_ypos 0 +set logfile 3 +set snd_dll defsnd.dll +set s_nosound 1 " +
             "+set in_mouse 0 +set in_joystick 0 $ClientExtra "
    if ($Connect) {
        $cargs += "+set clientport $ClientPort +set ip_clientport $ClientPort +connect 127.0.0.1:$ServerPort"
    } else {
        $cargs += "+set clientport $ClientPort +set ip_clientport $ClientPort +exec omtest.cfg"
    }
    Write-Host "client: SoF.exe $cargs"
    $capdir = Join-Path $env:TEMP 'omi1c_cap'
    New-Item -ItemType Directory -Force $capdir | Out-Null
    Remove-Item (Join-Path $capdir '*') -Force -ErrorAction SilentlyContinue
    # arrival times of OMTEST / OMSHOT log lines (the log itself has no time stamps)
    $timesFile = Join-Path $DataDir "$Tag-times.txt"
    $tjob = Start-Job -ScriptBlock {
        param($log, $out, $to, $pat)
        $sw = [Diagnostics.Stopwatch]::StartNew(); $pos = 0; $lines = @()
        while ($sw.Elapsed.TotalSeconds -lt $to) {
            Start-Sleep -Milliseconds 50
            if (-not (Test-Path $log)) { continue }
            try {
                $fs = [IO.File]::Open($log, 'Open', 'Read', 'ReadWrite,Delete')
                if ($fs.Length -gt $pos) {
                    $fs.Seek($pos, 'Begin') | Out-Null
                    $buf = New-Object byte[] ($fs.Length - $pos); $n = $fs.Read($buf, 0, $buf.Length); $pos += $n
                    foreach ($l in ([Text.Encoding]::ASCII.GetString($buf, 0, $n) -split "`n")) {
                        if ($l -match $pat) { $lines += ('{0,8:N2} {1}' -f $sw.Elapsed.TotalSeconds, $l.Trim()) }
                    }
                    [IO.File]::WriteAllLines($out, [string[]]$lines)
                }
                $fs.Close()
            } catch { }
        }
    } -ArgumentList $clog, $timesFile, ($Timeout + 5), $TimeLines
    $shotjob = $null
    if ($TimedShots) {
        # I1c: an entry "trace:<name>@<s>" asks the shim for a one-frame call trace (omtrace.req) instead,
        # "ghl:<name>@<s>" ref_omgl for a one-frame Ghoul GL-table trace (omghl.req, <DataDir>\<Tag>\<name>.ghl.txt),
        # written to <DataDir>\<Tag>_<name>.trace.txt
        $shotjob = Start-Job -ScriptBlock {
            param($log, $req, $capdir, $shots, $from, $to, $treq, $tdir)
            $sw = [Diagnostics.Stopwatch]::StartNew(); $t0 = $null; $done = @()
            $list = @($shots -split ',' | ForEach-Object { $n, $t = $_ -split '@'; [pscustomobject]@{ n = $n.Trim(); t = [double]$t } })
            while ($sw.Elapsed.TotalSeconds -lt $to -and $done.Count -lt $list.Count) {
                Start-Sleep -Milliseconds 5
                if ($null -eq $t0) {
                    if ((Test-Path $log) -and (Select-String -Path $log -Pattern $from -Quiet)) { $t0 = $sw.Elapsed.TotalSeconds }
                    continue
                }
                foreach ($s in $list) {
                    if ($done -contains $s.n) { continue }
                    if ($s.n -like 'ghl:*') {
                        $greq = Join-Path (Split-Path $treq) 'omghl.req'
                        if ($sw.Elapsed.TotalSeconds - $t0 -ge $s.t -and -not (Test-Path $greq)) {
                            [IO.File]::WriteAllText($greq, (Join-Path $tdir ($s.n.Substring(4) + '.ghl.txt')))
                            $done += $s.n
                            Write-Output ('timed Ghoul GL trace {0} at +{1:N3} s' -f $s.n, ($sw.Elapsed.TotalSeconds - $t0))
                        }
                        continue
                    }
                    if ($s.n -like 'trace:*') {
                        if ($sw.Elapsed.TotalSeconds - $t0 -ge $s.t -and -not (Test-Path $treq)) {
                            [IO.File]::WriteAllText($treq, (Join-Path $tdir ($s.n.Substring(6) + '.trace.txt')))
                            $done += $s.n
                            Write-Output ('timed trace {0} at +{1:N3} s' -f $s.n, ($sw.Elapsed.TotalSeconds - $t0))
                        }
                        continue
                    }
                    if ($sw.Elapsed.TotalSeconds - $t0 -ge $s.t -and -not (Test-Path $req)) {
                        [IO.File]::WriteAllText($req, (Join-Path $capdir ($s.n + '.tga')))
                        $done += $s.n
                        Write-Output ('timed shot {0} at +{1:N2} s' -f $s.n, ($sw.Elapsed.TotalSeconds - $t0))
                    }
                }
            }
        } -ArgumentList $clog, (Join-Path $full 'omshot.req'), $capdir, $TimedShots, $TimedFrom, ($Timeout + 5),
            (Join-Path $full 'omtrace.req'), (Join-Path $DataDir "$Tag")
    }
    $ojob = $null
    if ($QuitAfter -gt 0) {
        $ojob = Start-Job -ScriptBlock {
            param($py, $log, $pat, $port, $delay, $to)
            & python -I $py --log $log --wait $pat --port $port --delay $delay --timeout $to --text 'quit' 2>&1
        } -ArgumentList (Join-Path $PSScriptRoot 'oobcmd.py'), $clog, $TimedFrom, $ClientPort, $QuitAfter, $Timeout
    } elseif ($Connect) {
        $ojob = Start-Job -ScriptBlock {
            param($py, $log, $pat, $port, $delay, $to)
            & python -I $py --log $log --wait $pat --port $port --delay $delay --timeout $to --text 'exec omtest.cfg' 2>&1
        } -ArgumentList (Join-Path $PSScriptRoot 'oobcmd.py'), $clog, $ReadyPattern, $ClientPort, $ReadyDelay, $Timeout
    }
    $xjob = $null
    if ($OobText) {
        $xjob = Start-Job -ScriptBlock {
            param($py, $log, $pat, $port, $delay, $to, $text)
            & python -I $py --log $log --wait $pat --port $port --delay $delay --timeout $to --text $text 2>&1
        } -ArgumentList (Join-Path $PSScriptRoot 'oobcmd.py'), $clog, $TimedFrom, $ClientPort, $OobAfter, $Timeout, $OobText
    }
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $code = [DeskRun]::Run('OMTestI1c', $exe, $cargs, $full, $Timeout * 1000, $clog, $capdir, 'SoF',
                           (Join-Path $full 'omshot.req'))
    Write-Host ("client exit code {0} after {1:N1} s{2}" -f $code, $sw.Elapsed.TotalSeconds, $(if ($code -eq -1) { ' (killed)' } else { '' }))
    Start-Sleep -Milliseconds 300
    Stop-Job $tjob -ErrorAction SilentlyContinue; Remove-Job $tjob -Force -ErrorAction SilentlyContinue
    if ($shotjob) { Stop-Job $shotjob -ErrorAction SilentlyContinue; Receive-Job $shotjob | Write-Host; Remove-Job $shotjob -Force -ErrorAction SilentlyContinue }
    if ($xjob) { Stop-Job $xjob -ErrorAction SilentlyContinue; Receive-Job $xjob | Write-Host; Remove-Job $xjob -Force -ErrorAction SilentlyContinue }
    if ($ojob) { Stop-Job $ojob -ErrorAction SilentlyContinue; Receive-Job $ojob | Write-Host; Remove-Job $ojob -Force -ErrorAction SilentlyContinue }
    $n = 0
    foreach ($c in Get-ChildItem (Join-Path $capdir '*.tga')) {
        $png = Join-Path $OutDir ("{0}_{1}.png" -f $Tag, $c.BaseName)
        python -I -c "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])" $c.FullName $png
        Remove-Item $c.FullName -Force
        $n++
    }
    Write-Host "$n capture(s) -> $OutDir"
} finally {
    if ($server -and -not $server.HasExited) {
        Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue
        $server.WaitForExit(5000) | Out-Null
    }
    # any other process of this test copy that this run started (never earlier ones)
    Get-OwnSoF | Where-Object { $before -notcontains $_.Id } | ForEach-Object {
        Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
    }
    if ($job) { Wait-Job $job -Timeout 15 | Out-Null; Remove-Job $job -Force -ErrorAction SilentlyContinue }
    Start-Sleep -Milliseconds 500
    if (Test-Path $clog) { Copy-Item $clog (Join-Path $DataDir "$Tag-client.log") -Force }
    if (Test-Path $slog) { Copy-Item $slog (Join-Path $DataDir "$Tag-server.log") -Force }
    $req = Join-Path $full 'omshot.req'
    if (Test-Path $req) { Remove-Item $req -Force }
    $treq = Join-Path $full 'omtrace.req'
    if (Test-Path $treq) { Remove-Item $treq -Force }
    Write-Host "logs in $DataDir"
}
