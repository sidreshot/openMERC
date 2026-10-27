# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc I1d - one scripted single-player session in the I1d test copy (D:\Games\SoF-test-I1d), on a separate,
# never shown desktop (I4a's DeskRun.cs), so the game can neither take the focus nor receive the owner's input:
#   -Engine om   : the OpenMerc engine (om\SoF.exe = openMerc.exe of -Build) hosting the modules of base\ of the
#                  original-modules copy D:\Games\SoF-test-I1d-orig (original gamex86.dll / player.dll, or
#                  OpenMerc's player.dll with -OmPlayer)
#   -Engine orig : the original 1.07f SoF.exe of that copy; renderer through the capture shim ref_omcap.dll
#   -Engine open : the fully open stack D:\Games\SoF-test-I1d (tests\X1\assemble.ps1: our engine openMerc.exe,
#                  ref_gl3, OMsnd, OUR base\gamex86.dll and base\player.dll); nothing is copied
# All use OMsnd (build-sound-x86-Release) with `s_om_debug 4`, so every sound start is logged with the client time
# ("OMsnd: [p=.. cl=..] start <name> ent .. ch ..") and, unless -NoWav, the OpenAL mix is written to <data>\snd.wav
# (s_om_wavefile; nothing reaches the speakers), and the I1d ports (base 29560: server 29560, client 29561).
# The script is copied to <user>\omtest.cfg and run by `+exec omtest.cfg`; a prelude i1dpre.cfg defines the
# capture aliases `shot_<n>` (n = 1..60) for the engine: original `echo OMSHOT <n>` (capture shim, next frame),
# OpenMerc `omshot <n>`, and `trace_<n>` (original only: `echo OMTRACE <n>`, one frame of renderer calls).
# Outputs in openmerc-data\I1d\<Tag>\: client.log, captures <n>.png, traces <n>.txt. Only processes started here
# are stopped; the oracle is never touched. Parental locks: stock modules only (sv_violence 63 = no refusal);
# with -OmPlayer the listen game is refused by the original's rule - the OpenMerc engine has no such check.
#   run.ps1 -Engine orig -Script tests\I1d\sp_pistol.cfg -Tag o1
#   run.ps1 -Engine om   -Script tests\I1d\sp_pistol.cfg -Tag m1
#   run.ps1 -Engine open -Script tests\I1d\sp_pistol.cfg -Tag x1
# Implements: SPEC-003 / SPEC-600 / SPEC-400 black-box test harness (no game code)
param(
    [ValidateSet('om', 'orig', 'open')][string]$Engine = 'om',
    [Parameter(Mandatory = $true)][string]$Script,
    [string]$Tag = 'x',
    [int]$Timeout = 150,
    [string]$Extra = '',
    # game copy ('' = D:\Games\SoF-test-I1d for -Engine open, D:\Games\SoF-test-I1d-orig otherwise)
    [string]$Game = '',
    [string]$Build = 'C:\openmerc\build-I1d-x86\release',
    [string]$CapBuild = 'C:\openmerc\build-I1d-x86\refcap',
    [string]$SndBuild = 'C:\openmerc\build-sound-x86-Release',
    # OpenMerc's player module (I7) instead of the original in base\ (the original is restored afterwards)
    [string]$OmPlayer = '',
    [int]$ServerPort = 29560,
    [int]$ClientPort = 29561,
    # a demo file copied to <user>\demos and base\demos for `demomap` (keeps its file name)
    [string]$Demo = '',
    # milliseconds of game time per frame (0 = real time)
    [int]$FixedTime = 25,
    [switch]$NoSound,
    [switch]$NoWav,
    [switch]$NoCopy,
    # OpenMerc's player module prints its selection / view-instance / player-state steps ("pm:" lines; the
    # environment variable OM_PMTRACE is inherited by the game process)
    [switch]$PmTrace
)
$ErrorActionPreference = 'Stop'
$game = if ($Game) { $Game } elseif ($Engine -eq 'open') { 'D:\Games\SoF-test-I1d' } else { 'D:\Games\SoF-test-I1d-orig' }
$repo = 'C:\openmerc'
$data = Join-Path 'C:\openmerc-data\I1d' $Tag
New-Item -ItemType Directory -Force $data | Out-Null
Get-ChildItem $data -File -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
$deskRun = Join-Path $repo 'engine\src\client\refresh\gl3\test\DeskRun.cs'
if (-not ('DeskRun' -as [type])) { Add-Type -Path $deskRun -ReferencedAssemblies System.Drawing }

$base = Join-Path $game 'base'
$origPlayer = $null
if ($Engine -ne 'open') {
    $origPlayer = Join-Path $base 'player.dll.orig'
    if (-not (Test-Path $origPlayer)) { Copy-Item (Join-Path $base 'player.dll') $origPlayer }
    if ((Get-Item $origPlayer).Length -ne 110592) { throw "$origPlayer is not the 1.07f player.dll" }
    if ($OmPlayer) { Copy-Item $OmPlayer (Join-Path $base 'player.dll') -Force }
    else { Copy-Item $origPlayer (Join-Path $base 'player.dll') -Force }
} elseif ($OmPlayer) { throw '-OmPlayer applies to the original-modules copy only (the open copy already has ours)' }

