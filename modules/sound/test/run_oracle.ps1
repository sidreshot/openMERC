# SPDX-License-Identifier: GPL-2.0-or-later
# Black-box run of the original SoF.exe with OMsnd.dll (SPEC-004 backend selection via snd_dll).
# Copies OMsnd.dll + OpenAL32.dll + fmt.dll next to SoF.exe (adds files only), writes a test script
# into <game>\user, starts the game windowed, waits for it to quit (or kills it after -Timeout s)
# and copies user\sof.log to -OutDir. (`developer` must not be set on the command line: the engine then
# shows a blocking `Developer Mode` message box, SPEC-301; test scripts set it themselves.)
# Implements: SPEC-004 (test vectors)
param(
    [string]$Game = 'D:\Games\SoF-oracle',
    [string]$Build = (Join-Path $PSScriptRoot '..\..\..\build-sound-x86-Release'),
    [string]$Script = (Join-Path $PSScriptRoot 'smoke.cfg'),
    [string]$OutDir = (Join-Path $env:TEMP 'omsnd-test'),
    [string]$Wave = '',
    [string]$SndDll = 'OMsnd',
    [string]$Extra = '',
    [int]$Timeout = 180
)
$ErrorActionPreference = 'Stop'
if ($Game -like '*\Soldier of Fortune*') { throw 'refusing to touch the main install' }
New-Item -ItemType Directory -Force $OutDir | Out-Null
foreach ($f in 'OMsnd.dll', 'OpenAL32.dll', 'fmt.dll') {
    Copy-Item (Join-Path $Build $f) (Join-Path $Game $f) -Force
}
$user = Join-Path $Game 'user'
Copy-Item $Script (Join-Path $user 'omtest.cfg') -Force
$log = Join-Path $user 'sof.log'
if (Test-Path $log) { Remove-Item $log -Force }

$argv = "+set ip 127.0.0.1 +set vid_fullscreen 0 +set gl_mode 3 +set logfile 3 " +
        "+set snd_dll $SndDll +set s_om_focusmute 0 +set s_volume 0.7 +set s_musicvolume 0.7 "
if ($Wave) {
    if (Test-Path $Wave) { Remove-Item $Wave -Force }
    $argv += "+set s_om_wavefile $Wave "
}
$argv += "$Extra +exec omtest.cfg"
Write-Host "SoF.exe $argv"
$p = Start-Process -FilePath (Join-Path $Game 'SoF.exe') -ArgumentList $argv -WorkingDirectory $Game -PassThru
$sw = [Diagnostics.Stopwatch]::StartNew()
if (-not $p.WaitForExit($Timeout * 1000)) {
    Write-Host "timeout after $Timeout s - killing"
    Stop-Process -Id $p.Id -Force
    $p.WaitForExit()
    $killed = $true
} else { $killed = $false }
Write-Host ("exit code {0} after {1:N1} s{2}" -f $p.ExitCode, $sw.Elapsed.TotalSeconds, $(if ($killed) { ' (killed)' } else { '' }))
Get-Process SoF -ErrorAction SilentlyContinue | Stop-Process -Force
if (Test-Path $log) { Copy-Item $log (Join-Path $OutDir 'sof.log') -Force }
