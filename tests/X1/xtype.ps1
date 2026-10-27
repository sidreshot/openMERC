# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc X1 - keyboard text test: one hidden session (xrun.ps1) while XType.cs posts typed text to the game
# window at the script's "OMTYPE <n>" marks. Output as xrun.ps1 (openmerc-data\x1\<Tag>\: client.log, *.png).
#   xtype.ps1 -Game D:\Games\OpenMerc-int2-test -Tag cyr1
# Implements: black-box test harness (no game code)
param(
    [string]$Game = 'D:\Games\OpenMerc-int2-test',
    [string]$Tag = 'xtype',
    [string]$Map = 'tsr1',
    [string]$Script = (Join-Path $PSScriptRoot 'x1_type.cfg'),
    [int]$Timeout = 120
)
$ErrorActionPreference = 'Stop'
# (Yo is on the console key of the Russian layout, so it is typed as VK_PACKET here)
# step n of x1_type.cfg's "echo OMTYPE n"; text in UTF-8 (this file is UTF-8 with BOM)
$steps = @(
    '{CON}echo ',                             # 0: console key (opens it), ASCII part of the line
    "$([char]0x41F)$([char]0x440)$([char]0x438)$([char]0x432)$([char]0x435)$([char]0x442), $([char]0x43C)$([char]0x438)$([char]0x440)! {ENTER}", # 1: 'Privet, mir!' in Cyrillic, sent
    "say $([char]0x421)$([char]0x43E)$([char]0x43B)$([char]0x434)$([char]0x430)$([char]0x442) $([char]0x443)$([char]0x434)$([char]0x430)$([char]0x447)$([char]0x438) {PACKET:$([char]0x401)}{PACKET:$([char]0x451)} $([char]0x416)$([char]0x44D)$([char]0x44E) {PACKET:$([char]0x42F)}{PACKET:$([char]0x44A)}", # 2: line left open, shot
    '{ENTER}',                                # 3: send it
    '{CON}',                                  # 4: console key (closes it)
    "$([char]0x41F)$([char]0x440)$([char]0x438)$([char]0x432)$([char]0x435)$([char]0x442) $([char]0x447)$([char]0x430)$([char]0x442)", # 5: chat line, shot
    '{ENTER}'                                 # 6: send chat
)
$x1 = $PSScriptRoot
if (-not ('XType' -as [type])) { Add-Type -Path (Join-Path $x1 'XType.cs') }
$log = Join-Path "C:\openmerc-data\x1\$Tag" 'user\sof.log'
$job = Start-Job -ScriptBlock {
    param($x1, $Script, $Map, $Tag, $Timeout, $Game)
    & (Join-Path $x1 'xrun.ps1') -Script $Script -Map $Map -Tag $Tag -Timeout $Timeout -Game $Game -NoSound -Config @('set vid_maxfps 60', 'set cl_maxfps 60')
} -ArgumentList $x1, $Script, $Map, $Tag, $Timeout, $Game
Start-Sleep -Milliseconds 1500  # xrun clears the output directory first
$n = [XType]::Run('OMTestX1', $log, [string[]]$steps, $Timeout * 1000)
Write-Host "xtype: $n step(s) posted"
[XType]::Log | ForEach-Object { Write-Host "xtype: $_" }
Wait-Job $job | Out-Null
Receive-Job $job
Remove-Job $job
