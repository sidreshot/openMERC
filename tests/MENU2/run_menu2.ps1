# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc MENU2 - run the OpenMerc client of the menu test copy (D:\Games\OpenMerc-menu-test) on a separate,
# never shown desktop (I4a's DeskRun.cs: no focus, no input from the owner) with a console script, and convert
# the om_menushot / omshot captures to PNG under <OutRoot>\<Tag>\.
#   run_menu2.ps1 -Tag m1080 -Width 1920 -Height 1080 -Script tests\MENU2\pages.cfg
# Every run: -portable, user directory under the test copy, ports -Port/-Port+1 (default 29630/29631), OMsnd
# writing a wave file; parallel runs need different -Tag and -Port values (own desktop, own processes).
# Implements: black-box test harness (no game code)
param(
    [Parameter(Mandatory = $true)][string]$Tag,
    [string]$Game = 'D:\Games\OpenMerc-menu-test',
    [string]$OutRoot = 'D:\Games\OpenMerc-menu-test\shots',
    [int]$Width = 1920,
    [int]$Height = 1080,
    [string]$Args = '',
    [string]$Script = '',
    [switch]$Fresh,
    [switch]$NoWave,   # real output device instead of the wave writer (device switching tests)
    [switch]$KeepWave, # keep <OutRoot>\<Tag>\snd.wav
    [int]$Timeout = 240,
    [switch]$Debugger, # run under cdb: on an access violation write registers, stack and modules to <OutRoot>\<Tag>\cdb.log
    [int]$Port = 29630,
    [string]$DeskRun = 'C:\openmerc\engine\src\client\refresh\gl3\test\DeskRun.cs'
)
$ErrorActionPreference = 'Stop'
$Exe = Join-Path $Game 'openMerc.exe'
$out = Join-Path $OutRoot $Tag
New-Item -ItemType Directory -Force $out | Out-Null
$user = Join-Path $Game "user-$Tag"
if ($Fresh -and (Test-Path $user)) { Remove-Item $user -Recurse -Force }
New-Item -ItemType Directory -Force $user | Out-Null
$post = ''
if ($Script) { Copy-Item $Script (Join-Path $user 'omtest.cfg') -Force; $post = '+set busywait 0 +exec omtest.cfg' }
# a script split into parts (<name>_*.cfg next to it, run with exec) takes the parts along
if ($Script) { Get-ChildItem (Join-Path (Split-Path $Script) ([IO.Path]::GetFileNameWithoutExtension($Script) + '_*.cfg')) -ErrorAction SilentlyContinue | % { Copy-Item $_.FullName (Join-Path $user $_.Name) -Force } }
foreach ($d in @('menushots', 'scrnshot')) { $p = Join-Path $user $d; if (Test-Path $p) { Remove-Item (Join-Path $p '*') -Force -ErrorAction SilentlyContinue } }
foreach ($l in @('sof.log', 'qconsole.log')) { $p = Join-Path $user $l; if (Test-Path $p) { Remove-Item $p -Force } }
$g = $Game -replace '\\', '/'
$u = $user -replace '\\', '/'
$wav = (Join-Path $out 'snd.wav') -replace '\\', '/'
$snd = "+set snd_dll OMsnd.dll +set s_om_wavefile $wav +set s_om_focusmute 0"
if ($NoWave) { $snd = "+set snd_dll OMsnd.dll +set s_om_focusmute 0" }
$vid = "+set vid_fullscreen 0 +set r_mode -1 +set r_customwidth $Width +set r_customheight $Height"
$argv = "-portable -basedir $g -user $u +set port $Port +set clientport $($Port + 1) +set logfile 3 $vid $snd $Args $post".Trim()
$words = @($argv -split '\s+' | Where-Object { $_ }).Count
if ($words -gt 40) { throw "too many command-line words ($words)" }
if ($Debugger) {
    $cdb = 'C:\Program Files (x86)\Windows Kits\10\Debuggers\x86\cdb.exe'
    Set-Content -Path (Join-Path $out 'cdb.txt') -Encoding ASCII -Value @('sxe -c "r;kb 40;lm;q" av', 'g')
    $argv = "-hd -logo $(Join-Path $out 'cdb.log') -cf $(Join-Path $out 'cdb.txt') $Exe $argv"
    $Exe = $cdb
}if (-not ('DeskRun' -as [type])) { Add-Type -Path $DeskRun -ReferencedAssemblies System.Drawing }
# this run's processes only (its user directory is on the command line), so parallel runs do not kill each other
function Get-Own { @(Get-CimInstance Win32_Process -Filter "Name='openMerc.exe' OR Name='cdb.exe'" | Where-Object { $_.CommandLine -like "*user-$Tag *" } | ForEach-Object { Get-Process -Id $_.ProcessId -ErrorAction SilentlyContinue }) }
$before = @(Get-Own | ForEach-Object { $_.Id })
Write-Host "menu2: $Exe $argv"
$sw = [Diagnostics.Stopwatch]::StartNew()
$code = $null
try {
    $code = [DeskRun]::Run("OMTestMENU2$Tag", $Exe, $argv, $Game, $Timeout * 1000, $null, $null, 'SDL_app', $null)
} finally {
    $secs = $sw.Elapsed.TotalSeconds
    Get-Own | Where-Object { $before -notcontains $_.Id } | ForEach-Object { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue }
    Write-Host ("exit code {0} after {1:N1} s{2}" -f $code, $secs, $(if ($code -eq -1) { ' (killed at timeout)' } else { '' }))
    Start-Sleep -Milliseconds 300
    $n = 0
    foreach ($dir in @((Join-Path $user 'menushots'), (Join-Path $user 'scrnshot'))) {
        if (Test-Path $dir) {
            foreach ($c in Get-ChildItem (Join-Path $dir '*.tga')) {
                python -I -c "import sys; from PIL import Image; Image.open(sys.argv[1]).convert('RGB').save(sys.argv[2])" $c.FullName (Join-Path $out ($c.BaseName + '.png'))
                $n++
            }
        }
    }
    if (-not $KeepWave -and (Test-Path (Join-Path $out 'snd.wav'))) { Remove-Item (Join-Path $out 'snd.wav') -Force -ErrorAction SilentlyContinue }
    foreach ($l in @('sof.log', 'qconsole.log')) { $p = Join-Path $user $l; if (Test-Path $p) { Copy-Item $p (Join-Path $out 'client.log') -Force } }
    Write-Host "$n capture(s) -> $out"
    Set-Content -Path (Join-Path $out 'run.txt') -Encoding ASCII -Value @("menu2: $Exe $argv", ("exit code {0} after {1:N1} s" -f $code, $secs), "$n capture(s)")
}
