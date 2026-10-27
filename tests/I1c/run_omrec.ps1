# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-311 test harness (no game code)
#
# An original 1.07f dedicated server from the I1c test copy and the OpenMerc client (run_om.ps1's install in
# <game>\om) joining it; the client runs -Script once it is in the level (cl_omexecactive). Both run on
# separate, never shown desktops (DeskRun.cs). Only the processes this script starts are stopped.
#   run_omrec.ps1 -Script tests\I1c\omrec.cfg -Tag omrec
param(
    [Parameter(Mandatory = $true)][string]$Script,
    [string]$Tag = 'omrec',
    [string]$Game = 'D:\Games\SoF-test-I1c',
    [string]$ServerArgs = '+exec i1cdev.cfg +set deathmatch 1 +set maxclients 4 +set timelimit 0 +set fraglimit 0 +map dm/jpndm2',
    [int]$ServerPort = 29540,
    [int]$Timeout = 180,
    [string]$User = 'C:\openmerc-data\I1c\home\user',
    [string]$OutDir = 'C:\openmerc-data\shots\I1c',
    [string]$DataDir = 'C:\openmerc-data\I1c',
    # relay the client through tools/net/udpproxy.py and keep the capture as <DataDir>\<Tag>.jsonl
    [switch]$Proxy,
    # I1c: the OpenMerc dedicated server (openMerc-ded.exe of -Build, -portable, user dir under -DataDir) instead
    [switch]$OmServer,
    [string]$Build = 'C:\openmerc\build-I1c-x86\release',
    [int]$ProxyPort = 29542
)
$ErrorActionPreference = 'Stop'
$full = [IO.Path]::GetFullPath($Game).TrimEnd('\')
if ($full -ne 'D:\Games\SoF-test-I1c') { throw "refusing to run outside D:\Games\SoF-test-I1c ($full)" }
$exe = Join-Path $full 'SoF.exe'
if ($OmServer) {
    New-Item -ItemType Directory -Force (Join-Path $full 'om') | Out-Null
    Copy-Item (Join-Path $Build 'openMerc-ded.exe') (Join-Path $full 'om\openMerc-ded.exe') -Force
    $exe = Join-Path $full 'om\openMerc-ded.exe'
}
$deskRun = 'C:\openmerc\engine\src\client\refresh\gl3\test\DeskRun.cs'
if (-not ('DeskRun' -as [type])) { Add-Type -Path $deskRun -ReferencedAssemblies System.Drawing }
$usersrv = Join-Path $full 'usersrv'
if ($OmServer) { $usersrv = Join-Path $DataDir 'omsrv\user' }
New-Item -ItemType Directory -Force $usersrv, $DataDir | Out-Null
$slog = Join-Path $usersrv 'sof.log'
if (Test-Path $slog) { Remove-Item $slog -Force }

function Get-OwnSoF { @(Get-Process -Name SoF, openMerc-ded -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe }) }
$before = @(Get-OwnSoF | ForEach-Object { $_.Id })
$server = $null
$job = $null
try {
    $sargs = "+set dedicated 1 +set user ./usersrv +set logfile 3 +set ip 127.0.0.1 +set port $ServerPort $ServerArgs"
    if ($OmServer) {
        # -user (not +set user): with +set the server's log moved away after start-up (I1b's svtest.py form)
        $sargs = "-portable -basedir $($full -replace '\\', '/') -user $($usersrv -replace '\\', '/') " +
                 "+set logfile 3 +set ip 127.0.0.1 +set port $ServerPort $ServerArgs"
    }
    Write-Host "server: $exe $sargs"
    if ($OmServer) {
        # the console server as I1b's svtest.py starts it: own hidden console (on DeskRun's hidden desktop it
        # stopped after start-up without loading the map)
        $server = Start-Process -FilePath $exe -ArgumentList $sargs -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -PassThru
    } else {
    $job = Start-Job -ScriptBlock {
        param($cs, $exe, $a, $dir, $to)
        Add-Type -Path $cs -ReferencedAssemblies System.Drawing
        [DeskRun]::Run('OMTestI1cSrv', $exe, $a, $dir, $to, $null, $null, 'SoF', $null)
    } -ArgumentList $deskRun, $exe, $sargs, (Split-Path $exe), (($Timeout + 120) * 1000)
    }
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt 30) {
        Start-Sleep -Milliseconds 300
        if (-not $server) { $server = Get-OwnSoF | Where-Object { $before -notcontains $_.Id } | Select-Object -First 1 }
        if ((Test-Path $slog) -and ((Get-Content $slog -Raw) -match 'Initialized')) { break }
    }
    if (-not $server) { throw 'dedicated server did not start' }
    Start-Sleep -Seconds 3
    Write-Host ("server pid {0} up after {1:N1} s" -f $server.Id, $sw.Elapsed.TotalSeconds)

    $port = $ServerPort
    $relay = $null
    if ($Proxy) {
        $cap = Join-Path $DataDir "$Tag.jsonl"
        $relay = Start-Process -FilePath python -ArgumentList @('-I', 'C:\openmerc\tools\net\udpproxy.py',
            '--listen', "127.0.0.1:$ProxyPort", '--server', "127.0.0.1:$ServerPort", '--out', $cap,
            '--seconds', "$($Timeout + 30)", '--quiet') -WindowStyle Hidden -PassThru
        Start-Sleep -Seconds 1
        $port = $ProxyPort
    }
    & (Join-Path $PSScriptRoot 'run_om.ps1') -Script $Script -Tag $Tag -Timeout $Timeout -User $User -OutDir $OutDir `
        -Extra "+set cl_omexecactive omtest.cfg +connect 127.0.0.1:$port" -NoScriptExec
    if ($relay -and -not $relay.HasExited) { Stop-Process -Id $relay.Id -Force -ErrorAction SilentlyContinue }
} finally {
    if ($server -and -not $server.HasExited) {
        Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue
        $server.WaitForExit(5000) | Out-Null
    }
    Get-OwnSoF | Where-Object { $before -notcontains $_.Id } | ForEach-Object { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue }
    if ($job) { Wait-Job $job -Timeout 15 | Out-Null; Remove-Job $job -Force -ErrorAction SilentlyContinue }
    if (Test-Path $slog) { Copy-Item $slog (Join-Path $DataDir "$Tag-server.log") -Force }
}
