# SPDX-License-Identifier: GPL-2.0-or-later
# Black-box run of the ORIGINAL SoF.exe 1.07f in the I2d test copy D:\Games\SoF-test-I2d (never the oracle) with
# a console script. The game runs on a separate, never shown desktop (I4a's DeskRun.cs: no focus, no input from
# the owner) and renders through the capture shim ref_omcap.dll; a script line `echo OMSHOT <name>` saves the
# next presented frame as <OutDir>\<Tag>_<name>.png. The console log is copied to <OutDir>\<Tag>.log.
#   run_orig.ps1 -Script tests\I2d\orig_fake.cfg -Tag origfake
# Implements: SPEC-204 section 5, SPEC-803 section 3 (test harness, no game code)
param(
    [string]$Game = 'D:\Games\SoF-test-I2d',
    [string]$CapDll = 'C:\openmerc\build-I4a-x86\refcap\ref_omcap.dll',
    [Parameter(Mandatory = $true)][string]$Script,
    [string]$Tag = 'orig',
    [string]$OutDir = 'C:\openmerc-data\I2d\shots',
    [string]$Extra = '',
    [int]$Timeout = 300
)
$ErrorActionPreference = 'Stop'
$full = [IO.Path]::GetFullPath($Game).TrimEnd('\')
if ($full -ne 'D:\Games\SoF-test-I2d') { throw "refusing to run outside D:\Games\SoF-test-I2d ($full)" }
$deskRun = Join-Path $PSScriptRoot '..\..\engine\src\client\refresh\gl3\test\DeskRun.cs'
if (-not ('DeskRun' -as [type])) { Add-Type -Path $deskRun -ReferencedAssemblies System.Drawing }
New-Item -ItemType Directory -Force $OutDir | Out-Null
Copy-Item $CapDll (Join-Path $full 'ref_omcap.dll') -Force
$user = Join-Path $full 'user'
New-Item -ItemType Directory -Force $user | Out-Null
Copy-Item $Script (Join-Path $user 'omtest.cfg') -Force
$log = Join-Path $user 'sof.log'
if (Test-Path $log) { Remove-Item $log -Force }
$argv = "+set vid_ref omcap +set om_capref gl +set vid_fullscreen 0 +set gl_mode 3 +set vid_xpos 0 +set vid_ypos 0 " +
        "+set logfile 3 +set console 1 +set snd_dll defsnd.dll +set s_nosound 1 +set clientport 29551 $Extra +exec omtest.cfg"
Write-Host "SoF.exe $argv"
$capdir = Join-Path $env:TEMP 'omi2d_cap'
New-Item -ItemType Directory -Force $capdir | Out-Null
Remove-Item (Join-Path $capdir '*') -Force -ErrorAction SilentlyContinue
$req = Join-Path $full 'omshot.req'
if (Test-Path $req) { Remove-Item $req -Force }
$sw = [Diagnostics.Stopwatch]::StartNew()
$code = [DeskRun]::Run('OMTestI2d', (Join-Path $full 'SoF.exe'), $argv, $full, $Timeout * 1000, $log, $capdir, 'SoF', $req)
Write-Host ("exit code {0} after {1:N1} s{2}" -f $code, $sw.Elapsed.TotalSeconds, $(if ($code -eq -1) { ' (killed)' } else { '' }))
$n = 0
foreach ($c in Get-ChildItem (Join-Path $capdir '*.tga')) {
    $png = Join-Path $OutDir ("{0}_{1}.png" -f $Tag, $c.BaseName)
    python -I -c "import sys; from PIL import Image; Image.open(sys.argv[1]).convert('RGB').save(sys.argv[2])" $c.FullName $png
    Remove-Item $c.FullName -Force
    $n++
}
Write-Host "$n capture(s) -> $OutDir"
if (Test-Path $log) { Copy-Item $log (Join-Path $OutDir "$Tag.log") -Force }
