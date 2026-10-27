# SPDX-License-Identifier: GPL-2.0-or-later
# Runs the OpenMerc client (-portable, data from the menu test copy D:\Games\SoF-test-I6m, user directory under
# openmerc-data) with a console script on a separate, never shown desktop (DeskRun.cs of the I4a harness: no
# focus, no input from the owner). The script captures menu frames with "om_menushot <name>"; the captures are
# stored as -OutDir\<Tag>_<name>.png and the console log as <Tag>.log.
# Implements: SPEC-801/802/803 black-box checks (test harness, no game code)
param(
    [string]$Exe = 'C:\openmerc\build-I6m-x86\release\openMerc.exe',
    [string]$Data = 'D:\Games\SoF-test-I6m',
    [string]$HomeDir = 'C:\openmerc-data\I6m\home',
    [string]$DeskRun = 'C:\openmerc\engine\src\client\refresh\gl3\test\DeskRun.cs',
    [Parameter(Mandatory = $true)][string]$Script,
    [string]$Tag = 'om',
    [string]$OutDir = 'C:\openmerc-data\shots\I6m',
    [string]$Extra = '',
    # console commands put in front of the script (';'-separated)
    [string]$Pre = '',
    [int]$Timeout = 300,
    [int]$Width = 640,
    [int]$Height = 480,
    [switch]$OwnerDesktop
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $OutDir | Out-Null
$user = Join-Path $HomeDir 'user'
New-Item -ItemType Directory -Force $user | Out-Null
# settings that need not be on the command line (the engine accepts a limited number of arguments)
# (no vid_gamma here: `vid_gamma 1` made every SoF picture flat - F5, I4c finding 2026-10-25)
$prelines = @('set developer 0') + ($Pre -split ';' | Where-Object { $_ })
Set-Content (Join-Path $user 'omtest.cfg') -Value (($prelines + (Get-Content $Script)) -join "`r`n") -NoNewline
$shots = Join-Path $user 'menushots'
if (Test-Path $shots) { Remove-Item (Join-Path $shots '*') -Force -ErrorAction SilentlyContinue }
foreach ($l in @('sof.log', 'qconsole.log')) { $p = Join-Path $user $l; if (Test-Path $p) { Remove-Item $p -Force } }

$desk = 'OMTestI6m'
$pos = ''
if ($OwnerDesktop) { $desk = ''; $pos = '+set vid_xpos -32003 +set vid_ypos -32026' }
# always -portable with the home under openmerc-data (never the owner's Documents\openMERC)
$argv = "-portable -basedir $Data -user $user +set vid_fullscreen 0 +set r_mode -1 +set r_customwidth $Width +set r_customheight $Height $pos " +
        "+set logfile 3 +set console 1 +set s_nosound 1 $Extra +exec omtest.cfg"
Write-Host "$Exe $argv"
if (-not ('DeskRun' -as [type])) {
    Add-Type -Path $DeskRun -ReferencedAssemblies System.Drawing
}
$sw = [Diagnostics.Stopwatch]::StartNew()
$exeDir = Split-Path $Exe
# working directory = the data copy, so the sound module (defsnd.dll) is found like the original's
$code = [DeskRun]::Run($desk, $Exe, $argv, $Data, $Timeout * 1000, $null, $null, 'SDL_app', $null)
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
foreach ($l in @('sof.log', 'qconsole.log')) {
    $p = Join-Path $user $l
    if (Test-Path $p) { Copy-Item $p (Join-Path $OutDir "$Tag.log") -Force }
}
