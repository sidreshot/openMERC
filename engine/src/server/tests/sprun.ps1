# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc I1b - single-player session in the I1b test copy (D:\Games\SoF-test-I1b), on a separate, never shown
# desktop (I4a's DeskRun.cs), so the game can neither take the focus nor receive the owner's input:
#   -Engine om   : the OpenMerc client (om\SoF.exe = openMerc.exe of build-I1b-x86) hosting the ORIGINAL
#                  gamex86.dll / player.dll; `omshot <name>` captures go to <user>\scrnshot.
#   -Engine orig : the original 1.07f SoF.exe; renderer through I4a's capture shim ref_omcap.dll; `echo OMSHOT
#                  <name>` lines (or -TimedShots) become captures.
# -Script is copied to <user>\omtest.cfg and started by `+map <map> +exec omtest.cfg`: both engines hold the
# commands after `map` back until the client has entered the level (not yet active: wait a little first).
# Logs: openmerc-data\i1b\<Tag>\ (client.log, captures as PNG). Only processes started here are stopped.
#   sprun.ps1 -Engine om -Script engine\src\server\tests\sp_kos1.cfg -Tag sp_om1
# Implements: SPEC-001 / SPEC-003 black-box test harness (no game code)
param(
    [ValidateSet('om', 'orig')][string]$Engine = 'om',
    [Parameter(Mandatory = $true)][string]$Script,
    [string]$Map = 'kos1',
    [string]$Tag = 'sp',
    [int]$Timeout = 150,
    [string]$Extra = '',
    # command-line commands after the map: both engines defer them until the local client has entered the level
    # (Quake II Cbuf_CopyToDefer), so the script starts once in the level
    [string]$Post = '+exec omtest.cfg',
    # original only: captures by time, 'name@seconds,...' counted from the first log line matching -TimedFrom
    # (default: the level's "Map:" line, about server frame 0); pair with the OpenMerc client's
    # `cl_omshotframes` captures at server frames (10 per second) for time-aligned pictures
    [string]$TimedShots = '',
    [string]$TimedFrom = '^Map: ',
    # original: also send `exec omtest.cfg` by the out-of-band cmd packet when the log matches (only works when
    # the client's network socket is open, i.e. not in single player); empty = do not send
    [string]$ReadyPattern = '',
    [double]$ReadyDelay = 3,
    # I1b ports (testing notes "Network ports"): base 29400; this harness uses 29403 (listen server) and
    # 29404 (client)
    [int]$ClientPort = 29404,
    [int]$ServerPort = 29403,
    [string]$Build = 'C:\openmerc\build-I1b-x86\release',
    [string]$CapBuild = 'C:\openmerc\build-I4a-x86\refcap',
    [switch]$NoCopy,
    # sv_watchdog stacks: system symbols from the Microsoft symbol server (cached under openmerc-data)
    [switch]$SysSymbols
)
$ErrorActionPreference = 'Stop'
$game = 'D:\Games\SoF-test-I1b'
$repo = 'C:\openmerc'
$data = Join-Path 'C:\openmerc-data\i1b' $Tag
New-Item -ItemType Directory -Force $data | Out-Null
$deskRun = Join-Path $repo 'engine\src\client\refresh\gl3\test\DeskRun.cs'
if (-not ('DeskRun' -as [type])) { Add-Type -Path $deskRun -ReferencedAssemblies System.Drawing }

if ($Engine -eq 'om') {
    $dir = Join-Path $game 'om'
    $exe = Join-Path $dir 'SoF.exe'
    if (-not $NoCopy) {
        Copy-Item (Join-Path $Build 'openMerc.exe') $exe -Force
        foreach ($f in 'SDL3.dll', 'ref_gl3.dll', 'ref_omgl.dll') {
            if (Test-Path (Join-Path $Build $f)) { Copy-Item (Join-Path $Build $f) (Join-Path $dir $f) -Force }
        }
    }
    $user = Join-Path $data 'user'
    if (Test-Path $user) { Remove-Item $user -Recurse -Force }
    New-Item -ItemType Directory -Force $user | Out-Null
    Copy-Item $Script (Join-Path $user 'omtest.cfg') -Force
    $u = $user -replace '\\', '/'
    $g = $game -replace '\\', '/'
    # the OpenMerc engine also takes at most 50 command-line words (Quake II MAX_NUM_ARGVS)
    Set-Content -Path (Join-Path $user 'i1bpre.cfg') -Encoding ASCII -Value @(
        'set in_grab 0', "set port $ServerPort", "set clientport $ClientPort")
    $cargs = "-portable -basedir $g +set user $u +set vid_fullscreen 0 +set vid_renderer gl3 " +
             "+set gl_mode 3 +set r_mode 4 +set logfile 3 +set s_nosound 1 " +
             "$Extra +exec i1bpre.cfg +map $Map $Post"
    $words = @($cargs -split '\s+' | Where-Object { $_ }).Count
    if ($words -gt 50) { throw "the engine takes at most 50 command-line words ($words)" }
    $shotDir = $null; $req = $null
} else {
    $dir = $game
    $exe = Join-Path $game 'SoF.exe'
    Copy-Item (Join-Path $CapBuild 'ref_omcap.dll') (Join-Path $game 'ref_omcap.dll') -Force
    $rel = 'u_i1b_' + $Tag
    $user = Join-Path $game $rel
    if (Test-Path $user) { Remove-Item $user -Recurse -Force }
    New-Item -ItemType Directory -Force $user | Out-Null
    Copy-Item $Script (Join-Path $user 'omtest.cfg') -Force
    # developer must stay 0: otherwise SoF.exe opens the "Developer Mode" message box (SPEC-301)
    # SoF.exe crashes with more than 50 command-line words: settings that need not be early go to i1bpre.cfg
    Set-Content -Path (Join-Path $user 'i1bpre.cfg') -Encoding ASCII -Value @(
        'set console 1', 'set developer 0', 'set in_mouse 0', 'set in_joystick 0')
    $cargs = "+set user ./$rel +set vid_ref omcap +set om_capref gl +set vid_fullscreen 0 " +
             "+set gl_mode 3 +set logfile 3 +set snd_dll defsnd.dll +set s_nosound 1 " +
             "+set port $ServerPort +set clientport $ClientPort +set ip_clientport $ClientPort " +
             "$Extra +exec i1bpre.cfg +map $Map $Post"
    $words = @($cargs -split '\s+' | Where-Object { $_ }).Count
    if ($words -gt 50) { throw "the original takes at most 50 command-line words ($words)" }
    $shotDir = Join-Path $env:TEMP 'omi1b_cap'
    New-Item -ItemType Directory -Force $shotDir | Out-Null
    Remove-Item (Join-Path $shotDir '*') -Force -ErrorAction SilentlyContinue
    $req = Join-Path $game 'omshot.req'
    if (Test-Path $req) { Remove-Item $req -Force }
}
$log = Join-Path $user 'sof.log'
if ($SysSymbols) { $env:_NT_SYMBOL_PATH = 'srv*C:/openmerc-data/symcache*https://msdl.microsoft.com/download/symbols' }

