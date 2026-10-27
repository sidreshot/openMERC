# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: test harness only (no game code). Runs the OpenMerc engine of a RelWithDebInfo tree under the
# console debugger cdb on a separate, never shown desktop (DeskRun.cs), from D:\Games\SoF-test-I1c\omdbg with
# -portable and a user dir under openmerc-data; on an access violation or heap corruption cdb writes the
# stack to <OutDir>\<Tag>.cdb.txt and quits. Only our own engine is debugged (never the original files).
#   run_omdbg.ps1 -Script tests\I1c\play_om.cfg -Tag dbg1 -Extra '+set snd_dll OMsnd.dll'
param(
    [string]$Script,
    [string]$Tag = 'dbg',
    [string]$Extra = '',
    [int]$Timeout = 180,
    [string]$Build = 'C:\openmerc\build-I1c-x86dbg\release',
    [string]$Game = 'D:\Games\SoF-test-I1c',
    [string]$User = 'C:\openmerc-data\I1c\home\user',
    [string]$OutDir = 'C:\openmerc-data\I1c',
    [string]$Cdb = 'C:\Program Files (x86)\Windows Kits\10\Debuggers\x86\cdb.exe'
)
$ErrorActionPreference = 'Stop'
if ($Game -notlike '*SoF-test-I1c*') { throw 'refusing to run outside the I1c test copy' }
$om = Join-Path $Game 'omdbg'
New-Item -ItemType Directory -Force $om, $OutDir, $User | Out-Null
Copy-Item (Join-Path $Build 'openMerc.exe') (Join-Path $om 'SoF.exe') -Force
foreach ($f in 'SDL3.dll', 'ref_gl3.dll') { Copy-Item (Join-Path $Build $f) (Join-Path $om $f) -Force }
foreach ($f in 'Defsnd.dll', 'EAXSnd.dll', 'A3Dsnd.dll', 'EaxMan.dll', 'eax.dll', 'a3dapi.dll') {
    $src = Join-Path $Game $f
    if (Test-Path $src) { Copy-Item $src (Join-Path $om $f) -Force }
}
if ($Script) { Copy-Item $Script (Join-Path $User 'omtest.cfg') -Force }
$log = Join-Path $OutDir "$Tag.cdb.txt"
if (Test-Path $log) { Remove-Item $log -Force }
$u = $User -replace '\\', '/'
$g = $Game -replace '\\', '/'
$eargs = "-portable -basedir $g +set user $u +set vid_fullscreen 0 +set in_grab 0 +set vid_renderer gl3 " +
         "+set gl_mode 3 +set r_mode 4 +set console 1 +set logfile 3 +set s_nosound 1 +set con_notifytime 0 $Extra"
if ($Script) { $eargs += ' +exec omtest.cfg' }
$sym = ($Build + ';' + $om)
$cmds = 'sxe -c \"r; kb 60; q\" c0000374; sxe -c \"r; kb 60; q\" av; sxe -c \"kb 60; q\" c0000409; sxd eh; g'
$cargs = "-G -lines -y `"$sym`" -logo `"$log`" -c `"$cmds`" `"$(Join-Path $om 'SoF.exe')`" $eargs"
Write-Host "cdb $cargs"
if (-not ('DeskRun' -as [type])) {
    Add-Type -Path 'C:\openmerc\engine\src\client\refresh\gl3\test\DeskRun.cs' -ReferencedAssemblies System.Drawing
}
$code = [DeskRun]::Run('OMTestI1cDbg', $Cdb, $cargs, $om, $Timeout * 1000, $null, $null, 'SDL_app', $null)
Write-Host "exit code $code"
Get-Process -Name SoF -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq (Join-Path $om 'SoF.exe') } |
    ForEach-Object { Stop-Process -Id $_.Id -Force }
