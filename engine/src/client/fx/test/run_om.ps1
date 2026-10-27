# SPDX-License-Identifier: GPL-2.0-or-later
# Runs the OpenMerc client of the I5 build on a separate, never shown desktop (FxRun.cs), from
# D:\Games\SoF-test-I5\om (the engine is copied there as SoF.exe so the original sound modules bind their
# imports), -portable, with the test copy as basedir and a user dir under openmerc-data; waits until it
# quits (or kills it after -Timeout s) and keeps <user>\sof.log as -Out. Arguments follow the I6h harness.
# Example (in-engine self test):  run_om.ps1 -Extra '+om_fxselftest +quit' -Out selftest.log
# Implements: SPEC-400 / SPEC-401 test harness (no game code)
param(
    [string]$Build = 'C:\openmerc\build-I5-x86\release',
    [string]$Game = 'D:\Games\SoF-test-I5',
    [string]$User = 'C:\openmerc-data\i5\home\user',
    [string]$Extra = '+om_fxselftest +quit',
    [string]$Out = 'C:\openmerc-data\i5\om.log',
    [int]$Timeout = 90,
    # package I5's ports start at 29420 (testing notes "Network ports"); 29424 = OpenMerc alone
    [int]$Port = 29424,
    [switch]$NoCopy,
    [switch]$Background
)
$ErrorActionPreference = 'Stop'
if ($Game -notlike '*SoF-test-I5*') { throw 'refusing to run outside the I5 test copy' }
$om = Join-Path $Game 'om'
New-Item -ItemType Directory -Force $om, $User | Out-Null
if (-not $NoCopy) {
    Copy-Item (Join-Path $Build 'openMerc.exe') (Join-Path $om 'SoF.exe') -Force
    foreach ($f in 'SDL3.dll', 'ref_gl3.dll') {
        if (Test-Path (Join-Path $Build $f)) { Copy-Item (Join-Path $Build $f) (Join-Path $om $f) -Force }
    }
    foreach ($f in 'Defsnd.dll', 'EAXSnd.dll', 'A3Dsnd.dll', 'EaxMan.dll', 'eax.dll', 'a3dapi.dll') {
        $src = Join-Path $Game $f
        if (Test-Path $src) { Copy-Item $src (Join-Path $om $f) -Force }
    }
}
$log = Join-Path $User 'sof.log'
if (Test-Path $log) { Remove-Item $log -Force }
$u = $User -replace '\\', '/'
$g = $Game -replace '\\', '/'
$argv = "-portable -basedir $g +set user $u +set vid_fullscreen 0 +set in_grab 0 +set vid_renderer gl3 " +
        "+set gl_mode 3 +set r_mode 4 +set console 1 +set logfile 3 +set s_nosound 1 +set con_notifytime 0 " +
        "+set clientport $Port $Extra"
Write-Host "SoF.exe (OpenMerc) $argv"
if (-not ('FxRun' -as [type])) {
    Add-Type -Path (Join-Path $PSScriptRoot 'FxRun.cs') -ReferencedAssemblies System.Drawing
}
[FxRun]::OpenDesktop('OMTestI5')
try {
    $id = [FxRun]::Start((Join-Path $om 'SoF.exe'), $argv, $om)
    $code = [FxRun]::Wait($id, $Timeout * 1000)
    if ($code -eq -1) { Write-Host ('timeout: killed; windows: ' + [FxRun]::ListWindows($id)) }
    else { Write-Host "exit code $code" }
} finally {
    [FxRun]::KillAll()
}
if (Test-Path $log) { Copy-Item $log $Out -Force; Write-Host "log -> $Out" }
