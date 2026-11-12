# SPDX-License-Identifier: GPL-2.0-or-later
# Pack a Windows x86 build into openMERC-<version>-win32.zip + .sha256, the layout of the release archives:
#   openMerc.exe, openMerc-ded.exe, ref_gl3.dll, ref_gles3.dll, OMsnd.dll, SDL3.dll, OpenAL32.dll, fmt.dll, the MSVC
#   runtime and openMERC-README.txt next to each other, and the folder openmerc\ (game and player modules,
#   zz_openmerc.pak, LICENSE.txt, THIRD_PARTY.txt, SOURCE.txt, licenses\, hd\). Unpacked into the Soldier of Fortune
#   folder it adds no file the game has (docs/impl/INSTALL-LAYOUT.md).
#
# usage: package-win.ps1 -Engine <dir> -Sound <dir> -Game <dir> -Player <dir> -Vcpkg <vcpkg root> [-Out <dir>]
#   the four build directories of tools\build\build-msvc.cmd, modules\sound\build.cmd, game\build.cmd and
#   player\build.cmd (OM_BUILD_DIR); -Vcpkg the classic-mode vcpkg root with sdl3 and openal-soft for x86-windows.
# The MSVC runtime is taken from the newest Visual Studio found by vswhere (VC\Redist\MSVC\<version>\x86).
param(
	[Parameter(Mandatory = $true)] [string]$Engine,
	[Parameter(Mandatory = $true)] [string]$Sound,
	[Parameter(Mandatory = $true)] [string]$Game,
	[Parameter(Mandatory = $true)] [string]$Player,
	[Parameter(Mandatory = $true)] [string]$Vcpkg,
	[string]$Out = "."
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$cm = Get-Content -Raw (Join-Path $repo 'engine\CMakeLists.txt')
if ($cm -notmatch 'set\(OM_VERSION "([^"]+)"\)') { throw 'OM_VERSION not found in engine\CMakeLists.txt' }
$version = $Matches[1]
$commit = (& git -C $repo rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'git rev-parse failed' }
$name = "openMERC-$version-win32"

# the newest file of that name under a build directory (the generators put outputs in different subfolders)
function Find-Built([string]$dir, [string]$file) {
	$f = Get-ChildItem -Path $dir -Recurse -File -Filter $file -ErrorAction SilentlyContinue |
		Where-Object { $_.FullName -notmatch '\\(CMakeFiles|vcpkg_installed)\\' } |
		Sort-Object LastWriteTime -Descending | Select-Object -First 1
	if (-not $f) { throw "$file not found under $dir" }
	return $f.FullName
}

# vcpkg: installed x86-windows files and the version of a port (from its info list name, e.g. sdl3_3.2.22_x86-windows.list)
$vi = Join-Path $Vcpkg 'installed\x86-windows'
function Port-Version([string]$port) {
	$l = Get-ChildItem -Path (Join-Path $Vcpkg 'installed\vcpkg\info') -Filter "$($port)_*_x86-windows.list" |
		Select-Object -First 1
	if (-not $l) { throw "vcpkg port $port (x86-windows) is not installed" }
	return ($l.Name -replace "^$([regex]::Escape($port))_", '' -replace '_x86-windows\.list$', '')
}

# MSVC runtime (x86) of the newest Visual Studio
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
$crt = Get-ChildItem -Path (Join-Path $vs 'VC\Redist\MSVC') -Directory |
	ForEach-Object { Get-ChildItem -Path (Join-Path $_.FullName 'x86') -Directory -Filter 'Microsoft.VC*.CRT' -ErrorAction SilentlyContinue } |
	Sort-Object FullName -Descending | Select-Object -First 1
if (-not $crt) { throw "MSVC x86 runtime not found under $vs" }
$crtFiles = 'msvcp140.dll', 'msvcp140_atomic_wait.dll', 'vcruntime140.dll'
$crtVersion = (Get-Item (Join-Path $crt.FullName 'vcruntime140.dll')).VersionInfo.ProductVersion

$stage = Join-Path ([IO.Path]::GetTempPath()) ("om-pack-" + [Guid]::NewGuid().ToString('N'))
$om = Join-Path $stage 'openmerc'
New-Item -ItemType Directory -Force -Path (Join-Path $om 'licenses'), (Join-Path $om 'hd') | Out-Null

function Put([string]$src, [string]$dst) { Copy-Item -LiteralPath $src -Destination $dst -Force }

# programs and libraries next to openMerc.exe
foreach ($f in 'openMerc.exe', 'openMerc-ded.exe', 'ref_gl3.dll', 'ref_gles3.dll') { Put (Find-Built $Engine $f) $stage }
Put (Find-Built $Sound 'OMsnd.dll') $stage
foreach ($f in 'SDL3.dll', 'OpenAL32.dll') { Put (Join-Path $vi "bin\$f") $stage }
# fmt.dll: a runtime dependency of some vcpkg builds of OpenAL Soft
$haveFmt = Test-Path (Join-Path $vi 'bin\fmt.dll')
if ($haveFmt) { Put (Join-Path $vi 'bin\fmt.dll') $stage }
foreach ($f in $crtFiles) { Put (Join-Path $crt.FullName $f) $stage }

# openMERC's own modules and data
Put (Find-Built $Game 'gamex86.dll') $om
Put (Find-Built $Player 'player.dll') $om
Put (Find-Built $Engine 'zz_openmerc.pak') $om

# texts: UTF-8 (README with a byte order mark, as Notepad expects) and CRLF line ends
$utf8 = New-Object System.Text.UTF8Encoding($false)
$utf8bom = New-Object System.Text.UTF8Encoding($true)
function Write-Text([string]$path, [string]$text, [bool]$bom = $false) {
	$t = ($text -replace "`r`n", "`n") -replace "`n", "`r`n"
	[IO.File]::WriteAllText($path, $t, $(if ($bom) { $utf8bom } else { $utf8 }))
}
function Read-Text([string]$path) { return [IO.File]::ReadAllText($path) }

$readme = (Read-Text (Join-Path $repo 'dist\win\README.txt')).Replace('@VERSION@', $version).Replace('@COMMIT@', $commit)
Write-Text (Join-Path $stage 'openMERC-README.txt') $readme $true
Write-Text (Join-Path $om 'README.txt') $readme $true
Write-Text (Join-Path $om 'LICENSE.txt') (Read-Text (Join-Path $repo 'LICENSE'))
Write-Text (Join-Path $om 'openmerc-portable.txt') "openMERC: keeps settings, saves, screenshots and logs in openmerc\user next to openMerc.exe.`n"
Write-Text (Join-Path $om 'hd\README.txt') (Read-Text (Join-Path $repo 'dist\README-hd.txt'))

$vsName = (& $vswhere -latest -products * -property catalog_productLineVersion).Trim()
Write-Text (Join-Path $om 'SOURCE.txt') @"
openMERC $version - source code

Repository: https://github.com/sidreshot/openMERC
Commit:     $commit

These binaries were built from exactly this commit (Release, MSVC $vsName x86, CMake + Ninja, vcpkg classic
mode for SDL3 and OpenAL Soft), see "Build (Windows)" in the repository README. GPL v2 or later: you can
get, change and share the source code under the terms in LICENSE.txt.
"@

$sdlV = Port-Version 'sdl3'
$oalV = Port-Version 'openal-soft'
$fmtRow = ''
if ($haveFmt) {
	$fmtRow = "| ``fmt.dll`` | {fmt} (unmodified vcpkg build) | $(Port-Version 'fmt') | ``licenses/fmt.txt`` |`n"
}
$third = (Read-Text (Join-Path $repo 'THIRD_PARTY.md')).TrimEnd() + @"


## Files of this binary package

| File | Component | Version | License text |
|---|---|---|---|
| ``SDL3.dll`` | SDL 3 (unmodified vcpkg build) | $sdlV | ``licenses/SDL3.txt`` |
| ``OpenAL32.dll`` | OpenAL Soft (unmodified vcpkg build, dynamically linked; source: https://github.com/kcat/openal-soft) | $oalV | ``licenses/OpenAL-Soft.txt`` |
$fmtRow| ``msvcp140.dll``, ``msvcp140_atomic_wait.dll``, ``vcruntime140.dll`` | Microsoft Visual C++ runtime, redistributable files of Visual Studio | $crtVersion | Microsoft Visual Studio redistributable terms (``licenses/MSVC-runtime.txt``) |
| fonts inside ``openmerc/zz_openmerc.pak`` | Play, Russo One, JetBrains Mono | - | ``licenses/OFL-*.txt`` |
| everything else | openMERC | $version | ``LICENSE.txt`` (GPL v2 or later), Yamagi Quake II notices in ``licenses/Yamagi-Quake-II.txt``; code after FidelityFX FSR 1 / CAS and SMAA: ``licenses/FidelityFX-FSR.txt``, ``licenses/SMAA.txt`` |
"@
Write-Text (Join-Path $om 'THIRD_PARTY.txt') $third

$lic = Join-Path $om 'licenses'
Write-Text (Join-Path $lic 'SDL3.txt') (Read-Text (Join-Path $vi 'share\sdl3\copyright'))
Write-Text (Join-Path $lic 'OpenAL-Soft.txt') (Read-Text (Join-Path $vi 'share\openal-soft\copyright'))
if ($haveFmt) { Write-Text (Join-Path $lic 'fmt.txt') (Read-Text (Join-Path $vi 'share\fmt\copyright')) }
Write-Text (Join-Path $lic 'Yamagi-Quake-II.txt') (Read-Text (Join-Path $repo 'engine\LICENSE'))
foreach ($f in 'OFL-Play.txt', 'OFL-RussoOne.txt', 'OFL-JetBrainsMono.txt') {
	Write-Text (Join-Path $lic $f) (Read-Text (Join-Path $repo "data\base\fonts\om\$f"))
}
foreach ($f in Get-ChildItem -Path (Join-Path $repo 'licenses') -Filter '*.txt') {
	Write-Text (Join-Path $lic $f.Name) (Read-Text $f.FullName)
}
Write-Text (Join-Path $lic 'MSVC-runtime.txt') @"
msvcp140.dll, msvcp140_atomic_wait.dll and vcruntime140.dll are the Microsoft Visual C++ 2015-2022
runtime (version $crtVersion, x86), copyright Microsoft Corporation. They are redistributable files of
Visual Studio ("Distributable Code", Microsoft Visual Studio license terms) and are shipped unmodified
next to openMerc.exe so that no separate runtime installer is needed. They are not covered by the GPL.
"@

# every PE file must be 32-bit x86 (machine 0x014c): a stray x64 DLL would only fail at the player's start
foreach ($f in Get-ChildItem -Path $stage -Recurse -File -Include '*.exe', '*.dll') {
	$b = [IO.File]::ReadAllBytes($f.FullName)
	$pe = [BitConverter]::ToInt32($b, 0x3c)
	$machine = [BitConverter]::ToUInt16($b, $pe + 4)
	if ($machine -ne 0x014c) { throw ("{0} is not x86 (machine 0x{1:x4})" -f $f.Name, $machine) }
}

New-Item -ItemType Directory -Force -Path $Out | Out-Null
$zip = Join-Path (Resolve-Path $Out).Path "$name.zip"
if (Test-Path $zip) { Remove-Item $zip }
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($stage, $zip, [IO.Compression.CompressionLevel]::Optimal, $false)
Remove-Item -Recurse -Force $stage

$hash = (Get-FileHash -Algorithm SHA256 $zip).Hash.ToLowerInvariant()
[IO.File]::WriteAllText("$zip.sha256", "$hash  $name.zip`n", $utf8)
Write-Host "Packed $zip"
Write-Host "$hash  $name.zip"
