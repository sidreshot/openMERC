# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc X1 - one single-player session of the OpenMerc engine (openMerc.exe) on a separate, never shown
# desktop (I4a's DeskRun.cs), so the game can neither take the focus nor receive the owner's input.
#   -Game D:\Games\SoF-open-X1 : the fully open stack (our engine, ref_gl3, OMsnd, gamex86.dll, player.dll)
#   -Game D:\Games\SoF-test-X1 : the same engine binaries in om\, the ORIGINAL base\gamex86.dll / player.dll
# Sound: OMsnd with OpenAL Soft's wave writer (s_om_wavefile): the mix goes to <data>\snd.wav, nothing reaches the
# speakers; s_om_focusmute 0 so the hidden window still mixes. Settings that must exist before the client starts
# (snd_dll, the wave file, ports) go into <user>\config.cfg, which the engine runs before CL_Init.
# -Script is copied to <user>\omtest.cfg and started by `+map <map> +exec omtest.cfg` (deferred to the client's
# begin, as in I1b's sprun.ps1). Output: openmerc-data\x1\<Tag>\ (client.log, m*.png, watchdog.txt, g18crash.log,
# snd.wav, run.json). Only processes started here are stopped.
#   xrun.ps1 -Script tests\X1\x1_sweep.cfg -Map kos1 -Tag t1
# Ports: X1 base 29580 (docs/process/testing.md "Network ports"): listen server 29580, client 29581.
# Implements: black-box test harness (no game code)
param(
    [Parameter(Mandatory = $true)][string]$Script,
    # 'none' = no +map (e.g. -Post '+demomap auto/<name>.dm2')
    [string]$Map = 'kos1',
    [string]$Tag = 'x1',
    [int]$Timeout = 150,
    [string]$Extra = '',
    [string]$Post = '+exec omtest.cfg',
    [string]$Game = 'D:\Games\SoF-open-X1',
    # directory of openMerc.exe inside $Game ('' = $Game itself)
    [string]$ExeSub = '',
    [int]$ServerPort = 29580,
    [int]$ClientPort = 29581,
    [int]$Watchdog = 8,
    [switch]$NoSound,
    # extra lines for <user>\config.cfg
    [string[]]$Config = @(),
    [string]$DataRoot = 'C:\openmerc-data\x1',
    # parental locks: on this PC the stored lock words are invalid, so the engine forces every lock (cl_violence 63)
    # and a listen server with non-stock modules (sv_violence 31, GS.15) refuses its own client. -Unlock writes valid
    # "off" words only for the engine's start-up (I7's player/test/sof_locks.py), then restores the key
    # byte-identically from the I7 backup (checked before and after, and again in finally).
    [switch]$Unlock,
    [string]$LockBackup = 'C:\openmerc-data\i7\sof-registry-backup.json',
    # a folder copied into <user> before the start (demos to play back, saves)
    [string]$Seed = ''
)
$ErrorActionPreference = 'Stop'
$repo = 'C:\openmerc'
$data = Join-Path $DataRoot $Tag
if (Test-Path $data) { Remove-Item $data -Recurse -Force }
New-Item -ItemType Directory -Force $data | Out-Null
$deskRun = Join-Path $repo 'engine\src\client\refresh\gl3\test\DeskRun.cs'
if (-not ('DeskRun' -as [type])) { Add-Type -Path $deskRun -ReferencedAssemblies System.Drawing }

$dir = if ($ExeSub) { Join-Path $Game $ExeSub } else { $Game }
$exe = Join-Path $dir 'openMerc.exe'
if (-not (Test-Path $exe)) { throw "no engine at $exe" }
$user = Join-Path $data 'user'
New-Item -ItemType Directory -Force $user | Out-Null
if ($Seed) { Copy-Item (Join-Path $Seed '*') $user -Recurse -Force }
Copy-Item $Script (Join-Path $user 'omtest.cfg') -Force
$u = $user -replace '\\', '/'
$g = $Game -replace '\\', '/'
$wav = (Join-Path $data 'snd.wav') -replace '\\', '/'
$cfg = @('// X1 harness settings, executed by the engine before the client starts',
    'set in_grab 0', 'set in_mouse 0', "set port $ServerPort", "set clientport $ClientPort",
    'set vid_fullscreen 0', 'set vid_renderer gl3', 'set gl_mode 3', 'set r_mode 4')
