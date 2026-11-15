<#
Scripted drive of the FZX_SIM=orig image on the Yaba MCP lane: boot it, then hold inputs through
the image's fzx_debug_n64 hook and sample racer 0's pose (orig_pose) with a screenshot per step.

  tools/emu/orig_drive.ps1 [-Iso saturn/build-orig/fzero-x-saturn.iso] [-Sym ...yaul/fzero-x-saturn.sym]
                           [-Steps 'A:4','A+R:2',...] [-OutDir DIR]

A step whose buttons include RS first restarts the race (as pressing Start). A step whose buttons
include OV also runs the image's overload control (as holding X+Z): the
miss counter must climb. Each line also prints the frame timing (fzx_timing, FRT counts as a
percentage of the field): last loop, worst loop, worst ticks, missed fields.

Each step is BUTTONS[,SX[,SY]]:SECONDS with N64 buttons A B Z R S CD (0 for none). Prints one line
per step: position (game units), speed (km/h), lap, energy. Then stops the emulator and restores
the shared ini. Tier: EMULATOR (input forced through a hook, not the pad).
#>
[CmdletBinding()]
param(
    [string]$Iso = (Join-Path $PSScriptRoot '..\..\saturn\build-orig\fzero-x-saturn.iso'),
    [string]$Sym = (Join-Path $PSScriptRoot '..\..\saturn\build-orig\yaul\fzero-x-saturn.sym'),
    [string[]]$Steps = @('0:9', 'A:4', 'A,40:3', 'A,-40:3', 'A+B:3', 'CD:2'),
    [string]$OutDir = $(Join-Path $HOME 'fzx-evidence\emu\drive'),
    [int]$Port = 9641
)
$ErrorActionPreference = 'Stop'
$bits = @{ RS = 0; OV = 0; '0' = 0; A = 0x8000; B = 0x4000; Z = 0x2000; S = 0x1000; R = 0x10; CD = 0x4 }
function Sym([string]$n) {
    $l = Get-Content -LiteralPath $Sym | Where-Object { ($_ -split '\s+')[2] -match ('^' + [regex]::Escape($n) + '(\.lto_priv\.\d+)?$') } | Select-Object -First 1
    if (-not $l) { throw "symbol $n not in $Sym" }
    '0x' + ($l -split '\s+')[0]
}
function Mcp([string]$name, [hashtable]$arguments) {
    $body = @{ jsonrpc = '2.0'; id = 1; method = 'tools/call'; params = @{ name = $name; arguments = $arguments } } |
        ConvertTo-Json -Depth 6 -Compress
    $r = Invoke-WebRequest -UseBasicParsing -TimeoutSec 30 "http://127.0.0.1:$Port/mcp" -Method Post `
        -ContentType 'application/json' -Headers @{ Accept = 'application/json, text/event-stream' } -Body $body
    (($r.Content | ConvertFrom-Json).result.content | ForEach-Object { $_.text }) -join "`n"
}
# memory_read prints "ADDRESS: BB BB ..." lines; the SH-2 is big-endian.
function Floats([string]$text, [int]$count) {
    $hex = (($text -split "`n") | ForEach-Object { if ($_ -match '^[0-9A-Fa-f]{8}:\s+((?:[0-9A-Fa-f]{2} ?)+)') { $Matches[1] } }) -join ''
    $hex = $hex -replace '\s+', ''
    for ($i = 0; $i -lt $count; $i++) {
        $b = [byte[]](3, 2, 1, 0 | ForEach-Object { [Convert]::ToByte($hex.Substring(8 * $i + 2 * $_, 2), 16) })
        [BitConverter]::ToSingle($b, 0)
    }
}

$boot = Join-Path $PSScriptRoot 'yaba_boot.ps1'
$ini = Join-Path $env:APPDATA 'YabaSanshiro\yabause.ini'
New-Item -ItemType Directory -Force $OutDir | Out-Null
& $boot -Iso $Iso -Sym $Sym -OutDir $OutDir -Port $Port -KeepRunning | Write-Output
$hook = Sym '_fzx_debug_n64'
$over = Sym '_fzx_debug_overload'
$timing = Sym '_fzx_timing'
function Longs([string]$text, [int]$count) {
    $hex = (($text -split "`n") | ForEach-Object { if ($_ -match '^[0-9A-Fa-f]{8}:\s+((?:[0-9A-Fa-f]{2} ?)+)') { $Matches[1] } }) -join ''
    $hex = $hex -replace '\s+', ''
    for ($i = 0; $i -lt $count; $i++) { [Convert]::ToUInt32($hex.Substring(8 * $i, 8), 16) }
}
$pose = Sym '_orig_pose'
try {
    $n = 0
    foreach ($step in $Steps) {
        $spec, $secs = $step -split ':'
        $f = $spec -split ','
        $mask = 0
        foreach ($b in $f[0] -split '\+') { $mask = $mask -bor $bits[$b] }
        $sx = if ($f.Count -gt 1) { [int]$f[1] } else { 0 }
        $sy = if ($f.Count -gt 2) { [int]$f[2] } else { 0 }
        # Zero means "use the pad": an idle step still forces a word (an unused low bit of B).
        $word = ([uint32]$mask -shl 16) -bor (([uint32]($sx -band 0xFF)) -shl 8) -bor [uint32]($sy -band 0xFF)
        if ($word -eq 0) { $word = 0x00010000 }
        $ov = if ($f[0] -split '\+' -contains 'OV') { '0x00000001' } else { '0x00000000' }
        Mcp 'memory_write' @{ address = $over; size = 'long'; value = $ov } | Out-Null
        if ($f[0] -split '\+' -contains 'RS') {
            Mcp 'memory_write' @{ address = (Sym '_fzx_debug_restart'); size = 'long'; value = '0x00000001' } | Out-Null
        }
        # The MCP server takes the value as a hex string only.
        $w = Mcp 'memory_write' @{ address = $hook; size = 'long'; value = ('0x{0:X8}' -f $word) }
        if ($w -notmatch '^wrote') { throw "memory_write failed: $w" }
        Start-Sleep -Seconds ([int]$secs)
        $v = Floats (Mcp 'memory_read' @{ address = $pose; length = 60 }) 14
        $t = Longs (Mcp 'memory_read' @{ address = $timing; length = 48 }) 12
        $fl = [Math]::Max(1, $t[2])
        $png = Join-Path $OutDir ('drive-{0:D2}.png' -f $n)
        Mcp 'framebuffer_screenshot' @{ path = $png.Replace('\', '/') } | Out-Null
        Write-Output ('FZX-DRIVE {0,-10} pos {1,8:F0} {2,6:F0} {3,8:F0}  speed {4,4:F0} km/h  energy {5,5:F1}  cpu {6,3}% max {7,3}% sim {8,3}% miss {9} (pose {10}% render {11}% hud {12}%)  {13}' -f
            $step, $v[0], $v[1], $v[2], ($v[12] * 21.6), $v[13], [int](100 * $t[3] / $fl), [int](100 * $t[4] / $fl),
            [int](100 * $t[6] / $fl), $t[7], [int](100 * $t[9] / $fl),
            [int](100 * $t[10] / $fl), [int](100 * $t[11] / $fl), $png)
        $n++
    }
} finally {
    Get-Process yabasanshiro -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq (Resolve-Path -LiteralPath $env:FZX_YABA_EXE).Path } |
        Stop-Process -Force
    Copy-Item -LiteralPath "$ini.fzx-backup" -Destination $ini -Force
}
