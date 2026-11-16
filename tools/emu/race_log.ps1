<#
Whole-race run of the FZX_SIM=orig image on the Yaba MCP lane: boot, hand the pad to the image's
autopilot, sample racer 0's pose once a second, then read back every tick's input
(fzx_input_log) for tools/race_replay.py to replay through the host reference.

  tools/emu/race_log.ps1 [-Seconds 150] [-Autopilot 0x2803] [-OutDir DIR]

-Autopilot: look-ahead in rings in the low byte, coast threshold in the next (0x2803 finishes
Mute City 1). Writes OutDir/samples.txt (ticks x y z energy lap= time= flags=) and
OutDir/inputs.hex (one N64 input word per tick). Then stops the emulator and restores the shared
ini. Tier: EMULATOR. A --rom image's outputs stay local, like the image.
#>
[CmdletBinding()]
param(
    [string]$Iso = (Join-Path $PSScriptRoot '..\..\saturn\build-orig\fzero-x-saturn.iso'),
    [string]$Sym = (Join-Path $PSScriptRoot '..\..\saturn\build-orig\yaul\fzero-x-saturn.sym'),
    [int]$Seconds = 150,
    [int]$Autopilot = 0x2803,
    [int]$BootSeconds = 22,
    [string]$OutDir = $(Join-Path $HOME 'fzx-evidence\emu\race'),
    [int]$Port = 9641
)
$ErrorActionPreference = 'Stop'
function S([string]$n) {
    $l = Get-Content -LiteralPath $Sym | Where-Object { ($_ -split '\s+')[2] -match ('^' + [regex]::Escape($n) + '(\.lto_priv\.\d+)?$') } | Select-Object -First 1
    if (-not $l) { throw "symbol $n not in $Sym" }
    [Convert]::ToUInt32(($l -split '\s+')[0], 16)
}
function Mcp([string]$name, [hashtable]$arguments) {
    $body = @{ jsonrpc = '2.0'; id = 1; method = 'tools/call'; params = @{ name = $name; arguments = $arguments } } |
        ConvertTo-Json -Depth 6 -Compress
    $r = Invoke-WebRequest -UseBasicParsing -TimeoutSec 60 "http://127.0.0.1:$Port/mcp" -Method Post `
        -ContentType 'application/json' -Headers @{ Accept = 'application/json, text/event-stream' } -Body $body
    (($r.Content | ConvertFrom-Json).result.content | ForEach-Object { $_.text }) -join "`n"
}
# memory_read prints "ADDRESS: BB BB ..." lines; the SH-2 is big-endian.
function Hex([string]$text) {
    ((($text -split "`n") | ForEach-Object { if ($_ -match '^[0-9A-Fa-f]{8}:\s+((?:[0-9A-Fa-f]{2} ?)+)') { $Matches[1] } }) -join '') -replace '\s+', ''
}
function Floats([string]$hex, [int]$count) {
    for ($i = 0; $i -lt $count; $i++) {
        $b = [byte[]](3, 2, 1, 0 | ForEach-Object { [Convert]::ToByte($hex.Substring(8 * $i + 2 * $_, 2), 16) })
        [BitConverter]::ToSingle($b, 0)
    }
}
function Addr([uint32]$a) { '0x{0:X8}' -f $a }

$ini = Join-Path $env:APPDATA 'YabaSanshiro\yabause.ini'
New-Item -ItemType Directory -Force $OutDir | Out-Null
& (Join-Path $PSScriptRoot 'yaba_boot.ps1') -Iso $Iso -Sym $Sym -OutDir $OutDir -Port $Port -KeepRunning | Out-Null
try {
    Start-Sleep -Seconds $BootSeconds
    $cnt = S '_fzx_input_count'
    $pose = S '_orig_pose'
    $ap = S '_fzx_debug_autopilot'
    Mcp 'memory_write' @{ address = (Addr $ap); size = 'long'; value = ('0x{0:X8}' -f $Autopilot) } | Out-Null
    $samples = @()
    for ($k = 0; $k -lt $Seconds; $k++) {
        Start-Sleep -Seconds 1
        $n = [Convert]::ToUInt32((Hex (Mcp 'memory_read' @{ address = (Addr $cnt); length = 4 })), 16)
        # RefPose: pos[3], basis[3][3], speed, energy, intro, lap, laps_completed, race_time, state_flags.
        $ph = Hex (Mcp 'memory_read' @{ address = (Addr $pose); length = 76 })
        $v = Floats $ph 14
        $lap = [Convert]::ToInt32($ph.Substring(120, 8), 16)
        $time = [Convert]::ToInt32($ph.Substring(136, 8), 16)
        $samples += ('{0} {1:F3} {2:F3} {3:F3} {4:F1} lap={5} time={6} flags={7}' -f $n, $v[0], $v[1], $v[2], $v[13],
            $lap, $time, $ph.Substring(144, 8))
    }
    Mcp 'memory_write' @{ address = (Addr $ap); size = 'long'; value = '0x00000000' } | Out-Null
    Mcp 'memory_write' @{ address = (Addr (S '_fzx_debug_n64')); size = 'long'; value = '0x00010000' } | Out-Null
    $n = [Convert]::ToUInt32((Hex (Mcp 'memory_read' @{ address = (Addr $cnt); length = 4 })), 16)
    $log = S '_fzx_input_log'
    $hex = ''
    for ($o = 0; $o -lt 4 * $n; $o += 1024) {
        $hex += Hex (Mcp 'memory_read' @{ address = (Addr ($log + $o)); length = [Math]::Min(1024, 4 * $n - $o) })
    }
    $samples | Set-Content -Encoding ascii (Join-Path $OutDir 'samples.txt')
    $(for ($i = 0; $i -lt $n; $i++) { $hex.Substring(8 * $i, 8) }) | Set-Content -Encoding ascii (Join-Path $OutDir 'inputs.hex')
    # fzx_timing: magic, loops, field, cpu, cpu_max, sim, sim_max, misses, overload (FRT counts).
    $t = Hex (Mcp 'memory_read' @{ address = (Addr (S '_fzx_timing')); length = 36 })
    $w = for ($i = 0; $i -lt 9; $i++) { [Convert]::ToUInt32($t.Substring(8 * $i, 8), 16) }
    $fl = [Math]::Max(1, $w[2])
    $timing = 'frames {0}  worst frame {1}% of a field  worst ticks {2}%  missed fields {3}' -f $w[1],
        [int](100 * $w[4] / $fl), [int](100 * $w[6] / $fl), $w[7]
    $timing | Set-Content -Encoding ascii (Join-Path $OutDir 'timing.txt')
    Write-Output "FZX-RACE ticks logged $n; last sample: $($samples[-1])"
    Write-Output "FZX-RACE timing: $timing"
} finally {
    Get-Process yabasanshiro -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq (Resolve-Path -LiteralPath $env:FZX_YABA_EXE).Path } |
        Stop-Process -Force
    Copy-Item -LiteralPath "$ini.fzx-backup" -Destination $ini -Force
}
