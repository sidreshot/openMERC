# SPDX-License-Identifier: GPL-2.0-or-later
# Runs the OpenMerc client (build-I2d-x86, -portable, data from D:\Games\SoF-test-I2d, user directory under
# openmerc-data\I2d\omhome) with one of the console scripts written for the original (tests\I2d\orig_*.cfg) on a
# separate, never shown desktop (I6m's run_om.ps1 + I4a's DeskRun.cs: no focus, no input from the owner).
# The script is adapted to the OpenMerc engine first:
#   - the timing aliases: the original runs about 1000 `wait`s per second, the OpenMerc client about 200, so
#     w1s becomes 200 waits (about 1.0 s, measured with calib.cfg: 100 waits = 0.50 s; the original's 1000 waits
#     take about 1.07 s), so time markers in a script fall at about the same real time on both engines;
#   - `echo OMSHOT <name>` becomes `om_menushot <name>` + 10 waits (the capture is taken at the end of the next
#     drawn menu frame, so the screen must stay until then) -> <OutDir>\<Tag>_<name>.png.
# Optionally starts tools\net\chattest.py first (-Fake: scripted server on 127.0.0.1:16668; -Proxy: logging relay
# 127.0.0.1:16667 -> -Upstream, with -Peer a second user on the upstream server) and stops it afterwards.
#   powershell -ExecutionPolicy Bypass -File tests\I2d\run_om.ps1 -Script tests\I2d\orig_fake.cfg -Tag omfake -Fake
# Implements: SPEC-204 section 5, SPEC-803 section 3 (test harness, no game code)
param(
    [Parameter(Mandatory = $true)][string]$Script,
    [string]$Tag = 'om',
    [string]$OutDir = 'C:\openmerc-data\I2d\om',
    [string]$Exe = 'C:\openmerc\build-I2d-x86\release\openMerc.exe',
    [string]$HomeDir = 'C:\openmerc-data\I2d\omhome',
    [switch]$Fake,
    [switch]$Proxy,
    [string]$Upstream = 'peerchat.openspy.net:6667',
    [string]$Peer = '',          # "<peer nick>,<channel>,<target nick>"
    [string]$Extra = '',
    [int]$Timeout = 400
)
$ErrorActionPreference = 'Stop'
$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$chattest = Join-Path $repo 'tools\net\chattest.py'
New-Item -ItemType Directory -Force $OutDir | Out-Null

$lines = Get-Content $Script | ForEach-Object {
    if ($_ -match '^alias w1s ') { 'alias w1s "w100;w100"' }
    elseif ($_ -match '^echo OMSHOT (\S+)') { "om_menushot $($Matches[1])"; 'w10' }
    else { $_ }
}
$adapted = Join-Path $OutDir "$Tag.cfg"
Set-Content $adapted -Value ($lines -join "`r`n")

foreach ($port in 16667, 16668) {
    if (Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue) {
        throw "port $port is already in use (a stale chattest.py?)"
    }
}
$helpers = @()
if ($Fake) {
    $log = Join-Path $OutDir "${Tag}_fake.txt"
    if (Test-Path $log) { Remove-Item $log -Force }
    $helpers += Start-Process -PassThru -WindowStyle Hidden python -ArgumentList @('-I', $chattest, 'fake', '--port', '16668',
        '--log', $log, '--timeout', ($Timeout + 30))
}
if ($Proxy) {
    $log = Join-Path $OutDir "${Tag}_proxy.txt"
    if (Test-Path $log) { Remove-Item $log -Force }
    $helpers += Start-Process -PassThru -WindowStyle Hidden python -ArgumentList @('-I', $chattest, 'proxy', '--port', '16667',
        '--upstream', $Upstream, '--log', $log, '--timeout', ($Timeout + 30), '--quiet-list')
}
if ($Peer) {
    $p = $Peer -split ','
    $log = Join-Path $OutDir "${Tag}_peer.txt"
    if (Test-Path $log) { Remove-Item $log -Force }
    $helpers += Start-Process -PassThru -WindowStyle Hidden python -ArgumentList @('-I', $chattest, 'peer', '--server', $Upstream,
        '--nick', $p[0], '--channel', $p[1], '--target', $p[2], '--log', $log, '--wait', $Timeout, '--linger', $Timeout)
}
Start-Sleep -Milliseconds 1500
try {
    & (Join-Path $repo 'engine\src\client\menu\test\run_om.ps1') -Exe $Exe -Data 'D:\Games\SoF-test-I2d' `
        -HomeDir $HomeDir -Script $adapted -Tag $Tag -OutDir $OutDir `
        -Timeout $Timeout -Extra "+set snd_dll OMsnd.dll +set clientport 29551 $Extra"
}
finally {
    Start-Sleep -Seconds 2
    # `python` may be the py.exe launcher: end the whole tree
    foreach ($h in $helpers) { if (-not $h.HasExited) { taskkill.exe /T /F /PID $h.Id 2>&1 | Out-Null } }
}
