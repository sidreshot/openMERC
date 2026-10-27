# Copies the MENU2 build into the test copy (never D:\Games\OpenMerc).
#   -Mode B (default): openMERC game and player modules from build-MENU2-game / build-MENU2-player
#   -Mode A: the original modules, kept once as base\gamex86.retail.dll / base\player.retail.dll
param([string]$Build = "$PSScriptRoot\..\..\build-MENU2\release", [string]$Game = 'D:\Games\OpenMerc-menu-test',
      [string]$Snd = "$PSScriptRoot\..\..\build-MENU2-snd", [ValidateSet('A','B')][string]$Mode = 'B',
      [string]$GameBuild = "$PSScriptRoot\..\..\build-MENU2-game", [string]$PlayerBuild = "$PSScriptRoot\..\..\build-MENU2-player")
if ($Game -match 'OpenMerc$') { throw "refusing to deploy into $Game" }
foreach ($f in 'openMerc.exe','openMerc-ded.exe','ref_gl3.dll','ref_omgl.dll','SDL3.dll') { Copy-Item "$Build\$f" "$Game\$f" -Force }
# OMsnd 1.0 from this tree (modules\sound\build.cmd with OM_BUILD_DIR=build-MENU2-snd)
foreach ($f in 'OMsnd.dll','OpenAL32.dll','fmt.dll') { if (Test-Path "$Snd\$f") { Copy-Item "$Snd\$f" "$Game\$f" -Force } }
Copy-Item "$Build\zz_openmerc.pak" "$Game\base\zz_openmerc.pak" -Force
# keep the original modules once (only if the current file is not one of ours)
foreach ($m in 'gamex86','player') {
    $cur = "$Game\base\$m.dll"; $ret = "$Game\base\$m.retail.dll"
    if (-not (Test-Path $ret) -and (Test-Path $cur) -and (Get-Item $cur).LastWriteTime.Year -lt 2026) { Copy-Item $cur $ret }
}
if ($Mode -eq 'B') {
    Copy-Item "$GameBuild\gamex86.dll" "$Game\base\gamex86.dll" -Force
    Copy-Item "$PlayerBuild\player.dll" "$Game\base\player.dll" -Force
} else {
    foreach ($m in 'gamex86','player') {
        if (-not (Test-Path "$Game\base\$m.retail.dll")) { throw "no $m.retail.dll in $Game\base" }
        Copy-Item "$Game\base\$m.retail.dll" "$Game\base\$m.dll" -Force
    }
}
"deployed $Build -> $Game (mode $Mode)"