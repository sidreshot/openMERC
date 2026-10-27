# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-004, SPEC-040 (black-box test sessions for OMsnd; no game code)
#
# Scripted network session: an original dedicated server (copy D:\Games\SoF-test-I5s2-srv, map -Map), the relay
# injproxy.py (changes the player-state music/ambient fields, adds svc_sound_info, rewrites configstrings, see
# its header), and the original client (D:\Games\SoF-test-I5s2) with the chosen sound backend connecting through
# the relay. Only the client's process tree is recorded (proccap.exe, process loopback).
#   -Backend defsnd | omsnd      (defsnd runs with the dsound.dll focus proxy, see dsfocus.c)
#   -Script  client console script (copied to user\omtest.cfg; it must `connect $omtest_server` itself)
#   -Proxy   arguments for injproxy.py, e.g. "--music 0:2,20:3 --info 30:2:1 --cs 13=TSR1"
#   -Tag     <OutDir>\<Tag>.wav, <Tag>.log (client sof.log), <Tag>.srv.log, <Tag>.proxy.jsonl
# Both games run on a separate, never shown desktop. The registry is not touched: the relay sets the client's
# userinfo cl_violence to 0 in its connect request, so the forced parental locks of this PC do not matter.
# The server runs with developer 0: with developer 1 the original dedicated server opens a hidden modal box.
# The client's own socket is bound to port 29612 (`clientport`): the default client port 28901 is shared by every
# SoF client on this PC, and other packages' servers kept sending to it; the original client accepts `challenge` and
# `client_connect` from any address, so one such packet once moved a test client to another package's server
# (run t1_omsnd_lat). After the run, the log is checked for a second `Serverdata` (warning HIJACKED).
# Do not put s_khz, s_volume or s_musicvolume on the command line: the original client then dies at start-up
# (access violation after 0.2 s, seen 2026-10-25). An 11 kHz run sets s_khz in its client script (snd_restart)
# and the relay sets it back to 22 before `quit`, because the cvar is archived.
param(
    [ValidateSet('defsnd', 'omsnd')][string]$Backend = 'defsnd',
    [Parameter(Mandatory = $true)][string]$Script,
    [Parameter(Mandatory = $true)][string]$Tag,
    [string]$Map = 'jpn3',
    [string]$Proxy = '',
    [string]$OutDir = 'C:\openmerc-data\i5s2\runs',
    [string]$Game = 'D:\Games\SoF-test-I5s2',
    [string]$ServerGame = 'D:\Games\SoF-test-I5s2-srv',
    [string]$Build = 'C:\openmerc\build-I5s2-x86',
    [string]$Extra = '',
    [string]$ServerExtra = '',
    [int]$Timeout = 300,
    [int]$ServerDelay = 6,
    [int]$Rate = 48000,
    [switch]$Wave
)
$ErrorActionPreference = 'Stop'
foreach ($d in $Game, $ServerGame) {
    $full = [IO.Path]::GetFullPath($d).TrimEnd('\')
    if ($full -like '*\SoF-oracle*' -or $full -like '*\Soldier of Fortune*' -or $full -notlike '*\SoF-test-I5s2*') {
        throw "refusing to run outside the I5s2 test copies: $full"
    }
}
New-Item -ItemType Directory -Force $OutDir | Out-Null
if (-not ('SndProc' -as [type])) { Add-Type -Path (Join-Path $PSScriptRoot 'SndRun.cs') }

$snd = @{ defsnd = 'defsnd.dll'; omsnd = 'OMsnd' }[$Backend]
if ($Backend -eq 'omsnd') {
    foreach ($f in 'OMsnd.dll', 'OpenAL32.dll', 'fmt.dll') { Copy-Item (Join-Path $Build $f) (Join-Path $Game $f) -Force }
}
$dsproxy = Join-Path $Game 'dsound.dll'
if ($Backend -eq 'defsnd') { Copy-Item (Join-Path $Build 'test\dsound.dll') $dsproxy -Force }
elseif (Test-Path $dsproxy) { Remove-Item $dsproxy -Force }

$user = Join-Path $Game 'user'
Copy-Item $Script (Join-Path $user 'omtest.cfg') -Force
$log = Join-Path $user 'sof.log'
if (Test-Path $log) { Remove-Item $log -Force }
$svlog = Join-Path $ServerGame 'user\sof.log'
if (Test-Path $svlog) { Remove-Item $svlog -Force }
$wav = Join-Path $OutDir "$Tag.wav"
$plog = Join-Path $OutDir "$Tag.proxy.jsonl"
foreach ($f in $wav, "$wav.txt", "$wav.stop", $plog, "$plog.stop") { if (Test-Path $f) { Remove-Item $f -Force } }

$svargv = "+set dedicated 1 +set developer 0 +set deathmatch 1 +set maxclients 4 +set port 29610 +set ip 127.0.0.1 +set logfile 3 " +
          "+set public 0 +set cheats 1 +set timelimit 0 +set fraglimit 0 $ServerExtra +map $Map"
$clargv = "+set vid_fullscreen 0 +set gl_mode 3 +set vid_xpos 0 +set vid_ypos 0 +set logfile 3 +set in_mouse 0 " +
          "+set in_joystick 0 +set snd_dll $snd +set s_om_focusmute 0 +set ip 127.0.0.1 +set port 29611 " +
          "+set clientport 29612 " +
          "+set omtest_server 127.0.0.1:29620 +set cl_maxfps 100 "
if ($Wave) {
    $alwav = Join-Path $OutDir "${Tag}_al.wav"
    if (Test-Path $alwav) { Remove-Item $alwav -Force }
    $clargv += "+set s_om_wavefile $alwav "
}
$clargv += "$Extra +exec omtest.cfg"

$sv = $null; $cl = $null; $px = $null; $cap = $null
try {
    Write-Host "server: SoF.exe $svargv"
    $sv = [SndProc]::Start('OMTestI5s2', (Join-Path $ServerGame 'SoF.exe'), $svargv, $ServerGame)
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt 40 -and -not $sv.HasExited()) {
        if ((Test-Path $svlog) -and (Select-String -Path $svlog -Pattern '^-{20,}$' -Quiet)) { break }
        Start-Sleep -Milliseconds 250
    }
    Start-Sleep -Seconds $ServerDelay
    $pargs = "-I `"$(Join-Path $PSScriptRoot 'injproxy.py')`" --listen 127.0.0.1:29620 --server 127.0.0.1:29610 " +
             "--out `"$plog`" --seconds $($Timeout + 30) $Proxy"
    Write-Host "proxy: python $pargs"
    $px = Start-Process -FilePath python -ArgumentList $pargs -PassThru -WindowStyle Hidden `
        -RedirectStandardError (Join-Path $OutDir "$Tag.proxy.err.txt")
    Start-Sleep -Milliseconds 800
    Write-Host "client: SoF.exe $clargv"
    $cl = [SndProc]::Start('OMTestI5s2', (Join-Path $Game 'SoF.exe'), $clargv, $Game)
    if (-not $Wave) {
        $cap = Start-Process -FilePath (Join-Path $Build 'test\proccap.exe') -ArgumentList "$($cl.Pid) `"$wav`" $Rate $($Timeout + 30)" `
            -PassThru -WindowStyle Hidden -RedirectStandardError (Join-Path $OutDir "$Tag.proccap.txt")
    }
    $sw.Restart()
    while ($sw.Elapsed.TotalSeconds -lt 40 -and -not $cl.HasExited()) {
        if ((Test-Path $log) -and (Select-String -Path $log -Pattern 'execing omtest' -Quiet)) { break }
        Start-Sleep -Milliseconds 200
    }
    $code = $cl.Wait($Timeout * 1000)
    Write-Host ("client exit code {0} after {1:N1} s{2}" -f $code, $sw.Elapsed.TotalSeconds, $(if ($code -eq -1) { ' (killed)' } else { '' }))
} finally {
    if ($cl -and -not $cl.HasExited()) { $cl.Wait(1) | Out-Null }
    if ($cap) {
        New-Item -ItemType File -Force "$wav.stop" | Out-Null
        if (-not $cap.WaitForExit(15000)) { Stop-Process -Id $cap.Id -Force }
        Remove-Item "$wav.stop" -Force -ErrorAction SilentlyContinue
    }
    if ($px) {
        New-Item -ItemType File -Force "$plog.stop" | Out-Null
        if (-not $px.WaitForExit(5000)) { Stop-Process -Id $px.Id -Force }
        Remove-Item "$plog.stop" -Force -ErrorAction SilentlyContinue
    }
    if ($sv -and -not $sv.HasExited()) { $sv.Wait(1) | Out-Null }
    if ($cl) { $cl.Wait(1) | Out-Null }
    if ($sv) { $sv.Wait(1) | Out-Null }
    Start-Sleep -Milliseconds 300
    if (Test-Path $log) { Copy-Item $log (Join-Path $OutDir "$Tag.log") -Force }
    if (Test-Path $svlog) { Copy-Item $svlog (Join-Path $OutDir "$Tag.srv.log") -Force }
    if (Test-Path $dsproxy) { Remove-Item $dsproxy -Force }
}
if (Test-Path "$OutDir\$Tag.proccap.txt") { Get-Content "$OutDir\$Tag.proccap.txt" | Write-Host }
$clog = Join-Path $OutDir "$Tag.log"
if (Test-Path $clog) {
    $sd = @(Select-String -Path $clog -Pattern 'Serverdata packet received').Count
    $foreign = @(Select-String -Path $clog -Pattern '^127\.0\.0\.1:(?!29620)\d+:').Count
    if ($sd -ne 1) { Write-Host "WARNING HIJACKED: $sd serverdata packets in $clog" }
    elseif ($foreign) { Write-Host "note: $foreign packets from foreign addresses in $clog" }
}
