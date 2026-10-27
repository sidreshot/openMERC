# SPDX-License-Identifier: GPL-2.0-or-later
# Black-box effects session for package I5, all processes on a separate, never shown desktop (FxRun.cs):
#   1. an ORIGINAL 1.07f dedicated server from the I5 test copy (D:\Games\SoF-test-I5);
#   2. tools/net/udpproxy.py between the OpenMerc client and the server (capture of every datagram);
#   3. optionally an ORIGINAL client (-OrigScript): renderer through the ref_omcap.dll capture shim
#      (I4a's refcap), joins directly, runs its script when it has entered the game (out-of-band "cmd"
#      packet, tests/I6h/oobcmd.py); `echo OMSHOT <name>` lines save frames as <OutDir>\<Tag>_orig_<name>.png;
#   4. the OpenMerc client of the I5 build (installed by run_om.ps1 in <game>\om), joining through the
#      relay with om_fxtrace 1, running -OmScript once in the level (cl_omexecactive); its `omshot <name>`
#      captures (I6h) become <OutDir>\<Tag>_om_<name>.png.
# Logs and the capture go to <DataDir>\<Tag>-*. Only processes started here are stopped.
#   run_session.ps1 -OmScript fire_om.cfg [-OrigScript fire_orig.cfg] -Tag fire
# Implements: SPEC-400 / SPEC-401 test harness (no game code)
param(
    [Parameter(Mandatory = $true)][string]$OmScript,
    [string]$OrigScript = '',
    # -Listen: script the original client executes at start-up (it starts the map)
    [string]$OrigStartScript = '',
    [string]$Tag = 'session',
    [string]$Game = 'D:\Games\SoF-test-I5',
    [string]$ServerArgs = '+set deathmatch 1 +set maxclients 4 +set timelimit 0 +set fraglimit 0 +map dm/jpndm2',
    # default: the OpenMerc client is a spectator (chases the first player); '' joins it as a player
    [string]$OmExtra = '+set spectator 1',
    # the original client's script starts when this pattern appears in the OpenMerc log (empty: when the
    # original client itself has entered the game)
    [string]$OrigTrigger = 'OMTEST chasing',
    # without an OpenMerc client (-NoOm): the original client's script starts when this appears in its own log
    [string]$OrigWait = 'Initialized',
    [string]$OrigExtra = '',
    # package I5's ports (testing notes "Network ports": base 29420): server 29420, relay 29421,
    # original client 29422, OpenMerc client 29423
    [int]$ServerPort = 29420,
    [int]$ProxyPort = 29421,
    [int]$OrigPort = 29422,
    [int]$OmPort = 29423,
    [int]$Timeout = 180,
    [string]$OmUser = 'C:\openmerc-data\i5\home\user',
    [string]$OutDir = 'C:\openmerc-data\shots\I5',
    [string]$DataDir = 'C:\openmerc-data\i5\sessions',
    # capture shim with the om_capview eye override (gl3/test/refcap/build.ps1 -Out <this>)
    [string]$CapBuild = 'C:\openmerc\build-I5-x86\refcap',
    [switch]$NoCopy,
    # no OpenMerc client: run the original client's script only (waits for it to quit)
    [switch]$NoOm,
    # the original client hosts the game itself (listen server on -ServerPort, -ServerArgs) instead of a
    # separate dedicated server
    [switch]$Listen,
    # seconds to wait after the original client started before the OpenMerc client starts
    [int]$OrigLead = 30,
    # a second ORIGINAL client origB joins as a spectator (-Listen only) and runs this script when the OpenMerc
    # client is chasing: it then takes the captures and renderer traces (OMSHOT/OMTRACE in its log, files
    # <Tag>_origB_<name>.png and <Tag>-traces), and origA uses the plain renderer. Effects the server sends to
    # everybody but the shooter (MPG beam, flame stream: svc_effect) are compared spectator against spectator.
    [string]$OrigBScript = '',
    [int]$OrigBPort = 29425
)
$ErrorActionPreference = 'Stop'
$full = [IO.Path]::GetFullPath($Game).TrimEnd('\')
if ($full -ne 'D:\Games\SoF-test-I5') { throw "refusing to run outside D:\Games\SoF-test-I5 ($full)" }
$repo = 'C:\openmerc'
$exe = Join-Path $full 'SoF.exe'
$omexe = Join-Path $full 'om\SoF.exe'
New-Item -ItemType Directory -Force $OutDir, $DataDir, $OmUser | Out-Null
if (-not ('FxRun' -as [type])) { Add-Type -Path (Join-Path $PSScriptRoot 'FxRun.cs') -ReferencedAssemblies System.Drawing }

# OpenMerc install (as run_om.ps1)
if (-not $NoCopy) {
    & (Join-Path $PSScriptRoot 'run_om.ps1') -Extra '+quit' -Timeout 40 -Out (Join-Path $DataDir "$Tag-install.log") | Out-Null
}
Copy-Item $OmScript (Join-Path $OmUser 'omtest.cfg') -Force
$omlog = Join-Path $OmUser 'sof.log'
if (Test-Path $omlog) { Remove-Item $omlog -Force }
$omshots = Join-Path $OmUser 'scrnshot'
if (Test-Path $omshots) { Remove-Item (Join-Path $omshots '*') -Force -ErrorAction SilentlyContinue }
# om_fxdump scene dumps of this session (moved to <DataDir>\<Tag>-omdump afterwards)
$omdump = Join-Path $OmUser 'fxdump'
if (Test-Path $omdump) { Remove-Item $omdump -Recurse -Force }

$usersrv = Join-Path $full 'usersrv'
$userA = Join-Path $full 'userA'
New-Item -ItemType Directory -Force $usersrv, $userA | Out-Null
$slog = Join-Path $usersrv 'sof.log'
$alog = Join-Path $userA 'sof.log'
foreach ($f in $slog, $alog) { if (Test-Path $f) { Remove-Item $f -Force } }
$cap = Join-Path $DataDir "$Tag-om.jsonl"
$capdir = Join-Path $env:TEMP 'omi5_cap'
New-Item -ItemType Directory -Force $capdir | Out-Null
Remove-Item (Join-Path $capdir '*') -Force -ErrorAction SilentlyContinue
$req = Join-Path $full 'omshot.req'
if (Test-Path $req) { Remove-Item $req -Force }
# `echo OMTRACE <name>` lines: one frame of renderer calls (entity lists) per line, <DataDir>\<Tag>-traces
$treq = Join-Path $full 'omtrace.req'
if (Test-Path $treq) { Remove-Item $treq -Force }

[FxRun]::OpenDesktop('OMTestI5')
try {
    if (-not $Listen) {
        $sargs = "+set dedicated 1 +set user ./usersrv +set logfile 3 +set ip 127.0.0.1 +set port $ServerPort $ServerArgs"
        Write-Host "server: SoF.exe $sargs"
        $srv = [FxRun]::Start($exe, $sargs, $full)
        $sw = [Diagnostics.Stopwatch]::StartNew()
        while ($sw.Elapsed.TotalSeconds -lt 40) {
            Start-Sleep -Milliseconds 300
            if ((Test-Path $slog) -and ((Get-Content $slog -Raw) -match 'Initialized')) { break }
        }
        Start-Sleep -Seconds 2
        Write-Host ("server up after {0:N1} s" -f $sw.Elapsed.TotalSeconds)
    }

    # the interpreter itself (`python` may be a .cmd wrapper whose child would survive a kill)
    $py = (& python -c "import sys; print(sys.executable)").Trim()
    # -Listen: the original client's sockets must be on the LAN address, the client ignores out-of-band
    # "cmd" packets from 127.0.0.1; its start script sets ip, port and public 0 before the map
    $srvip = '127.0.0.1'
    if ($Listen) {
        # the address the game itself reports as "LOCAL IP #1" (first IPv4 address of the host name)
        $srvip = ([System.Net.Dns]::GetHostAddresses([System.Net.Dns]::GetHostName()) |
                  Where-Object { $_.AddressFamily -eq 'InterNetwork' } | Select-Object -First 1).IPAddressToString
        $start = @("set ip $srvip", "set port $ServerPort", 'set public 0') + (Get-Content $OrigStartScript)
        Set-Content -Path (Join-Path $userA 'omstart.cfg') -Value $start -Encoding ascii
        Write-Host "listen server on $srvip`:$ServerPort"
    }
    $proxy = [FxRun]::Start($py, "-I `"$repo\tools\net\udpproxy.py`" --listen 127.0.0.1:$ProxyPort --server $srvip`:$ServerPort --out `"$cap`" --quiet", $DataDir)

    $orig = 0
    if ($OrigScript) {
        Copy-Item (Join-Path $CapBuild 'ref_omcap.dll') (Join-Path $full 'ref_omcap.dll') -Force
        Copy-Item $OrigScript (Join-Path $userA 'omtest.cfg') -Force
        # omstart.cfg is written above (-Listen)
        # with origB the request files belong to origB's shim: origA draws with the plain original renderer
        $aref = if ($OrigBScript) { 'gl' } else { 'omcap' }
        $aargs = "+set user ./userA +set vid_ref $aref +set om_capref gl +set vid_fullscreen 0 +set gl_mode 3 " +
                 "+set vid_xpos 0 +set vid_ypos 0 +set logfile 3 +set snd_dll OMsnd +set s_om_focusmute 0 +set ip 127.0.0.1 +set cl_maxfps 100 " +
                 "+set name origA +set clientport $OrigPort $OrigExtra "
        if ($Listen) { $aargs += "+exec omstart.cfg" } else { $aargs += "+connect 127.0.0.1:$ServerPort" }
        Write-Host "original client: SoF.exe $aargs"
        $orig = [FxRun]::Start($exe, $aargs, $full)
        if (-not $OrigBScript) { [FxRun]::Watch($alog, $capdir, $req, $orig, 'SoF', $treq) }
        if ($OrigTrigger -and -not $NoOm) { $tlog = $omlog; $tpat = $OrigTrigger } else { $tlog = $alog; $tpat = $OrigWait }
        $oob = [FxRun]::Start($py, "-I `"$repo\tests\I6h\oobcmd.py`" --log `"$tlog`" --wait `"$tpat`" --port $OrigPort --text `"exec omtest.cfg`" --timeout 150 --delay 1", $DataDir)
        if ($OrigBScript -and $Listen) {
            # origB: a second original client, spectator, captures and traces through its shim
            Start-Sleep -Seconds 20
            $userB = Join-Path $full 'userB'
            New-Item -ItemType Directory -Force $userB | Out-Null
            $blog = Join-Path $userB 'sof.log'
            if (Test-Path $blog) { Remove-Item $blog -Force }
            Copy-Item $OrigBScript (Join-Path $userB 'omtest.cfg') -Force
            $bargs = "+set user ./userB +set vid_ref omcap +set om_capref gl +set vid_fullscreen 0 +set gl_mode 3 " +
                     "+set vid_xpos 0 +set vid_ypos 0 +set logfile 3 +set snd_dll OMsnd +set s_om_focusmute 0 +set cl_maxfps 100 " +
                     "+set name origB +set spectator 1 +set clientport $OrigBPort +connect $srvip`:$ServerPort"
            Write-Host "original spectator: SoF.exe $bargs"
            $origb = [FxRun]::Start($exe, $bargs, $full)
            [FxRun]::Watch($blog, $capdir, $req, $origb, 'SoF', $treq)
            $oobb = [FxRun]::Start($py, "-I `"$repo\tests\I6h\oobcmd.py`" --log `"$tlog`" --wait `"$tpat`" --port $OrigBPort --text `"exec omtest.cfg`" --timeout 150 --delay 1", $DataDir)
        }
        # give the original client a lead before the OpenMerc client starts
        Start-Sleep -Seconds $OrigLead
    }

    if ($NoOm) {
        $code = [FxRun]::Wait($orig, $Timeout * 1000)
        Write-Host "original client exit $code"
        if ($code -eq -1) { Write-Host ('original client windows: ' + [FxRun]::ListWindows($orig)) }
    }
    else {
    $u = $OmUser -replace '\\', '/'
    $g = $full -replace '\\', '/'
    # both engines stop with more than 50 command-line words: the connection setup goes into an exec file
    # (+set lines of -OmExtra become set lines); video, user dir and clientport stay early +set commands
    $conn = @('set name omC', 'set om_fxtrace 1')
    foreach ($part in ($OmExtra -split '\+')) { if ($part.Trim()) { $conn += $part.Trim() } }
    $conn += @('set cl_omexecactive omtest.cfg', "connect 127.0.0.1:$ProxyPort")
    Set-Content -Path (Join-Path $OmUser 'omconnect.cfg') -Value $conn -Encoding ascii
    $oargs = "-portable -basedir $g +set user $u +set vid_fullscreen 0 +set in_grab 0 +set vid_renderer gl3 " +
             "+set gl_mode 3 +set r_mode 4 +set console 1 +set logfile 3 +set s_nosound 1 +set con_notifytime 0 " +
             "+set clientport $OmPort +exec omconnect.cfg"
    Write-Host "OpenMerc client: SoF.exe $oargs"
    $om = [FxRun]::Start($omexe, $oargs, (Split-Path $omexe))
    $code = [FxRun]::Wait($om, $Timeout * 1000)
    if ($code -eq -1) { Write-Host 'OpenMerc client: timeout, killed' } else { Write-Host "OpenMerc client exit $code" }
    if ($orig) { if ([FxRun]::Wait($orig, 20000) -eq -1) { Write-Host 'original client: killed' } }
    }
} finally {
    [FxRun]::KillAll()
}
if (Test-Path $omlog) { Copy-Item $omlog (Join-Path $DataDir "$Tag-om.log") -Force }
if (Test-Path $slog) { Copy-Item $slog (Join-Path $DataDir "$Tag-server.log") -Force }
if (Test-Path $alog) { Copy-Item $alog (Join-Path $DataDir "$Tag-orig.log") -Force }
$blog = Join-Path $full 'userB\sof.log'
if ($OrigBScript -and (Test-Path $blog)) { Copy-Item $blog (Join-Path $DataDir "$Tag-origB.log") -Force }
$n = 0
$bursts = 0
if (Test-Path $omshots) {
    foreach ($t in Get-ChildItem (Join-Path $omshots '*.tga')) {
        # fx* = om_fxshot burst frames: paired below instead of converted one by one
        if ($t.BaseName -like 'fx*') { $bursts++; continue }
        python -I -c "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])" $t.FullName (Join-Path $OutDir ("{0}_om_{1}.png" -f $Tag, $t.BaseName))
        $n++
    }
}
foreach ($c in Get-ChildItem (Join-Path $capdir '*.tga')) {
    python -I -c "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])" $c.FullName (Join-Path $OutDir ("{0}_orig_{1}.png" -f $Tag, $c.BaseName))
    $n++
}
Write-Host "$n capture(s) -> $OutDir; logs -> $DataDir"
$traces = @(Get-ChildItem (Join-Path $capdir '*.txt') -ErrorAction SilentlyContinue)
if ($traces.Count -gt 0) {
    $tdir = Join-Path $DataDir "$Tag-traces"
    New-Item -ItemType Directory -Force $tdir | Out-Null
    Remove-Item (Join-Path $tdir '*') -Force -ErrorAction SilentlyContinue
    foreach ($t in $traces) { Move-Item $t.FullName $tdir -Force }
    Write-Host ("{0} renderer trace(s) -> {1}" -f $traces.Count, $tdir)
}
if (Test-Path $treq) { Remove-Item $treq -Force }
if (Test-Path $omdump) {
    $ddir = Join-Path $DataDir "$Tag-omdump"
    if (Test-Path $ddir) { Remove-Item $ddir -Recurse -Force }
    Move-Item $omdump $ddir
    Write-Host ("OpenMerc scene dumps -> {0}" -f $ddir)
}
if ($bursts -gt 0) {
    Write-Host "$bursts burst frame(s): pairing (fxpairs.py)"
    python -I (Join-Path $PSScriptRoot 'fxpairs.py') --om $omshots --orig $capdir --tag $Tag --out (Join-Path $OutDir $Tag)
}
