# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-410 test harness: runs one console script with the original renderer and with ref_omgl
# (run_sof.ps1, +set fixedtime 30), tags <Tag>gl / <Tag>om.
param([string]$Script, [string]$Tag, [string]$Extra = '', [int]$Timeout = 300)
$h = Join-Path $PSScriptRoot 'run_sof.ps1'
foreach ($r in @('gl', 'omgl')) {
    $t = if ($r -eq 'gl') { "${Tag}gl" } else { "${Tag}om" }
    & $h -Script $Script -Ref $r -Tag $t -Extra "+set fixedtime 30 $Extra" -Timeout $Timeout | Select-Object -Last 2
}
