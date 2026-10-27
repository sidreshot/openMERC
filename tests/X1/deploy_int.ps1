# OpenMerc X1 - deploy the integration build to the owner's single test copy D:\Games\OpenMerc-int-test.
# Mode A by default (original 1.07f gamex86.dll/player.dll in base\), OpenMerc modules beside them as
# gamex86.openmerc.dll/player.openmerc.dll (play-modeB.cmd switches). Backs up every file it replaces into
# <Dest>\bak-pre-int3b-<ts>\ first; refuses while a program of <Dest> runs. Never touches the paks or user\ saves.
# Writes base\autoexec.cfg (autorecord + sof.log on a direct start) and openmerc-portable.txt (logs in user\). Keeps the -KeepBak newest backups plus -KeepAlso.
#   deploy_int.ps1 [-Repo <worktree>] [-Dest D:\Games\OpenMerc-int-test] [-KeepBak 2] [-KeepAlso <bak folder>]
param([string]$Repo = (Resolve-Path "$PSScriptRoot\..\..").Path, [string]$Dest = 'D:\Games\OpenMerc-int-test',
      [int]$KeepBak = 2, [string]$KeepAlso = 'bak-pre-int3b-20261027_121830')
$ErrorActionPreference = 'Stop'
$run = @(Get-Process | Where-Object { $_.Path -and $_.Path.StartsWith($Dest, [StringComparison]::OrdinalIgnoreCase) })
if ($run.Count) { throw "running from $Dest`: $($run | ForEach-Object { "$($_.Name) $($_.Id)" }) - close the game first" }
$eng = Join-Path $Repo 'build-int-x86\release'
$top = @{ 'openMerc.exe' = $eng; 'openMerc-ded.exe' = $eng; 'ref_gl3.dll' = $eng; 'ref_gles3.dll' = $eng; 'ref_omgl.dll' = $eng; 'SDL3.dll' = $eng
          'OMsnd.dll' = "$Repo\build-int-sound"; 'OpenAL32.dll' = "$Repo\build-int-sound"; 'fmt.dll' = "$Repo\build-int-sound" }
$tpl = Join-Path $Repo 'tests\X1\inttest'
foreach ($f in 'play.ps1', 'play.cmd', 'play-modeA.cmd', 'play-modeB.cmd') { $top[$f] = $tpl }
$base = @{ 'gamex86.openmerc.dll' = "$Repo\build-int-game\gamex86.dll"; 'player.openmerc.dll' = "$Repo\build-int-player\player.dll"
           'zz_openmerc.pak' = "$eng\zz_openmerc.pak" }
foreach ($k in @($top.Keys)) { if (-not (Test-Path (Join-Path $top[$k] $k))) { throw "missing $k in $($top[$k])" } }
foreach ($k in @($base.Keys)) { if (-not (Test-Path $base[$k])) { throw "missing $($base[$k])" } }
$bak = Join-Path $Dest ('bak-pre-int3b-' + (Get-Date -Format 'yyyyMMdd_HHmmss'))
New-Item -ItemType Directory -Force "$bak\base", "$bak\user" | Out-Null
# back up: everything replaced, the old engine names (moved: yquake2/q2ded/quake2 are gone since 0.2.0), the config
$old = 'yquake2.exe', 'q2ded.exe', 'quake2.exe', 'yquake2.exe.bak_pre_lateral', 'yquake2.exe.bak_pre_pfl'
foreach ($f in @($top.Keys) + 'BUILD_COMMIT.txt', 'README-TEST.txt') { if (Test-Path "$Dest\$f") { Copy-Item "$Dest\$f" $bak } }
foreach ($f in $old) { if (Test-Path "$Dest\$f") { Move-Item "$Dest\$f" $bak } }
foreach ($f in @($base.Keys) + 'gamex86.dll', 'player.dll', 'MODE.txt') { if (Test-Path "$Dest\base\$f") { Copy-Item "$Dest\base\$f" "$bak\base" } }
if (Test-Path "$Dest\user\config.cfg") { Copy-Item "$Dest\user\config.cfg" "$bak\user" }
# install
foreach ($k in $top.Keys) { Copy-Item (Join-Path $top[$k] $k) (Join-Path $Dest $k) -Force }
foreach ($k in $base.Keys) { Copy-Item $base[$k] (Join-Path "$Dest\base" $k) -Force }
# mode A: the original modules; keep them as *.retail.dll for play-modeA.cmd (taken from the copy, checked by hash)
$retail = @{ 'gamex86' = '152B6B788D0B84C0ADFB1BDD1EDDC0AF'; 'player' = '327764CDC6AEFABF629ED29CCB101B1B' }
foreach ($m in $retail.Keys) {
    $r = "$Dest\base\$m.retail.dll"
    if (-not (Test-Path $r) -or (Get-FileHash $r -Algorithm MD5).Hash -ne $retail[$m]) {
        $cur = "$Dest\base\$m.dll"
        if ((Get-FileHash $cur -Algorithm MD5).Hash -ne $retail[$m]) { throw "$cur is not the original 1.07f module and no $m.retail.dll - copy it from the game" }
        Copy-Item $cur $r -Force
    }
    Copy-Item $r "$Dest\base\$m.dll" -Force
}
Set-Content "$Dest\base\MODE.txt" 'A - original 1.07f gamex86.dll + player.dll (retail)' -Encoding ASCII
Copy-Item "$tpl\README-TEST.txt" $Dest -Force
# openmerc-portable.txt: a direct start is portable too, stdout.txt/console history go to <Dest>\user
New-Item -ItemType Directory -Force "$Dest\user" | Out-Null
Set-Content -Path "$Dest\openmerc-portable.txt" -Encoding ASCII -Value 'openMERC: this file makes openMerc.exe portable (logs in user\), same as -portable'
# base\autoexec.cfg: autorecord + log also for a direct start; a foreign autoexec is left alone
$ae = "$Dest\base\autoexec.cfg"
if (-not (Test-Path $ae) -or (Select-String -Path $ae -SimpleMatch 'openMERC int-test autoexec' -Quiet)) { Copy-Item "$tpl\autoexec.cfg" $ae -Force }
else { Write-Warning "$ae is not ours, left unchanged (no autorecord on a direct start)" }
# keep the backups small: the $KeepBak newest bak-pre-int3* plus the pre-integration one
Get-ChildItem $Dest -Directory -Filter 'bak-pre-int3*' | Where-Object { $_.Name -ne $KeepAlso } |
  Sort-Object Name -Descending | Select-Object -Skip $KeepBak | ForEach-Object { Write-Host "prune $($_.Name)"; Remove-Item $_.FullName -Recurse -Force }
Push-Location $Repo
$c = git rev-parse HEAD; $d = git describe --tags --always --dirty; $br = git rev-parse --abbrev-ref HEAD; $cd = git log -1 --format=%ci
Pop-Location
@("openMERC V0.2.0 test build (OpenMerc-int-test)", "branch:  $br", "commit:  $c", "build:   $d", "commit date: $cd",
  "deployed: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') (UTC+5)", "modules: mode A (play-modeA.cmd / play-modeB.cmd)", "backup:  $bak") |
  Set-Content "$Dest\BUILD_COMMIT.txt" -Encoding ASCII
Write-Host "deployed $d to $Dest (backup $bak)"
