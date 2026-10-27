# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-412 test harness. Builds the 32-bit capture shim ref_omcap.dll (refcap.c) into -Out.
# vcvarsall prints a vswhere warning on this machine; the environment it sets is still sufficient, so the
# commands are chained with & rather than &&.
param(
    [string]$Out = 'C:\openmerc\build-I4a-x86\refcap',
    [string]$VcVars = 'C:\BuildTools\VC\Auxiliary\Build\vcvarsall.bat'
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $Out | Out-Null
$dll = Join-Path $Out 'ref_omcap.dll'
if (Test-Path $dll) { Remove-Item $dll -Force }
$cmd = "call `"$VcVars`" x86 >nul 2>nul & " +
       "cl /nologo /O1 /MT /LD `"$PSScriptRoot\refcap.c`" /Fo`"$Out\refcap.obj`" /Fe`"$dll`" /link user32.lib"
cmd.exe /c $cmd
if (-not (Test-Path $dll)) { throw 'build failed' }
Write-Host "Built $dll"
