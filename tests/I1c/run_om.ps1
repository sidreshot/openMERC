# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-310 / SPEC-311 / SPEC-312 test harness (no game code)
#
# Runs the OpenMerc engine of the I1c build on a separate, never shown desktop (DeskRun.cs of the I4a
# harness: no focus, no input from the owner), from D:\Games\SoF-test-I1c\om with -portable, the test copy as
# basedir and a user dir under openmerc-data. The engine's own `omshot` / cl_omshotframes captures
# (<user>\scrnshot\*.tga, taken before the buffer swap) are converted to PNG as <OutDir>\<Tag>_<name>.png;
# <user>\sof.log is kept as <OutDir>\<Tag>.log.
#
# usage: run_om.ps1 -Script <cfg> [-Tag om] [-Extra "+set cl_omshotframes ..."] [-Timeout 120] [-NoCopy]
param(
    [string]$Script,
    [string]$Tag = 'om',
    [string]$Extra = '',
    [int]$Timeout = 120,
    [string]$Build = 'C:\openmerc\build-I1c-x86\release',
    [string]$Game = 'D:\Games\SoF-test-I1c',
    [string]$User = 'C:\openmerc-data\I1c\home\user',
    [string]$OutDir = 'C:\openmerc-data\shots\I1c',
    [switch]$NoCopy,
    # copy -Script as omtest.cfg but do not exec it at start (e.g. cl_omexecactive runs it later)
    [switch]$NoScriptExec,
    # I1c: install directory below the test copy (om64 for the x64 build)
    [string]$OmDir = 'om'
)
$ErrorActionPreference = 'Stop'
if ($Game -notlike '*SoF-test-I1c*') { throw 'refusing to run outside the I1c test copy' }
$om = Join-Path $Game $OmDir
New-Item -ItemType Directory -Force $om, $OutDir, $User | Out-Null

if (-not $NoCopy) {
    Copy-Item (Join-Path $Build 'openMerc.exe') (Join-Path $om 'SoF.exe') -Force
    # a renderer that failed to link (other package mid-edit) keeps the last good copy
    foreach ($f in 'SDL3.dll', 'ref_gl3.dll') {
        if (Test-Path (Join-Path $Build $f)) { Copy-Item (Join-Path $Build $f) (Join-Path $om $f) -Force }
        else { Write-Host "note: $f not built, keeping $om\$f" }
    }
    foreach ($f in 'Defsnd.dll', 'EAXSnd.dll', 'A3Dsnd.dll', 'EaxMan.dll', 'eax.dll', 'a3dapi.dll') {
        $src = Join-Path $Game $f
        if (Test-Path $src) { Copy-Item $src (Join-Path $om $f) -Force }
    }
}

# the script goes into the user dir (exec looks there first)
if ($Script) { Copy-Item $Script (Join-Path $User 'omtest.cfg') -Force }
$shotdir = Join-Path $User 'scrnshot'
if (Test-Path $shotdir) { Remove-Item (Join-Path $shotdir '*') -Force -ErrorAction SilentlyContinue }
$log = Join-Path $User 'sof.log'
if (Test-Path $log) { Remove-Item $log -Force }

$u = $User -replace '\\', '/'
$g = $Game -replace '\\', '/'
$argv = "-portable -basedir $g +set user $u +set vid_fullscreen 0 +set in_grab 0 +set vid_renderer gl3 " +
        "+set gl_mode 3 +set r_mode 4 +set console 1 +set logfile 3 +set s_nosound 1 +set con_notifytime 0 " +
        "+set port 29542 +set clientport 29541 $Extra"
if ($Script -and -not $NoScriptExec) { $argv += ' +exec omtest.cfg' }
Write-Host "SoF.exe (OpenMerc) $argv"

if (-not ('DeskRun' -as [type])) {
    Add-Type -Path 'C:\openmerc\engine\src\client\refresh\gl3\test\DeskRun.cs' -ReferencedAssemblies System.Drawing
}
$sw = [Diagnostics.Stopwatch]::StartNew()
$code = [DeskRun]::Run('OMTestI1c', (Join-Path $om 'SoF.exe'), $argv, $om, $Timeout * 1000, $null, $null, 'SDL_app', $null)
Write-Host ("exit code {0} after {1:N1} s{2}" -f $code, $sw.Elapsed.TotalSeconds, $(if ($code -eq -1) { ' (killed)' } else { '' }))

$n = 0
if (Test-Path $shotdir) {
    foreach ($t in Get-ChildItem (Join-Path $shotdir '*.tga')) {
        $png = Join-Path $OutDir ("{0}_{1}.png" -f $Tag, $t.BaseName)
        python -I -c "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])" $t.FullName $png
        $n++
    }
}
Write-Host "$n capture(s) -> $OutDir"
if (Test-Path $log) { Copy-Item $log (Join-Path $OutDir "$Tag.log") -Force }
