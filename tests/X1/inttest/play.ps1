# OpenMerc int-test: the combined test build of the integration branch (owner testing happens only here).
# Runs openMerc.exe portable: settings, saves, logs and the automatic demos live in .\user\.
#   play.ps1 [-Map kos1] [-Windowed] [-NoRec] [-Extra '+set cvar value']
# om_autorecord 1 (this test copy only): every map start, level change and savegame load starts
#   user\demos\auto\<date>_<time>_<map>.dm2 with the debug log <same name>.jsonl (README-TEST.txt).
# The game modules in base\ (gamex86.dll/player.dll) are switched by play-modeA.cmd / play-modeB.cmd.
param([string]$Map = '', [switch]$Windowed, [switch]$NoRec, [string]$Extra = '')
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$user = Join-Path $here 'user'
New-Item -ItemType Directory -Force $user | Out-Null
$log = Join-Path $user 'sof.log'
if (Test-Path $log) { Remove-Item $log -Force }
$h = $here -replace '\\', '/'
$u = $user -replace '\\', '/'
$cmdline = "-portable -basedir $h +set user $u +set logfile 3"
if (-not $NoRec) { $cmdline += " +set om_autorecord 1 +set s_om_debug 24" } else { $cmdline += " +set om_autorecord 0" }
if ($Windowed) { $cmdline += " +set vid_fullscreen 0 +set r_mode 21" }
if ($Extra) { $cmdline += " $Extra" }
if ($Map) { $cmdline += " +map $Map" }
$mode = Join-Path $here 'base\MODE.txt'
if (Test-Path $mode) { Write-Host ("modules: " + (Get-Content $mode -TotalCount 1)) }
Write-Host "running: openMerc.exe $cmdline"
$p = Start-Process -FilePath (Join-Path $here 'openMerc.exe') -ArgumentList $cmdline -WorkingDirectory $here -PassThru
$p.WaitForExit()
Write-Host "exit code $($p.ExitCode); log: $log; demos: $(Join-Path $user 'demos\auto')"
