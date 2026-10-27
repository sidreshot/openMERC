# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc MENU2 - several run_menu2.ps1 runs at once (own tag, desktop and ports each):
#   par.ps1 "a1080:1920x1080:tests\MENU2\anim.cfg" "a1440:2560x1440:tests\MENU2\anim.cfg"
param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Runs)
$sh = 'D:\Games\OpenMerc-menu-test\shots'
New-Item -ItemType Directory -Force $sh | Out-Null
$port = 29630
$procs = foreach ($r in $Runs) {
    $tag, $size, $cfg = $r -split ':', 3
    $w, $h = $size -split 'x'
    $p = Start-Process powershell -PassThru -WindowStyle Hidden -RedirectStandardOutput "$sh\$tag.out" -ArgumentList @(
        '-NoProfile', '-File', "$PSScriptRoot\run_menu2.ps1", '-Tag', $tag, '-Width', $w, '-Height', $h, '-Script', $cfg, '-Port', $port)
    $port += 10
    $p
}
$procs | Wait-Process -Timeout 600 -ErrorAction SilentlyContinue
foreach ($r in $Runs) { $tag = ($r -split ':')[0]; Write-Host "== $tag"; Get-Content "$sh\$tag.out" | Select-Object -Last 2 }
