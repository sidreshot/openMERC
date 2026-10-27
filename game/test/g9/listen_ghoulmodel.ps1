# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G9 - SPEC-791 test vector 6 as a listen server (SPEC-AMEND-006 G9.13): the original SoF.exe in the G9
# test copy runs windowed on a separate, never shown desktop (I4a's DeskRun helper: the window can take neither the
# focus nor the owner's input), loads dm/jpndm2 with cheats and developer 1, spawns `ghoul_model` through the
# console script and quits; the log is searched for the `ghoul_model` developer prints.
# Implements: SPEC-791 Behaviour 14 (test vector 6)
#
# usage: listen_ghoulmodel.ps1 -Dll orig|om [-Timeout 120]
param(
    [string]$Dll = 'orig',
    [int]$Timeout = 120,
    # With a non-stock module the engine clears the "pure" bit of sv_violence, and on this PC the stored lock words
    # are invalid (client forced to cl_violence 63), so the local client is refused (testing notes). -Unlock
    # writes valid "off" lock words with I7's player/test/sof_locks.py only while the game starts up, then restores
    # the key byte-identically from I7's backup (verified before and after; restore also in `finally`).
    [switch]$Unlock,
    [string]$LockBackup = 'C:\openmerc-data\i7\sof-registry-backup.json'
)
$ErrorActionPreference = 'Stop'
$Game = 'D:\Games\SoF-test-G9'
$Data = 'C:\openmerc-data\g9'
if (-not (Test-Path (Join-Path $Game 'SoF.exe'))) { throw 'G9 test copy missing' }
$src = if ($Dll -eq 'orig') { Join-Path $Game 'base\gamex86.dll.orig' } else { Join-Path $Data 'dll\om.dll' }
Copy-Item $src (Join-Path $Game 'base\gamex86.dll') -Force
$tag = "listen_$Dll"
$user = Join-Path $Game "u9_$tag"
if (Test-Path $user) { Remove-Item $user -Recurse -Force }
New-Item -ItemType Directory -Force $user | Out-Null
# console script: developer through the config (on the command line it opens a modal box), the map, enough frames
# for the local client to enter, the spawn, 40 s of frames, quit. `wait` = one frame at cl_maxfps 30.
# The command buffer holds 8 KB, so the frame delays are nested aliases (w100 = 100 frames).
$w10 = (1..10 | ForEach-Object { 'wait' }) -join ';'
$w100 = (1..10 | ForEach-Object { 'w10' }) -join ';'
$cfg = @(
    'set developer 1', 'set cheats 1', 'set deathmatch 1', 'set sv_maplistfile ""',
    "alias w10 `"$w10`"", "alias w100 `"$w100`"", 'map dm/jpndm2',
    'w100;w100;w100;w100;w100', 'echo G9 spawning ghoul_model', 'cmd gimme ghoul_model',
    'w100;w100;w100;w100;w100;w100;w100;w100;w100;w100;w100;w100', 'echo G9 done', 'quit'
) -join "`n"
Set-Content -Path (Join-Path $Game 'base\g9_listen.cfg') -Value $cfg -Encoding ascii
$log = Join-Path $user 'sof.log'
# G9 ports (testing notes "Network ports"): server 29490, client 29491
$argv = "+set vid_fullscreen 0 +set gl_mode 3 +set logfile 2 +set snd_dll Defsnd.dll +set s_nosound 1 " +
        "+set cl_maxfps 30 +set port 29490 +set clientport 29491 +set user ./u9_$tag +exec g9_listen.cfg"
if (-not ('DeskRun' -as [type])) {
    Add-Type -Path 'C:\openmerc\engine\src\client\refresh\gl3\test\DeskRun.cs' `
        -ReferencedAssemblies System.Drawing
}
$shots = Join-Path $env:TEMP 'omg9_listen'
New-Item -ItemType Directory -Force $shots | Out-Null
$locks = 'C:\openmerc\player\test\sof_locks.py'
$restorer = $null
try {
    if ($Unlock) {
        & python -I $locks verify $LockBackup | Write-Host
        if ($LASTEXITCODE -ne 0) { throw "parental-lock key differs from $LockBackup - not touching it" }
        & python -I $locks unlock | Write-Host
        # restore as soon as the engine has read the locks (start-up finished)
        $restorer = Start-Job -ArgumentList $log, $locks, $LockBackup -ScriptBlock {
            param($log, $locks, $backup)
            $t = [Diagnostics.Stopwatch]::StartNew()
            while ($t.Elapsed.TotalSeconds -lt 60) {
                if ((Test-Path $log) -and (Select-String -Path $log -Pattern 'Soldier of Fortune Initialized' -Quiet)) { break }
                Start-Sleep -Milliseconds 200
            }
            & python -I $locks restore $backup
        }
    }
    $code = [DeskRun]::Run('OMTestG9', (Join-Path $Game 'SoF.exe'), $argv, $Game, $Timeout * 1000, $log, $shots, 'SoF', '')
} finally {
    if ($Unlock) {
        if ($restorer) { Receive-Job $restorer -Wait | Write-Host; Remove-Job $restorer }
        & python -I $locks restore $LockBackup | Write-Host
        & python -I $locks verify $LockBackup | Write-Host
        if ($LASTEXITCODE -ne 0) { Write-Warning 'parental-lock key NOT identical to the backup' }
    }
}
Write-Host ("exit code {0}{1}" -f $code, $(if ($code -eq -1) { ' (killed after timeout)' } else { '' }))
$out = Join-Path $Data $tag
New-Item -ItemType Directory -Force $out | Out-Null
if (Test-Path $log) {
    Copy-Item $log (Join-Path $out 'sof.log') -Force
    Select-String -Path $log -Pattern 'G9 spawning|G9 done|hooha|seq-end callback|entered the game|SpawnServer' |
        ForEach-Object { $_.Line }
} else { Write-Host 'no log' }
