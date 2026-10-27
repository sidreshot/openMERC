# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc X1 - assemble a copy of the game that runs only OpenMerc code: the paks are hard-linked from a
# reference install (never written), every executable and DLL comes from the OpenMerc build trees.
#   assemble.ps1 [-Dest D:\Games\SoF-open-X1] [-Paks D:\Games\SoF-oracle\base]
#   assemble.ps1 -Dest D:\Games\SoF-test-X1 -OrigModules   (comparison copy: original SoF.exe/DLLs copied, our
#                engine in om\, the ORIGINAL base\gamex86.dll and base\player.dll - "original modules on our engine")
# Layout of the open copy:
#   <Dest>\openMerc.exe, openMerc-ded.exe        engine (client+listen server / dedicated server)  build-X1-engine\release
#   <Dest>\ref_gl3.dll, SDL3.dll         renderer (vid_renderer gl3) and its window layer   build-X1-engine\release
#   <Dest>\ref_omgl.dll                  the SoF-ABI renderer for the ORIGINAL SoF.exe (vid_ref omgl); unused here
#   <Dest>\OMsnd.dll, OpenAL32.dll, fmt.dll   sound module (snd_dll OMsnd.dll) + OpenAL Soft runtime  build-X1-sound
#   <Dest>\base\gamex86.dll              game module                                       build-X1-game
#   <Dest>\base\player.dll               player module                                     build-X1-player
#   <Dest>\base\*.pak                    hard links to the reference paks (pak0-3, zmain, zsond, gs)
#   <Dest>\base\zz_openmerc.pak          OpenMerc's own data (menu pages, strings; data\README.md)  <EngineBuild>
# Implements: packaging (no game code)
param(
    [string]$Dest = 'D:\Games\SoF-open-X1',
    [string]$Paks = 'D:\Games\SoF-oracle\base',
    [string]$Repo = 'C:\openmerc',
    [string]$EngineBuild = 'build-X1-engine\release',
    [string]$SoundBuild = 'build-X1-sound',
    [string]$GameBuild = 'build-X1-game',
    [string]$PlayerBuild = 'build-X1-player',
    [switch]$OrigModules
)
$ErrorActionPreference = 'Stop'
$oracle = Split-Path $Paks -Parent
if ((Resolve-Path $oracle -ErrorAction SilentlyContinue) -and ($Dest.TrimEnd('\') -eq $oracle.TrimEnd('\'))) {
    throw "refusing to write into the reference install $oracle"
}
New-Item -ItemType Directory -Force (Join-Path $Dest 'base') | Out-Null
foreach ($p in 'gs.pak', 'pak0.pak', 'pak1.pak', 'pak2.pak', 'pak3.pak', 'zmain.pak', 'zsond.pak') {
    $t = Join-Path $Dest "base\$p"
    if (-not (Test-Path $t)) { New-Item -ItemType HardLink -Path $t -Target (Join-Path $Paks $p) | Out-Null }
}
$bin = if ($OrigModules) { Join-Path $Dest 'om' } else { $Dest }
New-Item -ItemType Directory -Force $bin | Out-Null
$e = Join-Path $Repo $EngineBuild
foreach ($f in 'openMerc.exe', 'openMerc-ded.exe', 'ref_gl3.dll', 'ref_omgl.dll', 'SDL3.dll') { Copy-Item (Join-Path $e $f) $bin -Force }
$s = Join-Path $Repo $SoundBuild
foreach ($f in 'OMsnd.dll', 'OpenAL32.dll', 'fmt.dll') { Copy-Item (Join-Path $s $f) $bin -Force }
# OpenMerc's own data archive (V1, data\README.md): ours, so it goes into both copies
$pak = Join-Path $e 'zz_openmerc.pak'
if (Test-Path $pak) { Copy-Item $pak (Join-Path $Dest 'base\zz_openmerc.pak') -Force }
else { Write-Warning "no $pak (run data\build.cmd); the stock video page will be used" }
if ($OrigModules) {
    foreach ($f in Get-ChildItem $oracle -File) {
        if ($f.Extension -in '.exe', '.dll') { Copy-Item $f.FullName $Dest -Force }
    }
    foreach ($f in 'gamex86.dll', 'player.dll') { Copy-Item (Join-Path $Paks $f) (Join-Path $Dest "base\$f") -Force }
} else {
    Copy-Item (Join-Path $Repo "$GameBuild\gamex86.dll") (Join-Path $Dest 'base\gamex86.dll') -Force
    Copy-Item (Join-Path $Repo "$PlayerBuild\player.dll") (Join-Path $Dest 'base\player.dll') -Force
    # nothing of the original program may be in the open copy
    $foreign = @(Get-ChildItem $Dest -Recurse -File | Where-Object { $_.Name -match '^(SoF\.exe|ref_gl\.dll|defsnd\.dll|eaxsnd\.dll|a3dsnd\.dll|eaxman\.dll|eax\.dll|a3dapi\.dll|spcl\.dll|FFC1[01]\.dll)$' })
    if ($foreign) { throw ('original files in the open copy: ' + ($foreign.FullName -join ', ')) }
}
Get-ChildItem $Dest -Recurse -File | Where-Object { $_.Extension -in '.exe', '.dll' } |
    ForEach-Object { '{0,-40} {1,9} {2}' -f $_.FullName.Substring($Dest.Length + 1), $_.Length, $_.LastWriteTime.ToString('yyyy-MM-dd HH:mm') }
