# SPDX-License-Identifier: GPL-2.0-or-later
# Runs one session script with the original player.dll and with OpenMerc's (both behind the tracing proxy) and
# writes the normalised traces and their diff: <Out>\<Name>-orig\, <Name>-om\, <Name>.diff
# Implements: SPEC-003, SPEC-600, SPEC-601 (acceptance comparison)
param(
    [Parameter(Mandatory = $true)][string]$Script,
    [Parameter(Mandatory = $true)][string]$Name,
    [string]$Out = 'C:\openmerc-data\i7',
    [int]$Timeout = 400,
    [switch]$SkipOrig,
    [switch]$TraceVerbose
)
$ErrorActionPreference = 'Stop'
$run = Join-Path $PSScriptRoot 'run_session.ps1'
$view = Join-Path $PSScriptRoot 'trace_view.py'
$extra = '+set cl_maxfps 1000 +set gl_swapinterval 0'
$mods = if ($SkipOrig) { @('om') } else { @('orig', 'om') }
foreach ($m in $mods) {
    $o = Join-Path $Out "$Name-$m"
    & $run -Module $m -Script $Script -OutDir $o -Timeout $Timeout -Extra $extra -TraceVerbose:$TraceVerbose | Out-Null
    & python -I $view show (Join-Path $o 'trace-client.log') > (Join-Path $o 'view.txt')
}
$a = Join-Path $Out "$Name-orig\trace-client.log"
$b = Join-Path $Out "$Name-om\trace-client.log"
& python -I $view diff $a $b -n 1 --words > (Join-Path $Out "$Name.diff") 2>&1
Get-Content (Join-Path $Out "$Name.diff") | Select-Object -Last 1
