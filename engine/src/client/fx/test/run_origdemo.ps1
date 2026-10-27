# SPDX-License-Identifier: GPL-2.0-or-later
# Black-box helper for package I5: the ORIGINAL client of the I5 test copy plays a demo on a separate, never
# shown desktop (FxRun.cs) with its renderer behind the ref_omcap capture shim; <Script> (in userA) starts the
# demo and prints `echo OMSHOT <name>` lines, each saved as <CapDir>\<name>.tga (the shim's om_capview gives
# the same fixed eye as the OpenMerc client's om_fxview). Used for the demo-based effect pairs (fxpairs.py
# --dense), because the original host quits with "Ghoul :Missing Child" when it fires a rocket while the
# OpenMerc client is connected.
#   run_origdemo.ps1 -Script dplay.cfg -CapDir D:\Games\SoF-test-I5\cap
# Implements: SPEC-400 / SPEC-401 test harness (no game code)
param(
    [string]$Script = 'dplay.cfg',
    [string]$CapDir = 'D:\Games\SoF-test-I5\cap',
    [string]$Game = 'D:\Games\SoF-test-I5',
    [string]$CapBuild = 'C:\openmerc\build-I5-x86\refcap',
    [string]$Out = 'C:\openmerc-data\i5\sessions\origdemo.log',
    [int]$Timeout = 400
)
$ErrorActionPreference = 'Stop'
$full = [IO.Path]::GetFullPath($Game).TrimEnd('\')
if ($full -ne 'D:\Games\SoF-test-I5') { throw "refusing to run outside D:\Games\SoF-test-I5 ($full)" }
if (-not ('FxRun' -as [type])) { Add-Type -Path (Join-Path $PSScriptRoot 'FxRun.cs') -ReferencedAssemblies System.Drawing }
New-Item -ItemType Directory -Force $CapDir | Out-Null
Remove-Item (Join-Path $CapDir '*') -Force -ErrorAction SilentlyContinue
Copy-Item (Join-Path $CapBuild 'ref_omcap.dll') (Join-Path $full 'ref_omcap.dll') -Force
$alog = Join-Path $full 'userA\sof.log'
if (Test-Path $alog) { Remove-Item $alog -Force }
$req = Join-Path $full 'omshot.req'
if (Test-Path $req) { Remove-Item $req -Force }
$exe = Join-Path $full 'SoF.exe'
$aargs = "+set user ./userA +set vid_ref omcap +set om_capref gl +set vid_fullscreen 0 +set gl_mode 3 +set vid_xpos 0 " +
        "+set vid_ypos 0 +set logfile 3 +set snd_dll OMsnd +set s_om_focusmute 0 +set cl_maxfps 100 " +
        "+set port 29425 +set clientport 29426 +exec $Script"
[FxRun]::OpenDesktop('OMTestI5')
try {
    $pid0 = [FxRun]::Start($exe, $aargs, $full)
    [FxRun]::Watch($alog, $CapDir, $req, $pid0, 'SoF')
    $code = [FxRun]::Wait($pid0, $Timeout * 1000)
    if ($code -eq -1) { Write-Host 'original client: timeout, killed' } else { Write-Host "original client exit $code" }
} finally {
    [FxRun]::KillAll()
}
if (Test-Path $alog) { Copy-Item $alog $Out -Force }
Write-Host ("{0} capture(s) in {1}" -f (Get-ChildItem (Join-Path $CapDir '*.tga')).Count, $CapDir)
