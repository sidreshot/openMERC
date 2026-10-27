# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc V1 - run the OpenMerc client (D:\Games\SoF-test-V1\om\openMerc.exe) on a separate, never shown desktop
# (I4a's DeskRun.cs: no focus, no input from the owner) with free-form arguments, so the engine's own defaults can
# be tested: a plain start writes nothing but -portable/-basedir/-user. Unlike engine\src\client\menu\test\run_om.ps1
# nothing about the video mode is forced. Every run uses -portable and a user directory under openmerc-data\v1.
#   run_v1.ps1 -Tag plain -Fresh                                 (fresh user dir with only the given autoexec)
#   run_v1.ps1 -Tag menus1080 -Args "+set vid_fullscreen 0 +set r_mode 21" -Script tests\V1\menus.cfg
# Output: openmerc-data\v1\<Tag>\ (client log, menushots as PNG, config.cfg after exit, run.txt).
# Ports: V1 base 29600 (docs/process/testing.md): +set port 29600 +set clientport 29601 are always set.
# Implements: black-box test harness (no game code)
param(
    [Parameter(Mandatory = $true)][string]$Tag,
    [string]$Exe = 'D:\Games\SoF-test-V1\om\openMerc.exe',
    [string]$Data = 'D:\Games\SoF-test-V1',
    [string]$OutRoot = 'C:\openmerc-data\v1',
    # extra command-line words (the engine takes at most ~40)
    [string]$Args = '',
    # console script copied to <user>\omtest.cfg and started with +exec omtest.cfg
    [string]$Script = '',
    # autoexec.cfg placed in the (fresh) user directory
    [string]$Autoexec = '',
    [switch]$Fresh,
    [int]$Timeout = 180,
    [string]$DeskRun = 'C:\openmerc\engine\src\client\refresh\gl3\test\DeskRun.cs'
)
$ErrorActionPreference = 'Stop'
$out = Join-Path $OutRoot $Tag
New-Item -ItemType Directory -Force $out | Out-Null
$user = Join-Path $out 'user'
if ($Fresh -and (Test-Path $user)) { Remove-Item $user -Recurse -Force }
New-Item -ItemType Directory -Force $user | Out-Null
if ($Autoexec) { Copy-Item $Autoexec (Join-Path $user 'autoexec.cfg') -Force }
$post = ''
if ($Script) { Copy-Item $Script (Join-Path $user 'omtest.cfg') -Force; $post = '+exec omtest.cfg' }
$shots = Join-Path $user 'menushots'
if (Test-Path $shots) { Remove-Item (Join-Path $shots '*') -Force -ErrorAction SilentlyContinue }
foreach ($l in @('sof.log', 'qconsole.log')) { $p = Join-Path $user $l; if (Test-Path $p) { Remove-Item $p -Force } }

$d = $Data -replace '\\', '/'
$u = $user -replace '\\', '/'
# sound: OMsnd with OpenAL Soft's wave writer, so nothing reaches the speakers (the original defsnd.dll blocks
# on the hidden desktop; s_nosound is not honoured by it)
$wav = (Join-Path $out 'snd.wav') -replace '\\', '/'
$snd = "+set snd_dll OMsnd.dll +set s_om_wavefile $wav +set s_om_focusmute 0"
$argv = "-portable -basedir $d +set user $u +set port 29600 +set clientport 29601 +set logfile 3 $snd $Args $post".Trim()
$words = @($argv -split '\s+' | Where-Object { $_ }).Count
if ($words -gt 40) { throw "too many command-line words ($words)" }
if (-not ('DeskRun' -as [type])) { Add-Type -Path $DeskRun -ReferencedAssemblies System.Drawing }
function Get-Own { @(Get-Process -Name openMerc -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $Exe }) }
$before = @(Get-Own | ForEach-Object { $_.Id })
Write-Host "v1: $Exe $argv"
$sw = [Diagnostics.Stopwatch]::StartNew()
$code = $null
try {
    # working directory = the data copy (the sound module is looked up like the original's)
    $code = [DeskRun]::Run('OMTestV1', $Exe, $argv, $Data, $Timeout * 1000, $null, $null, 'SDL_app', $null)
} finally {
    $secs = $sw.Elapsed.TotalSeconds
    Get-Own | Where-Object { $before -notcontains $_.Id } | ForEach-Object { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue }
    Write-Host ("exit code {0} after {1:N1} s{2}" -f $code, $secs, $(if ($code -eq -1) { ' (killed at timeout)' } else { '' }))
    Start-Sleep -Milliseconds 300
    $n = 0
    # om_menushot (menu layer) -> menushots\, omshot (whole frame) -> scrnshot\
    foreach ($dir in @($shots, (Join-Path $user 'scrnshot'))) {
        if (Test-Path $dir) {
            foreach ($c in Get-ChildItem (Join-Path $dir '*.tga')) {
                python -I -c "import sys; from PIL import Image; Image.open(sys.argv[1]).convert('RGB').save(sys.argv[2])" $c.FullName (Join-Path $out ($c.BaseName + '.png'))
                $n++
            }
        }
    }
    if (Test-Path (Join-Path $out 'snd.wav')) { Remove-Item (Join-Path $out 'snd.wav') -Force -ErrorAction SilentlyContinue }
    foreach ($l in @('sof.log', 'qconsole.log')) { $p = Join-Path $user $l; if (Test-Path $p) { Copy-Item $p (Join-Path $out 'client.log') -Force } }
    $cfg = Join-Path $user 'config.cfg'
    if (Test-Path $cfg) { Copy-Item $cfg (Join-Path $out 'config.cfg') -Force }
    Write-Host "$n capture(s) -> $out"
    Set-Content -Path (Join-Path $out 'run.txt') -Encoding ASCII -Value @("v1: $Exe $argv", ("exit code {0} after {1:N1} s" -f $code, $secs), "$n capture(s)")
}
