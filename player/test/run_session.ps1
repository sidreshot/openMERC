# SPDX-License-Identifier: GPL-2.0-or-later
# Black-box session of the original SoF.exe 1.07f with a chosen player module (SPEC-003).
#
# Works only on the I7 swap-in copies (default D:\Games\SoF-test-I7, server copy D:\Games\SoF-test-I7-srv), never on
# the oracle D:\Games\SoF-oracle or the main install. Installs the tracing proxy (omptrace.dll) as base\player.dll,
# pointing it at the original module (player.dll.orig) or at OpenMerc's (player.om.dll), runs console scripts,
# collects sof.log and the traces, and restores the original player.dll afterwards. Only the processes this script
# starts are stopped. The games run on a separate desktop that is never shown (-Desktop; '' = the owner's desktop,
# minimised and not activated); all input comes from the console scripts and mp_drive.py.
#
#   -Mode listen   one process: the client script starts a local game itself (map ...)
#   -Mode mp       a dedicated server (server copy, -ServerScript) and a client (-Script) that connects to it
#                  (the client predicts weapons, SPEC-602)
#   -Module / -ServerModule   orig | om        -NoTrace   install the module directly, no proxy
# Implements: SPEC-003, SPEC-600, SPEC-601, SPEC-602 (test vectors)
param(
    [ValidateSet('listen', 'mp')][string]$Mode = 'listen',
    [ValidateSet('orig', 'om')][string]$Module = 'orig',
    [ValidateSet('orig', 'om')][string]$ServerModule = 'orig',
    [Parameter(Mandatory = $true)][string]$Script,
    [string]$ServerScript = '',
    [string]$OutDir = (Join-Path $env:TEMP 'omplayer-test'),
    [string]$Game = 'D:\Games\SoF-test-I7',
    [string]$ServerGame = 'D:\Games\SoF-test-I7-srv',
    [string]$Build = (Join-Path $PSScriptRoot '..\..\build-player-x86-Release'),
    [string]$Extra = '',
    # I7's ports (docs/process/testing.md "Network ports"): servers 29430, clients 29431; never the
    # defaults, other packages run games on this PC at the same time
    [int]$ServerPort = 29430,
    [int]$ClientPort = 29431,
    # mp: the client connects to the relay (mp_relay.py) on this port, which forwards to the server
    [int]$RelayPort = 29432,
    [string]$ServerExtra = '+set deathmatch 1 +set maxclients 4 +map dm/nycdm4',
    [int]$Timeout = 240,
    [int]$ServerDelay = 8,
    # mp: a real-time command script for mp_drive.py (out-of-band cmd packets to the client)
    [string]$Drive = '',
    [switch]$NoTrace,
    [switch]$TraceVerbose,
    # verbose trace of the server only (every import call, e.g. the fire-time getters 13/15 with zero results)
    [switch]$ServerTraceVerbose,
    # parental-lock words valid for this PC are written only while the games start up, then the key is restored
    # byte-identically from this backup (sof_locks.py; see docs/impl/I7.md "Parental locks")
    [string]$LockBackup = 'C:\openmerc-data\i7\sof-registry-backup.json',
    [switch]$NoUnlock,
    # games run on this never-shown desktop; '' = the owner's desktop (minimised, not activated)
    [string]$Desktop = 'OMTestI7'
)
$ErrorActionPreference = 'Stop'

