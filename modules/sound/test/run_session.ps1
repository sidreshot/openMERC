# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-004, SPEC-040 (black-box test sessions for OMsnd; no game code)
#
# One scripted session of the original SoF.exe 1.07f in the test copy D:\Games\SoF-test-I5s2 with a chosen
# sound backend, recording the audio of the game process only (proccap.exe, process loopback).
#   -Backend defsnd | omsnd | eaxsnd | a3dsnd     (omsnd: OMsnd.dll + OpenAL32.dll + fmt.dll from -Build)
#   -Script  console script (copied to user\omtest.cfg and run with +exec omtest.cfg)
#   -Tag     name of the run: <OutDir>\<Tag>.wav (+ .wav.txt), <Tag>.log (sof.log)
#   -Wave    OMsnd only: let OpenAL Soft write the output to <OutDir>\<Tag>_al.wav instead of a device
#            (digital copy of the mix; the process then plays nothing, so no loopback file is made)
# The game runs on a separate, never shown desktop (no focus, no input from the owner, nothing on screen).
# Every local game of the original on this PC is refused (`rejected_violence`) because the parental-lock words in
# HKCU\Software\Raven Software\SoF were made on another machine (docs/impl/I7.md). As in I7, valid "off" words are
# written only while the game starts (player\test\sof_locks.py unlock) and the key is restored byte-identically from
# the I7 backup as soon as the script runs; the key is checked against the backup before and after.
# Only the processes started here are stopped. The oracle and the main install are never touched.
param(
    [ValidateSet('defsnd', 'omsnd', 'eaxsnd', 'a3dsnd')][string]$Backend = 'defsnd',
    [Parameter(Mandatory = $true)][string]$Script,
    [Parameter(Mandatory = $true)][string]$Tag,
    [string]$OutDir = 'C:\openmerc-data\i5s2\runs',
    [string]$Game = 'D:\Games\SoF-test-I5s2',
    [string]$Build = 'C:\openmerc\build-I5s2-x86',
    [string]$Extra = '',
    [int]$Timeout = 300,
    [int]$Rate = 48000,
    [switch]$Wave,
    [switch]$OwnerDesktop,
    [switch]$NoUnlock,
    [switch]$DsFocus,
    [string]$LockBackup = 'C:\openmerc-data\i7\sof-registry-backup.json'
)
$ErrorActionPreference = 'Stop'
$full = [IO.Path]::GetFullPath($Game).TrimEnd('\')
if ($full -like '*\SoF-oracle*' -or $full -like '*\Soldier of Fortune*' -or $full -notlike '*\SoF-test-I5s2*') {
    throw "refusing to run outside the I5s2 test copy: $full"
}
New-Item -ItemType Directory -Force $OutDir | Out-Null
$locks = 'C:\openmerc\player\test\sof_locks.py'
if (-not ('SndProc' -as [type])) { Add-Type -Path (Join-Path $PSScriptRoot 'SndRun.cs') }

# sound backend files
$snd = @{ defsnd = 'defsnd.dll'; omsnd = 'OMsnd'; eaxsnd = 'eaxsnd.dll'; a3dsnd = 'a3dsnd.dll' }[$Backend]
if ($Backend -eq 'omsnd') {
    foreach ($f in 'OMsnd.dll', 'OpenAL32.dll', 'fmt.dll') { Copy-Item (Join-Path $Build $f) (Join-Path $Game $f) -Force }
}
$proxy = Join-Path $Game 'dsound.dll'
if ($DsFocus) { Copy-Item (Join-Path $Build 'test\dsound.dll') $proxy -Force }
elseif (Test-Path $proxy) { Remove-Item $proxy -Force }

$user = Join-Path $Game 'user'
Copy-Item $Script (Join-Path $user 'omtest.cfg') -Force
$log = Join-Path $user 'sof.log'
if (Test-Path $log) { Remove-Item $log -Force }
$wav = Join-Path $OutDir "$Tag.wav"
foreach ($f in $wav, "$wav.txt", "$wav.stop") { if (Test-Path $f) { Remove-Item $f -Force } }

$argv = "+set vid_fullscreen 0 +set gl_mode 3 +set vid_xpos 0 +set vid_ypos 0 +set logfile 3 +set in_mouse 0 " +
        "+set in_joystick 0 +set snd_dll $snd +set s_om_focusmute 0 +set cheats 1 "
if ($Wave) {
    $alwav = Join-Path $OutDir "${Tag}_al.wav"
    if (Test-Path $alwav) { Remove-Item $alwav -Force }
    $argv += "+set s_om_wavefile $alwav "
}
$argv += "$Extra +exec omtest.cfg"

function Restore-Locks {
    if ($NoUnlock) { return }
    & python -I $locks restore $LockBackup | Write-Host
    if ($LASTEXITCODE -ne 0) { Write-Warning 'parental-lock key NOT identical to the backup' }
}
if (-not $NoUnlock) {
    & python -I $locks verify $LockBackup | Write-Host
    if ($LASTEXITCODE -ne 0) { throw "parental-lock key differs from $LockBackup - not touching it" }
}
$cap = $null
$gp = $null
$restored = $false
try {
    if (-not $NoUnlock) { & python -I $locks unlock | Write-Host }
    $desk = $(if ($OwnerDesktop) { '' } else { 'OMTestI5s2' })
    Write-Host "SoF.exe $argv"
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $gp = [SndProc]::Start($desk, (Join-Path $Game 'SoF.exe'), $argv, $Game)
    $procId = $gp.Pid
    if (-not $Wave) {
        $cap = Start-Process -FilePath (Join-Path $Build 'test\proccap.exe') -ArgumentList "$procId `"$wav`" $Rate $($Timeout + 30)" `
            -PassThru -WindowStyle Hidden -RedirectStandardError (Join-Path $OutDir "$Tag.proccap.txt")
    }
    # restore the lock words as soon as the game has read them (the script starts after start-up)
    while ($sw.Elapsed.TotalSeconds -lt 40 -and -not $gp.HasExited()) {
        if ((Test-Path $log) -and (Select-String -Path $log -Pattern 'execing omtest' -Quiet)) { break }
        Start-Sleep -Milliseconds 200
    }
    Restore-Locks
    $restored = $true
    $left = [Math]::Max(1, $Timeout - [int]$sw.Elapsed.TotalSeconds)
    $code = $gp.Wait($left * 1000)
    Write-Host ("game exit code {0} after {1:N1} s{2}" -f $code, $sw.Elapsed.TotalSeconds, $(if ($code -eq -1) { ' (killed)' } else { '' }))
} finally {
    if (-not $restored) { Restore-Locks }
    if ($gp -and -not $gp.HasExited()) { $gp.Wait(1) | Out-Null }
    if ($cap) {
        New-Item -ItemType File -Force "$wav.stop" | Out-Null
        if (-not $cap.WaitForExit(15000)) { Stop-Process -Id $cap.Id -Force }
        Remove-Item "$wav.stop" -Force -ErrorAction SilentlyContinue
    }
    Start-Sleep -Milliseconds 300
    if (Test-Path $log) { Copy-Item $log (Join-Path $OutDir "$Tag.log") -Force }
    if (Test-Path $proxy) { Remove-Item $proxy -Force }
}
if (Test-Path "$OutDir\$Tag.proccap.txt") { Get-Content "$OutDir\$Tag.proccap.txt" | Write-Host }