function Get-Own { @(Get-Process -Name SoF -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe }) }
$before = @(Get-Own | ForEach-Object { $_.Id })
$ojob = $null
try {
    if (($Engine -eq 'orig') -and $ReadyPattern) {
        $ojob = Start-Job -ScriptBlock {
            param($py, $log, $pat, $port, $delay, $to)
            & python -I $py --log $log --wait $pat --port $port --delay $delay --timeout $to --text 'exec omtest.cfg' 2>&1
        } -ArgumentList (Join-Path $repo 'tests\I6h\oobcmd.py'), $log, $ReadyPattern, $ClientPort, $ReadyDelay, $Timeout
    }
    $tjob = $null
    if ($TimedShots -and $req) {
        $tjob = Start-Job -ScriptBlock {
            param($req, $dir, $shots, $log, $from)
            while (-not ((Test-Path $log) -and (Select-String -Path $log -Pattern $from -Quiet))) {
                Start-Sleep -Milliseconds 20
            }
            $sw = [Diagnostics.Stopwatch]::StartNew()
            foreach ($s in ($shots -split ',')) {
                $n, $t = $s -split '@'
                while ($sw.Elapsed.TotalSeconds -lt [double]$t) { Start-Sleep -Milliseconds 50 }
                while (Test-Path $req) { Start-Sleep -Milliseconds 50 }
                [IO.File]::WriteAllText($req, (Join-Path $dir ($n.Trim() + '.tga')))
            }
        } -ArgumentList $req, $shotDir, $TimedShots, $log, $TimedFrom
    }
    Write-Host "$Engine`: SoF.exe $cargs"
    $sw = [Diagnostics.Stopwatch]::StartNew()
    # PowerShell passes $null strings as "": the OpenMerc client gets no log path, so DeskRun ignores the
    # script's OMSHOT lines (meant for the original) instead of writing to an empty path
    $mlog = if ($Engine -eq 'om') { $null } else { $log }
    $code = [DeskRun]::Run('OMTestI1b', $exe, $cargs, $dir, $Timeout * 1000, $mlog, $shotDir, 'SoF', $req)
    Write-Host ("exit code {0} after {1:N1} s{2}" -f $code, $sw.Elapsed.TotalSeconds, $(if ($code -eq -1) { ' (killed at timeout)' } else { '' }))
    if ($ojob) { Stop-Job $ojob -ErrorAction SilentlyContinue; Receive-Job $ojob | Write-Host }
} finally {
    Get-Own | Where-Object { $before -notcontains $_.Id } | ForEach-Object { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue }
    if ($ojob) { Remove-Job $ojob -Force -ErrorAction SilentlyContinue }
    if ($tjob) { Stop-Job $tjob -ErrorAction SilentlyContinue; Remove-Job $tjob -Force -ErrorAction SilentlyContinue }
    Start-Sleep -Milliseconds 300
    if (Test-Path $log) { Copy-Item $log (Join-Path $data 'client.log') -Force }
    $shots = if ($Engine -eq 'om') { Join-Path $user 'scrnshot' } else { $shotDir }
    $n = 0
    if ($shots -and (Test-Path $shots)) {
        foreach ($c in Get-ChildItem (Join-Path $shots '*.tga')) {
            python -I -c "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])" $c.FullName (Join-Path $data ($c.BaseName + '.png'))
            $n++
        }
    }
    if ($req -and (Test-Path $req)) { Remove-Item $req -Force }
    Write-Host "$n capture(s); logs in $data"
}