$pre = @("set port $ServerPort", "set clientport $ClientPort", 'set in_mouse 0', 'set in_joystick 0', 'set console 1')
# deterministic frames: the original's fixedtime is milliseconds per frame, Yamagi's microseconds. Real time
# (-FixedTime 0) caps both engines to 40 fps (`wait` = one frame = 25 ms as with the fixed time; a hidden window
# would otherwise run hundreds of frames per second and the scripts' waits would shrink to nothing). Measured
# 2026-10-25: OpenMerc honours vid_maxfps (sp_a.cfg 70 s); the original ignores cl_maxfps set this way (18 s, as
# uncapped) - for comparisons keep the default fixed time on both: they then run at the same ~5x wall speed and the
# OMsnd 0.3 `[t=<real ms>]` stamps of both logs are on the same scale.
if ($FixedTime -gt 0) {
    $pre += $(if ($Engine -eq 'orig') { "set fixedtime $FixedTime" } else { "set fixedtime $($FixedTime * 1000)" })
} else {
    $pre += $(if ($Engine -eq 'orig') { 'set cl_maxfps 40' } else { 'set vid_maxfps 40' })
}
for ($i = 1; $i -le 60; $i++) {
    if ($Engine -eq 'orig') { $pre += "alias shot_$i `"echo OMSHOT $i`""; $pre += "alias trace_$i `"echo OMTRACE $i`"" }
    else { $pre += "alias shot_$i `"omshot $i`""; $pre += "alias trace_$i `"set om_entdump 1;echo trace $i`"" }
}
$wav = (Join-Path $data 'snd.wav') -replace '\\', '/'
$snd = if ($NoSound) { '+set s_nosound 1' } else { '+set snd_dll OMsnd.dll +set s_om_debug 4 +set s_om_focusmute 0' }
if (-not $NoSound -and -not $NoWav) { $snd += " +set s_om_wavefile $wav" }

$pname = 'SoF'
if ($Engine -eq 'open') {
    $dir = $game
    $exe = Join-Path $dir 'openMerc.exe'
    $pname = 'openMerc'
    if (-not (Test-Path $exe)) { throw "no open stack at $exe (tests\X1\assemble.ps1 -Dest $game ...)" }
    $user = Join-Path $data 'user'
    if (Test-Path $user) { Remove-Item $user -Recurse -Force }
    New-Item -ItemType Directory -Force $user | Out-Null
    $u = $user -replace '\\', '/'
    $g = $game -replace '\\', '/'
    $pre = @('set in_grab 0', 'set cl_async 0') + $pre
    $cargs = "-portable -basedir $g +set user $u +set vid_fullscreen 0 +set vid_renderer gl3 " +
             "+set gl_mode 3 +set r_mode 4 +set logfile 3 $snd $Extra +exec i1dpre.cfg +exec omtest.cfg"
    $wclass = 'SDL_app'
} elseif ($Engine -eq 'om') {
    $dir = Join-Path $game 'om'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $exe = Join-Path $dir 'SoF.exe'
    if (-not $NoCopy) {
        Copy-Item (Join-Path $Build 'openMerc.exe') $exe -Force
        foreach ($f in 'SDL3.dll', 'ref_gl3.dll', 'ref_omgl.dll') {
            if (Test-Path (Join-Path $Build $f)) { Copy-Item (Join-Path $Build $f) (Join-Path $dir $f) -Force }
        }
        foreach ($f in 'Defsnd.dll') { Copy-Item (Join-Path $game $f) (Join-Path $dir $f) -Force }
        foreach ($f in 'OMsnd.dll', 'OpenAL32.dll', 'fmt.dll') { Copy-Item (Join-Path $SndBuild $f) (Join-Path $dir $f) -Force }
    }
    $user = Join-Path $data 'user'
    if (Test-Path $user) { Remove-Item $user -Recurse -Force }
    New-Item -ItemType Directory -Force $user | Out-Null
    $u = $user -replace '\\', '/'
    $g = $game -replace '\\', '/'
    $pre = @('set in_grab 0', 'set cl_async 0') + $pre
    $cargs = "-portable -basedir $g +set user $u +set vid_fullscreen 0 +set vid_renderer gl3 " +
             "+set gl_mode 3 +set r_mode 4 +set logfile 3 $snd $Extra +exec i1dpre.cfg +exec omtest.cfg"
    $wclass = 'SDL_app'
} else {
    $dir = $game
    $exe = Join-Path $game 'SoF.exe'
    Copy-Item (Join-Path $CapBuild 'ref_omcap.dll') (Join-Path $game 'ref_omcap.dll') -Force
    foreach ($f in 'OMsnd.dll', 'OpenAL32.dll', 'fmt.dll') { Copy-Item (Join-Path $SndBuild $f) (Join-Path $game $f) -Force }
    $rel = 'u_i1d_' + $Tag
    $user = Join-Path $game $rel
    if (Test-Path $user) { Remove-Item $user -Recurse -Force }
    New-Item -ItemType Directory -Force $user | Out-Null
    # developer must stay 0 (SoF.exe opens the "Developer Mode" message box otherwise, SPEC-301)
    $pre = @('set developer 0', "set ip_clientport $ClientPort") + $pre
    $cargs = "+set user ./$rel +set vid_ref omcap +set om_capref gl +set vid_fullscreen 0 " +
             "+set gl_mode 3 +set logfile 3 $snd $Extra +exec i1dpre.cfg +exec omtest.cfg"
    $wclass = 'SoF'
}
Copy-Item $Script (Join-Path $user 'omtest.cfg') -Force
if ($Demo) {
    # demos/<name>: the user dir (the original's write dir) and the game directory
    foreach ($dd in (Join-Path $user 'demos'), (Join-Path $base 'demos')) {
        New-Item -ItemType Directory -Force $dd | Out-Null
        Copy-Item $Demo $dd -Force
    }
}
$common = Join-Path (Split-Path $Script) 'common.cfg'
if (Test-Path $common) { Copy-Item $common (Join-Path $user 'i1d_common.cfg') -Force }
Set-Content -Path (Join-Path $user 'i1dpre.cfg') -Encoding ASCII -Value $pre
$words = @($cargs -split '\s+' | Where-Object { $_ }).Count
if ($words -gt 50) { throw "the engines take at most 50 command-line words ($words)" }

$shotDir = Join-Path $env:TEMP ('omi1d_cap_' + $Tag)
New-Item -ItemType Directory -Force $shotDir | Out-Null
Remove-Item (Join-Path $shotDir '*') -Force -ErrorAction SilentlyContinue
$req = $null
if ($Engine -eq 'orig') {
    $req = Join-Path $game 'omshot.req'
    foreach ($f in $req, (Join-Path $game 'omtrace.req')) { if (Test-Path $f) { Remove-Item $f -Force } }
}
$log = Join-Path $user 'sof.log'

function Get-Own { @(Get-Process -Name $pname -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe }) }
$before = @(Get-Own | ForEach-Object { $_.Id })
try {
    Write-Host "$Engine`: SoF.exe $cargs"
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $mlog = if ($Engine -eq 'orig') { $log } else { $null }
    $mdir = if ($Engine -eq 'orig') { $shotDir } else { $null }
    if ($PmTrace) { $env:OM_PMTRACE = '1' }
    $code = [DeskRun]::Run('OMTestI1d', $exe, $cargs, $dir, $Timeout * 1000, $mlog, $mdir, $wclass, $req)
    Write-Host ("exit code {0} after {1:N1} s{2}" -f $code, $sw.Elapsed.TotalSeconds, $(if ($code -eq -1) { ' (killed at timeout)' } else { '' }))
} finally {
    Remove-Item Env:OM_PMTRACE -ErrorAction SilentlyContinue
    Get-Own | Where-Object { $before -notcontains $_.Id } | ForEach-Object { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue }
    Start-Sleep -Milliseconds 300
    if ($origPlayer) { Copy-Item $origPlayer (Join-Path $base 'player.dll') -Force }
    if (Test-Path $log) { Copy-Item $log (Join-Path $data 'client.log') -Force }
    $shots = if ($Engine -ne 'orig') { Join-Path $user 'scrnshot' } else { $shotDir }
    $n = 0
    if ($shots -and (Test-Path $shots)) {
        foreach ($c in Get-ChildItem (Join-Path $shots '*.tga')) {
            python -I -c "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])" $c.FullName (Join-Path $data ($c.BaseName + '.png'))
            $n++
        }
        foreach ($c in Get-ChildItem (Join-Path $shots '*.txt') -ErrorAction SilentlyContinue) { Copy-Item $c.FullName $data -Force }
    }
    if ($req -and (Test-Path $req)) { Remove-Item $req -Force }
    $clog = Join-Path $data 'client.log'
    if (Test-Path $clog) {
        $foreign = Select-String -Path $clog -Pattern '^\S+: client_connect' | Where-Object { $_.Line -notmatch '^(loopback|\[0\.0\.0\.0\]:0|0\.0\.0\.0:0):' }
        if ($foreign) { Write-Warning "DISCARD this run: unrequested connect(s): $($foreign.Line -join '; ')" }
    }
    $wavFile = Join-Path $data 'snd.wav'
    if (Test-Path $wavFile) { Write-Host ('snd.wav: ' + (python -I (Join-Path $repo 'tests\X1\wavstat.py') $wavFile)) }
    Write-Host "$n capture(s); logs in $data"
}
