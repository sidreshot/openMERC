# SPDX-License-Identifier: GPL-2.0-or-later
# Black-box run of the ORIGINAL SoF.exe 1.07f as a LISTEN server (local player = client 1) in a bot test copy, with
# a chosen gamex86.dll (original or OpenMerc) and a console script. Waypoint edit mode needs a listen server with
# cheats and the local player alone (SPEC-AMEND-005 G5.12), so the bot editor commands (bot_wp_whichpath,
# bot_wp_pathinfo, bot_wp_botroute, makewp ...) can only be compared this way. The game runs on a separate, never shown
# desktop (DeskRun.cs: no focus, no input from the owner) through I4a's capture shim; the console log is copied to
# <OutDir>\<Tag>.log. Optionally G18's headless client joins as a second human at -JoinAt seconds.
#   run_listen.ps1 -Dll om -Script game\src\bots\test\listen_edit.cfg -Tag om_edit -Extra '+set bot_wp_edit 1'
# Implements: SPEC-741 (test harness, no game code); SPEC-AMEND-005 G5.7, G5.12 verification
param(
    [string]$Game = 'D:\Games\SoF-test-F5',
    [string]$Dll = 'orig',
    [string]$OmDll = 'C:\openmerc\build-F5-game-x86\gamex86.dll',
    [string]$CapDll = 'C:\openmerc\build-I4a-x86\refcap\ref_omcap.dll',
    [Parameter(Mandatory = $true)][string]$Script,
    [string]$Tag = 'listen',
    [string]$OutDir = 'C:\openmerc-data\F5\listen',
    [string]$Map = 'dm/jpndm2',
    [string]$Extra = '',
    [int]$Port = 29440,
    [int]$JoinAt = 0,
    [int]$JoinSeconds = 20,
    [int]$Timeout = 240
)
$ErrorActionPreference = 'Stop'
$full = [IO.Path]::GetFullPath($Game).TrimEnd('\')
if ($full -eq 'D:\Games\SoF-oracle') { throw "refusing to run in the oracle" }
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..'))
$deskRun = Join-Path $repo 'engine\src\client\refresh\gl3\test\DeskRun.cs'
if (-not ('DeskRun' -as [type])) { Add-Type -Path $deskRun -ReferencedAssemblies System.Drawing }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$src = if ($Dll -eq 'orig') { Join-Path $full 'base\gamex86.dll.orig' } elseif ($Dll -eq 'om') { $OmDll } else { $Dll }
Copy-Item $src (Join-Path $full 'base\gamex86.dll') -Force
Copy-Item $CapDll (Join-Path $full 'ref_omcap.dll') -Force
$user = Join-Path $full 'user'
New-Item -ItemType Directory -Force $user | Out-Null
Copy-Item $Script (Join-Path $user 'omtest.cfg') -Force
$log = Join-Path $user 'sof.log'
if (Test-Path $log) { Remove-Item $log -Force }
# package ports (testing notes "Network ports": F5 base 29440, server = base, client = base + 1); the rest of
# the setup goes through an exec file (the original crashes with more than 50 command-line words); -Extra is a list
# of "+set name value" groups, written as cfg lines before the map starts (cheats is latched)
$setup = @('set rcon_password f5', 'set deathmatch 1', 'set maxclients 8')
foreach ($part in ($Extra -split '\+') | Where-Object { $_.Trim() }) { $setup += $part.Trim() }
$setup += "map $Map"
$setup += 'exec omtest.cfg'
Set-Content -Path (Join-Path $user 'f5setup.cfg') -Value $setup -Encoding Ascii
$argv = "+set vid_ref omcap +set om_capref gl +set vid_fullscreen 0 +set gl_mode 3 " +
        "+set logfile 3 +set console 1 +set snd_dll defsnd.dll +set s_nosound 1 +set port $Port " +
        "+set clientport $($Port + 1) +exec f5setup.cfg"
Write-Host "SoF.exe $argv  (gamex86 from $src)"
$capdir = Join-Path $env:TEMP 'omf5_cap'
New-Item -ItemType Directory -Force $capdir | Out-Null
$joiner = $null
if ($JoinAt -gt 0) {
    $cap = Join-Path $OutDir "$Tag.join.jsonl"
    $clog = Join-Path $OutDir "$Tag.join.log"
    $py = "import time,subprocess,sys; time.sleep($JoinAt); subprocess.run([sys.executable, r'$repo\game\src\bots\test\bottest.py', 'client', '--server', '127.0.0.1:$Port', '--seconds', '$JoinSeconds', '--out', r'$cap', '--log', r'$clog', '--name', 'F5second'])"
    $joiner = Start-Process -FilePath python -ArgumentList @('-c', "`"$py`"") -WindowStyle Hidden -PassThru
}
$sw = [Diagnostics.Stopwatch]::StartNew()
$code = [DeskRun]::Run('OMTestF5', (Join-Path $full 'SoF.exe'), $argv, $full, $Timeout * 1000, $log, $capdir, 'SoF', (Join-Path $full 'omshot.req'))
Write-Host ("exit code {0} after {1:N1} s{2}" -f $code, $sw.Elapsed.TotalSeconds, $(if ($code -eq -1) { ' (killed)' } else { '' }))
if ($joiner) {
    if (-not $joiner.HasExited) { $joiner.WaitForExit(30000) | Out-Null }
    if (-not $joiner.HasExited) { & taskkill /T /F /PID $joiner.Id | Out-Null }
}
Remove-Item (Join-Path $capdir '*') -Force -ErrorAction SilentlyContinue
if (Test-Path $log) { Copy-Item $log (Join-Path $OutDir "$Tag.log") -Force }