if ($NoSound) { $cfg += 'set s_nosound 1' }
else { $cfg += @('set snd_dll OMsnd.dll', "set s_om_wavefile $wav", 'set s_om_focusmute 0') }
$cfg += 'set g18_crashlog 1'
$cfg += $Config
Set-Content -Path (Join-Path $user 'config.cfg') -Encoding ASCII -Value $cfg
$cargs = "-portable -basedir $g +set user $u +set logfile 3 +set sv_watchdog $Watchdog $Extra $(if ($Map -ne 'none') { "+map $Map" }) $Post"
$words = @($cargs -split '\s+' | Where-Object { $_ }).Count
if ($words -gt 50) { throw "the engine takes at most 50 command-line words ($words)" }
$log = Join-Path $user 'sof.log'

function Get-Own { @(Get-Process -Name openMerc -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe }) }
$before = @(Get-Own | ForEach-Object { $_.Id })
$code = $null
$secs = 0
$locks =Join-Path $repo 'player\test\sof_locks.py'
$ljob = $null
function Lock-Restore {
    $o = & python -I $locks restore $LockBackup 2>&1
    if ($LASTEXITCODE -ne 0) { Write-Warning "parental-lock key NOT identical to the backup: $o" }
    return "$o"
}
try {
    if ($Unlock) {
        & python -I $locks verify $LockBackup | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "parental-lock key differs from $LockBackup - not touching it" }
        & python -I $locks unlock | Out-Null
        # restore as soon as the engine is past SoF_InitLocks (start-up step 11; the console starts in step 14)
        $ljob = Start-Job -ScriptBlock {
            param($log, $locks, $backup)
            $sw = [Diagnostics.Stopwatch]::StartNew()
            while ($sw.Elapsed.TotalSeconds -lt 20) {
                if ((Test-Path $log) -and (Select-String -Path $log -Pattern 'Console initialized' -Quiet)) { break }
                Start-Sleep -Milliseconds 100
            }
            & python -I $locks restore $backup 2>&1
            "lock window {0:N1} s" -f $sw.Elapsed.TotalSeconds
        } -ArgumentList $log, $locks, $LockBackup
    }
    Write-Host "x1: $exe $cargs"
    $sw = [Diagnostics.Stopwatch]::StartNew()
    # working directory = the output directory: g18crash.log (game module's fault log) lands there; the engine
    # still finds base\gamex86.dll / player.dll through its search path, OMsnd.dll and the renderer next to the exe
    $code = [DeskRun]::Run('OMTestX1', $exe, $cargs, $data, $Timeout * 1000, $null, $null, 'SDL_app', $null)
    $secs = $sw.Elapsed.TotalSeconds
    Write-Host ("exit code {0} after {1:N1} s{2}" -f $code, $secs, $(if ($code -eq -1) { ' (killed at timeout)' } else { '' }))
} finally {
    if ($Unlock) {
        if ($ljob) { Wait-Job $ljob -Timeout 25 | Out-Null; Write-Host ("locks: " + ((Receive-Job $ljob) -join ' ')); Remove-Job $ljob -Force }
        Write-Host ("locks (final): " + (Lock-Restore))
    }
    Get-Own | Where-Object { $before -notcontains $_.Id } | ForEach-Object { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue }
    Start-Sleep -Milliseconds 300
    if (Test-Path $log) { Copy-Item $log (Join-Path $data 'client.log') -Force }
    $shots = Join-Path $user 'scrnshot'
    $n = 0
    if (Test-Path $shots) {
        foreach ($c in Get-ChildItem (Join-Path $shots '*.tga')) {
            python -I -c "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])" $c.FullName (Join-Path $data ($c.BaseName + '.png'))
            $n++
        }
    }
    Write-Host "$n capture(s); logs in $data"
    Set-Content -Path (Join-Path $data 'run.txt') -Encoding ASCII -Value @(
        "x1: $exe $cargs",
        ("exit code {0} after {1:N1} s" -f $code, $secs),
        "$n capture(s)")
}
