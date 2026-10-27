# SPDX-License-Identifier: GPL-2.0-or-later
# GFX-modern black-box run: the OpenMerc client in the test copy D:\Games\OpenMerc-gfx-test (never the
# installed game), windowed at -Width x -Height on a separate, never shown desktop (DeskRun.cs: no focus, no
# input from the owner). The console script takes screenshots with "screenshot <name>" (read before the
# buffer swap) and measures with cl_showfps; the captures are stored as -OutDir\<Tag>_<name>.png and the
# console log as <Tag>.log (fps.py reads the "OMFPS <name>" sections of it).
# Implements: GFX-modern.md measurement (test harness, no game code)
param(
    [string]$Data = 'D:\Games\OpenMerc-gfx-test',
    [Parameter(Mandatory = $true)][string]$Script,
    [string]$Tag = 'gfx',
    [string]$OutDir = 'C:\openmerc-data\shots\gfx',
    [string]$Extra = '',
    [int]$Timeout = 300,
    [int]$Width = 1920,
    [int]$Height = 1080,
    [switch]$OwnerDesktop
)
$ErrorActionPreference = 'Stop'
if ($Data -notlike '*OpenMerc-gfx-test*') { throw 'refusing to run outside the gfx test copy' }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$user = Join-Path $Data 'user'
New-Item -ItemType Directory -Force $user | Out-Null
Remove-Item (Join-Path $user 's*.cfg') -Force -ErrorAction SilentlyContinue
if (Test-Path $Script -PathType Container) { Copy-Item (Join-Path $Script '*.cfg') $user -Force } else { Copy-Item $Script (Join-Path $user 'omtest.cfg') -Force }
$shots = Join-Path $user 'scrnshot'
if (Test-Path $shots) { Remove-Item (Join-Path $shots '*') -Force -ErrorAction SilentlyContinue }
$log = Join-Path $user 'sof.log'
if (Test-Path $log) { Remove-Item $log -Force }
$desk = 'OMTestGfx'
$pos = '+set vid_xpos 0 +set vid_ypos 0'
if ($OwnerDesktop) { $desk = ''; $pos = '+set vid_xpos -32003 +set vid_ypos -32026' }
$h = $Data -replace '\\', '/'
$u = $user -replace '\\', '/'
$argv = "-portable -basedir $h +set user $u +set vid_fullscreen 0 +set r_mode -1 +set r_customwidth $Width " +
        "+set r_customheight $Height $pos +set logfile 3 +set s_nosound 1 +set r_vsync 0 +set vid_maxfps 999 $Extra +exec omtest.cfg"
Write-Host "openMerc.exe $argv"
if (-not ('DeskRun' -as [type])) {
    Add-Type -Path (Join-Path $PSScriptRoot '..\DeskRun.cs') -ReferencedAssemblies System.Drawing
}
$sw = [Diagnostics.Stopwatch]::StartNew()
$code = [DeskRun]::Run($desk, (Join-Path $Data 'openMerc.exe'), $argv, $Data, $Timeout * 1000, $null, $null, 'SDL_app', $null)
Write-Host ("exit code {0} after {1:N1} s{2}" -f $code, $sw.Elapsed.TotalSeconds, $(if ($code -eq -1) { ' (killed)' } else { '' }))
$n = 0
if (Test-Path $shots) {
    foreach ($c in Get-ChildItem (Join-Path $shots '*.tga')) {
        $png = Join-Path $OutDir ("{0}_{1}.png" -f $Tag, $c.BaseName)
        python -I -c "import sys; from PIL import Image; Image.open(sys.argv[1]).convert('RGB').save(sys.argv[2])" $c.FullName $png
        $n++
    }
}
Write-Host "$n capture(s) -> $OutDir"
if (Test-Path $log) { Copy-Item $log (Join-Path $OutDir "$Tag.log") -Force }