if (-not ('OmLaunch2' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class OmLaunch2 {
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct SI { public int cb; public string reserved, desktop, title; public int x, y, xs, ys, xc, yc, fill, flags;
                public short show, r2; public IntPtr r3, hin, hout, herr; }
    [StructLayout(LayoutKind.Sequential)]
    struct PI { public IntPtr hp, ht; public int pid, tid; }
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern bool CreateProcessW(string app, StringBuilder cmd, IntPtr pa, IntPtr ta, bool inherit, int flags,
                                      IntPtr env, string dir, ref SI si, out PI pi);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr CreateDesktopW(string name, IntPtr dev, IntPtr devmode, int flags, uint access, IntPtr sa);
    static IntPtr desk = IntPtr.Zero;
    // desktop "": the owner's desktop, window minimised and not activated (STARTF_USESHOWWINDOW, SW_SHOWMINNOACTIVE);
    // otherwise a separate desktop of the interactive window station that is never shown: no window of the test
    // (game or dedicated-server console) can appear on the owner's screen, take the focus or receive input.
    // The handle stays open for the life of this PowerShell process; the desktop goes away with its last user.
    public static int Start(string exe, string args, string dir, string desktop) {
        SI si = new SI(); si.cb = Marshal.SizeOf(typeof(SI));
        if (!string.IsNullOrEmpty(desktop)) {
            if (desk == IntPtr.Zero) {
                desk = CreateDesktopW(desktop, IntPtr.Zero, IntPtr.Zero, 0, 0x10000000, IntPtr.Zero);
                if (desk == IntPtr.Zero) throw new System.ComponentModel.Win32Exception();
            }
            si.desktop = desktop;
        } else { si.flags = 1; si.show = 7; }
        PI pi;
        StringBuilder cmd = new StringBuilder("\"" + exe + "\" " + args);
        if (!CreateProcessW(exe, cmd, IntPtr.Zero, IntPtr.Zero, false, 0, IntPtr.Zero, dir, ref si, out pi))
            throw new System.ComponentModel.Win32Exception();
        CloseHandle(pi.ht); CloseHandle(pi.hp);
        return pi.pid;
    }
}
'@
}

function Assert-TestCopy([string]$dir) {
    $full = [IO.Path]::GetFullPath($dir).TrimEnd('\')
    if ($full -like '*\SoF-oracle*' -or $full -like '*\Soldier of Fortune*') { throw "refusing to touch $full" }
    if ($full -notlike '*\SoF-test-I7*') { throw "not an I7 test copy: $full" }
    $orig = Join-Path $full 'base\player.dll.orig'
    if (-not (Test-Path $orig)) { throw "no $orig" }
    if ((Get-Item $orig).Length -ne 110592) { throw "$orig is not the 1.07f player.dll" }
}

function Install-Module([string]$dir, [string]$which, [string]$logPath) {
    $base = Join-Path $dir 'base'
    $live = Join-Path $base 'player.dll'
    if ($which -eq 'om') { Copy-Item (Join-Path $Build 'player.dll') (Join-Path $base 'player.om.dll') -Force }
    if ($NoTrace) {
        if ($which -eq 'om') { Copy-Item (Join-Path $Build 'player.dll') $live -Force }
        else { Copy-Item (Join-Path $base 'player.dll.orig') $live -Force }
    } else {
        Copy-Item (Join-Path $Build 'omptrace.dll') $live -Force
    }
    if (Test-Path $logPath) { Remove-Item $logPath -Force }
}

function Restore-Module([string]$dir) {
    $base = Join-Path $dir 'base'
    Copy-Item (Join-Path $base 'player.dll.orig') (Join-Path $base 'player.dll') -Force
}

function Start-Game([string]$dir, [string]$which, [string]$traceLog, [string]$argv, [bool]$verbose) {
    $env:OMTRACE_TARGET = $(if ($which -eq 'om') { 'player.om.dll' } else { 'player.dll.orig' })
    $env:OMTRACE_LOG = $traceLog
    $env:OMTRACE_VERBOSE = $(if ($verbose) { '1' } else { '0' })
    $exe = Join-Path $dir 'SoF.exe'
    Write-Host "start $exe $argv"
    $procId = [OmLaunch2]::Start($exe, $argv, $dir, $Desktop)
    return Get-Process -Id $procId
}

function Stop-Game($p) {
    if ($p -and -not $p.HasExited) {
        Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
        $p.WaitForExit(5000) | Out-Null
    }
}

$locks = Join-Path $PSScriptRoot 'sof_locks.py'
function Lock-Restore {
    if ($NoUnlock) { return }
    & python -I $locks restore $LockBackup | Write-Host
    if ($LASTEXITCODE -ne 0) { Write-Warning 'parental-lock key NOT identical to the backup' }
}
function Wait-Started([string]$log, [int]$seconds, [string]$pattern = '^execing om') {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt $seconds) {
        if ((Test-Path $log) -and (Select-String -Path $log -Pattern $pattern -Quiet)) { return $true }
        Start-Sleep -Milliseconds 200
    }
    return $false
}

function Prepare-User([string]$dir, [string]$scriptFile, [string]$cfgName) {
    $user = Join-Path $dir 'user'
    Copy-Item $scriptFile (Join-Path $user $cfgName) -Force
    # shared helpers of the session scripts (exec omtest_common.cfg)
    $common = Join-Path (Split-Path $scriptFile) 'common.cfg'
    if (Test-Path $common) { Copy-Item $common (Join-Path $user 'omtest_common.cfg') -Force }
    $log = Join-Path $user 'sof.log'
    if (Test-Path $log) { Remove-Item $log -Force }
    return $log
}

Assert-TestCopy $Game
if ($Mode -eq 'mp') { Assert-TestCopy $ServerGame }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$common = '+set vid_fullscreen 0 +set gl_mode 3 +set logfile 3 +set in_mouse 0 +set in_joystick 0 +set s_volume 0 '
$cl = $null; $sv = $null
$clTrace = Join-Path $OutDir 'trace-client.log'
$svTrace = Join-Path $OutDir 'trace-server.log'
# Multiplayer never writes the registry: the client connects through mp_relay.py, which sets cl_violence 0 in the
# connect userinfo (both games run with forced locks). Only listen games (loopback, no relay possible) still need
# valid "off" words for the seconds of the start-up.
if ($Mode -eq 'mp') { $NoUnlock = $true }
& python -I $locks verify $LockBackup | Write-Host
if ($LASTEXITCODE -ne 0) { throw "parental-lock key differs from $LockBackup - not touching it" }
$relay = $null
$relayStop = Join-Path $OutDir 'relay.stop'
try {
    if (-not $NoUnlock) { & python -I $locks unlock | Write-Host }
    if ($Mode -eq 'mp') {
        if (-not $ServerScript) { throw '-ServerScript is required for -Mode mp' }
        Install-Module $ServerGame $ServerModule $svTrace
        $svLog = Prepare-User $ServerGame $ServerScript 'omsrv.cfg'
        # the original dedicated server wants developer 1 set from an exec'd file before the map starts: on the
        # command line it opens a modal box (never starts on the hidden desktop), with 0 it may not spawn the map
        # (docs/process/testing.md)
        Set-Content -Path (Join-Path $ServerGame 'user\omsrvdev.cfg') -Value 'set developer 1' -Encoding ascii
        $sv = Start-Game $ServerGame $ServerModule $svTrace "+set dedicated 1 +exec omsrvdev.cfg +set logfile 3 +set cheats 1 +set ip 127.0.0.1 +set port $ServerPort $ServerExtra +exec omsrv.cfg" ($TraceVerbose -or $ServerTraceVerbose)
        # the lock store is read during start-up (SPEC-313 2.3): restore the key as soon as start-up is done and
        # unlock again only for the client's start-up, so other test runs on this PC are blocked for less time
        if (-not (Wait-Started $svLog 120 'Soldier of Fortune Initialized')) { Write-Warning 'server init not seen' }
        if (-not (Wait-Started $svLog 180)) { Write-Warning 'server start-up not seen in its log' }
        Start-Sleep -Seconds $ServerDelay
        if (Test-Path $relayStop) { Remove-Item $relayStop -Force }
        $relay = Start-Process -FilePath python -ArgumentList @('-I', (Join-Path $PSScriptRoot 'mp_relay.py'),
            '--listen', $RelayPort, '--server', $ServerPort, '--seconds', ($Timeout + 900), '--stopfile', $relayStop,
            '--log', (Join-Path $OutDir 'relay.log')) -WindowStyle Hidden -PassThru
    }
    Install-Module $Game $Module $clTrace
    $clLog = Prepare-User $Game $Script 'omtest.cfg'
    # listen games bind to 127.0.0.1 (no LAN exposure); a client of the dedicated server must not take its port
    $ipArg = $(if ($Mode -eq 'mp') { '' } else { "+set ip 127.0.0.1 +set port $ServerPort" })
    $cl = Start-Game $Game $Module $clTrace "$ipArg +set clientport $ClientPort $common +set cheats 1 $Extra +exec omtest.cfg" $TraceVerbose.IsPresent
    if (-not (Wait-Started $clLog 30)) { Write-Warning 'client start-up not seen in its log' }
    Lock-Restore
    if ($Drive) {
        & python -I (Join-Path $PSScriptRoot 'mp_drive.py') $cl.Id $Drive --log $clLog --server-port $ServerPort |
            Tee-Object -FilePath (Join-Path $OutDir 'drive.log') | Out-Null
    }
    $sw = [Diagnostics.Stopwatch]::StartNew()
    if (-not $cl.WaitForExit($Timeout * 1000)) { Write-Host "client timeout after $Timeout s - stopping it" }
    Write-Host ("client finished after {0:N1} s" -f $sw.Elapsed.TotalSeconds)
} finally {
    Lock-Restore
    Stop-Game $cl
    Stop-Game $sv
    if ($relay) {
        New-Item -ItemType File -Force $relayStop | Out-Null
        if (-not $relay.WaitForExit(5000)) { Stop-Process -Id $relay.Id -Force -ErrorAction SilentlyContinue }
        Remove-Item $relayStop -Force -ErrorAction SilentlyContinue
    }
    Start-Sleep -Milliseconds 500
    if (Test-Path (Join-Path $Game 'user\sof.log')) { Copy-Item (Join-Path $Game 'user\sof.log') (Join-Path $OutDir 'sof-client.log') -Force }
    Restore-Module $Game
    if ($Mode -eq 'mp') {
        if (Test-Path (Join-Path $ServerGame 'user\sof.log')) { Copy-Item (Join-Path $ServerGame 'user\sof.log') (Join-Path $OutDir 'sof-server.log') -Force }
        Restore-Module $ServerGame
    }
    Remove-Item Env:OMTRACE_TARGET, Env:OMTRACE_LOG, Env:OMTRACE_VERBOSE -ErrorAction SilentlyContinue
    # a connect from another package's server (or a second serverdata) makes the run worthless: flag it
    $clCopy = Join-Path $OutDir 'sof-client.log'
    if (Test-Path $clCopy) {
        $want = $(if ($Mode -eq 'mp') { "127.0.0.1:${RelayPort}: client_connect" } else { 'loopback: client_connect' })
        $foreign = Select-String -Path $clCopy -Pattern '^\S+: client_connect' | Where-Object { $_.Line.Trim() -ne $want }
        if ($foreign) { Write-Warning "DISCARD this run: unrequested connect(s): $($foreign.Line -join '; ')" }
    }
    Write-Host "outputs in $OutDir"
}
