# SPDX-License-Identifier: GPL-2.0-or-later
# Black-box run of the original SoF.exe in the menu test copy D:\Games\SoF-test-I6m (never the oracle itself).
# Uses the I4a test tools: the desktop runner DeskRun.cs (separate, never shown desktop: no focus, no input from
# the owner) and the SPEC-002 capture shim ref_omcap.dll (saves the presented frame when the script prints
# "OMSHOT <name>"). Captures are stored as -OutDir\<Tag>_<name>.png, the game log as <Tag>.log.
# Implements: SPEC-801/802/803 black-box checks (test harness, no game code)
param(
    [string]$Game = 'D:\Games\SoF-test-I6m',
    [string]$CapDll = 'C:\openmerc\build-I4a-x86\refcap\ref_omcap.dll',
    [string]$DeskRun = 'C:\openmerc\engine\src\client\refresh\gl3\test\DeskRun.cs',
    [Parameter(Mandatory = $true)][string]$Script,
    [string]$Tag = 'oracle',
    [string]$OutDir = 'C:\openmerc-data\shots\I6m',
    [string]$Mode = '3',
    [string]$Extra = '',
    [int]$Timeout = 300
)
$ErrorActionPreference = 'Stop'
if ($Game -notlike '*SoF-test-I6m*') { throw 'refusing to run outside the I6m test copy' }
New-Item -ItemType Directory -Force $OutDir | Out-Null
Copy-Item $CapDll (Join-Path $Game 'ref_omcap.dll') -Force
$user = Join-Path $Game 'user'
New-Item -ItemType Directory -Force $user | Out-Null
Copy-Item $Script (Join-Path $user 'omtest.cfg') -Force
$log = Join-Path $user 'sof.log'
if (Test-Path $log) { Remove-Item $log -Force }

$argv = "+set vid_ref omcap +set om_capref gl +set vid_fullscreen 0 +set gl_mode $Mode +set vid_xpos 0 +set vid_ypos 0 " +
        "+set logfile 3 +set console 1 +set snd_dll defsnd.dll +set s_nosound 1 $Extra +exec omtest.cfg"
Write-Host "SoF.exe $argv"
if (-not ('DeskRun' -as [type])) {
    Add-Type -Path $DeskRun -ReferencedAssemblies System.Drawing
}
$capdir = Join-Path $env:TEMP 'omi6m_cap'
New-Item -ItemType Directory -Force $capdir | Out-Null
Remove-Item (Join-Path $capdir '*') -Force -ErrorAction SilentlyContinue
$req = Join-Path $Game 'omshot.req'
if (Test-Path $req) { Remove-Item $req -Force }
$sw = [Diagnostics.Stopwatch]::StartNew()
$code = [DeskRun]::Run('OMTestI6m', (Join-Path $Game 'SoF.exe'), $argv, $Game, $Timeout * 1000, $log, $capdir, 'SoF', $req)
Write-Host ("exit code {0} after {1:N1} s{2}" -f $code, $sw.Elapsed.TotalSeconds, $(if ($code -eq -1) { ' (killed)' } else { '' }))
$n = 0
foreach ($c in Get-ChildItem (Join-Path $capdir '*.tga')) {
    $png = Join-Path $OutDir ("{0}_{1}.png" -f $Tag, $c.BaseName)
    python -I -c "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])" $c.FullName $png
    Remove-Item $c.FullName -Force
    $n++
}
Write-Host "$n capture(s) -> $OutDir"
if (Test-Path $log) { Copy-Item $log (Join-Path $OutDir "$Tag.log") -Force }
