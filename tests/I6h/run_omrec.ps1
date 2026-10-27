# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-311 test harness (no game code)
#
# An original 1.07f dedicated server from the I6h test copy and the OpenMerc client (run_om.ps1's install in
# <game>\om) joining it; the client runs -Script once it is in the level (cl_omexecactive). Both run on
# separate, never shown desktops (DeskRun.cs). Only the processes this script starts are stopped.
#   run_omrec.ps1 -Script tests\I6h\omrec.cfg -Tag omrec
param(
    [Parameter(Mandatory = $true)][string]$Script,
    [string]$Tag = 'omrec',
    [string]$Game = 'D:\Games\SoF-test-I6h',
    [string]$ServerArgs = '+set deathmatch 1 +set maxclients 4 +set timelimit 0 +set fraglimit 0 +map dm/jpndm2',
    [int]$ServerPort = 27966,
    [int]$Timeout = 180,
    [string]$User = 'C:\openmerc-data\I6h\home\user',
    [string]$OutDir = 'C:\openmerc-data\shots\I6h',
    [string]$DataDir = 'C:\openmerc-data\I6h',
    # relay the client through tools/net/udpproxy.py and keep the capture as <DataDir>\<Tag>.jsonl
    [switch]$Proxy,
    [int]$ProxyPort = 27968
)
$ErrorActionPreference = 'Stop'
$full = [IO.Path]::GetFullPath($Game).TrimEnd('\')
if ($full -ne 'D:\Games\SoF-test-I6h') { throw "refusing to run outside D:\Games\SoF-test-I6h ($full)" }
$exe = Join-Path $full 'SoF.exe'
$deskRun = 'C:\openmerc\engine\src\client\refresh\gl3\test\DeskRun.cs'
if (-not ('DeskRun' -as [type])) { Add-Type -Path $deskRun -ReferencedAssemblies System.Drawing }
$usersrv = Join-Path $full 'usersrv'
New-Item -ItemType Directory -Force $usersrv, $DataDir | Out-Null
$slog = Join-Path $usersrv 'sof.log'
if (Test-Path $slog) { Remove-Item $slog -Force }

function Get-OwnSoF { @(Get-Process -Name SoF -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe }) }
$before = @(Get-OwnSoF | ForEach-Object { $_.Id })
$server = $null
$job = $null
try {
    $sargs = "+set dedicated 1 +set user ./usersrv +set logfile 3 +set ip 127.0.0.1 +set port $ServerPort $ServerArgs"
    Write-Host "server: SoF.exe $sargs"
    $job = Start-Job -ScriptBlock {
        param($cs, $exe, $a, $dir, $to)
        Add-Type -Path $cs -ReferencedAssemblies System.Drawing
        [DeskRun]::Run('OMTestI6hSrv', $exe, $a, $dir, $to, $null, $null, 'SoF', $null)
    } -ArgumentList $deskRun, $exe, $sargs, $full, (($Timeout + 120) * 1000)
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
