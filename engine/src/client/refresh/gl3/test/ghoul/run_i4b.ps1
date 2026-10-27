# SPDX-License-Identifier: GPL-2.0-or-later
# I4b copy of the I4a harness (../run_sof.ps1): black-box run of the original SoF.exe in the test copy
# renderer (-Ref gl) or the OpenMerc renderer module (-Ref omgl: ref_omgl.dll copied from -Build).
# The renderer is loaded through the capture shim ref_omcap.dll (refcap/, written from SPEC-002), which
# saves the presented frame whenever the script prints "OMSHOT <name>" (see DeskRun.cs).
# Writes the console script into <game>\user, starts the game windowed on a separate, never shown desktop
# (the window cannot take the focus and gets no input from the owner), waits for it to quit (or kills it
# after -Timeout s), stores the captures as -OutDir\<Tag>_<name>.png, the renderer's own screenshots
# (user\scrnshot\*.tga) as <Tag>_<n>.png and user\sof.log as <Tag>.log.
# Implements: SPEC-002 (test vectors), SPEC-411 (screenshot comparison of Ghoul entities)
param(
    [string]$Game = 'D:\Games\SoF-test-I4b',
    [string]$Build = 'C:\openmerc\build-I4b-x86\release',
    [string]$CapBuild = 'C:\openmerc\build-I4b-x86\refcap',
    [string]$Script,
    [string]$Ref = 'gl',
    [string]$Tag = 'shot',
    [string]$OutDir = 'C:\openmerc-data\shots\I4b',
    [string]$Extra = '',
    [int]$Timeout = 120,
    [switch]$KeepTga,
    # run on the owner's desktop instead, with the window placed off-screen (no focus forcing either way)
    [switch]$OwnerDesktop
)
$ErrorActionPreference = 'Stop'
if ($Game -notlike '*SoF-test-I4b*') { throw 'refusing to run outside the I4b test copy' }
New-Item -ItemType Directory -Force $OutDir | Out-Null
Copy-Item (Join-Path $CapBuild 'ref_omcap.dll') (Join-Path $Game 'ref_omcap.dll') -Force
if ($Ref -ne 'gl') {
    Copy-Item (Join-Path $Build "ref_$Ref.dll") (Join-Path $Game "ref_$Ref.dll") -Force
}
$user = Join-Path $Game 'user'
$shotdir = Join-Path $user 'scrnshot'
if (Test-Path $shotdir) { Remove-Item (Join-Path $shotdir '*') -Force -ErrorAction SilentlyContinue }
Copy-Item $Script (Join-Path $user 'omtest.cfg') -Force
$log = Join-Path $user 'sof.log'
if (Test-Path $log) { Remove-Item $log -Force }

$desk = 'OMTestI4b'
$pos = '+set vid_xpos 0 +set vid_ypos 0'
if ($OwnerDesktop) { $desk = ''; $pos = '+set vid_xpos -32003 +set vid_ypos -32026' }
$argv = "+set vid_ref omcap +set om_capref $Ref +set vid_fullscreen 0 +set gl_mode 3 $pos " +
        "+set logfile 3 +set snd_dll defsnd.dll +set s_nosound 1 +set cl_maxfps 30 $Extra +exec omtest.cfg"
Write-Host "SoF.exe $argv"
if (-not ('DeskRun' -as [type])) {
    Add-Type -Path (Join-Path $PSScriptRoot '..\DeskRun.cs') -ReferencedAssemblies System.Drawing
}
$capdir = Join-Path $env:TEMP 'omi4b_cap'
New-Item -ItemType Directory -Force $capdir | Out-Null
Remove-Item (Join-Path $capdir '*') -Force -ErrorAction SilentlyContinue
$req = Join-Path $Game 'omshot.req'
if (Test-Path $req) { Remove-Item $req -Force }
$sw = [Diagnostics.Stopwatch]::StartNew()
$code = [DeskRun]::Run($desk, (Join-Path $Game 'SoF.exe'), $argv, $Game, $Timeout * 1000, $log, $capdir, 'SoF', $req)
Write-Host ("exit code {0} after {1:N1} s{2}" -f $code, $sw.Elapsed.TotalSeconds, $(if ($code -eq -1) { ' (killed)' } else { '' }))
$n = 0
foreach ($c in Get-ChildItem (Join-Path $capdir '*.tga')) {
    $png = Join-Path $OutDir ("{0}_{1}.png" -f $Tag, $c.BaseName)
    python -I -c "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])" $c.FullName $png
    Remove-Item $c.FullName -Force
    $n++
}
Write-Host "$n capture(s) -> $OutDir"
foreach ($c in Get-ChildItem (Join-Path $capdir '*.txt')) {
    Move-Item $c.FullName (Join-Path $OutDir ("{0}_{1}.trace.txt" -f $Tag, $c.BaseName)) -Force
}
$tr = Join-Path $Game 'omtrace.req'
if (Test-Path $tr) { Remove-Item $tr -Force }
if (Test-Path $log) { Copy-Item $log (Join-Path $OutDir "$Tag.log") -Force }
if (Test-Path $shotdir) {
    $tgas = @(Get-ChildItem (Join-Path $shotdir 'sof*.tga') | Sort-Object Name)
    $i = 0
    foreach ($t in $tgas) {
        $png = Join-Path $OutDir ("{0}_{1}.png" -f $Tag, $i)
        python -I -c "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])" $t.FullName $png
        if ($KeepTga) { Copy-Item $t.FullName (Join-Path $OutDir ("{0}_{1}.tga" -f $Tag, $i)) -Force }
        $i++
    }
    Write-Host "$i screenshot(s) -> $OutDir"
}
