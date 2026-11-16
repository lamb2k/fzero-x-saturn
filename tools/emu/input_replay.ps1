<#
Replay a recorded local input log at simulation ticks, independent of render cadence.
Uses the existing fzx_input_log buffer: upload while paused, verify every byte,
restart, and freeze the source simulation after the final word. Presentation keeps
running for a stable capture. This is a debugger test, not pad or hardware acceptance.
Retail input logs, state, screenshots and disc images must remain outside git.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$Inputs,
    [Parameter(Mandatory)] [string]$Iso,
    [Parameter(Mandatory)] [string]$Sym,
    [Parameter(Mandatory)] [string]$OutDir,
    [int]$Port = 9641,
    [int]$TimeoutSeconds = 240,
    [string]$SourceCameraTrace = '',
    [switch]$SourceRoad,
    [int[]]$HoldTicks = @()
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..')) + [IO.Path]::DirectorySeparatorChar
if (([IO.Path]::GetFullPath($OutDir) + [IO.Path]::DirectorySeparatorChar).StartsWith(
        $repo, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Replay evidence must stay outside the repository'
}
$exe = (Resolve-Path -LiteralPath $env:FZX_YABA_EXE).Path
if (Get-Process yabasanshiro -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe }) {
    throw 'This emulator build is already running'
}
$words = @(Get-Content -LiteralPath $Inputs | Where-Object { $_.Trim() })
if ($words.Count -lt 1 -or $words.Count -gt 12288 -or
    @($words | Where-Object { $_ -notmatch '^[0-9A-Fa-f]{8}$' }).Count) {
    throw 'Input log must contain 1..12288 hex words'
}
$all = ($words -join '').ToUpperInvariant()
$cameraExpected = @{}
if ($SourceCameraTrace) {
    foreach ($line in Get-Content -LiteralPath $SourceCameraTrace) {
        $parts = $line -split '\s+'
        if ($parts.Count -ne 65) { throw 'Invalid source camera trace schema' }
        if ($parts[1] -eq '0') { $cameraExpected[[int]$parts[0]+1] = ($parts[2..64] -join '').ToUpperInvariant() }
    }
}
$targets = @(@($HoldTicks)+@($words.Count) | Sort-Object -Unique)
if (@($targets | Where-Object { $_ -lt 1 -or $_ -gt $words.Count }).Count) { throw 'Hold tick outside replay' }
$cameraSamples = @()
$roadSamples = @()
function Addr([uint32]$a) { '0x{0:X8}' -f $a }
function S([string]$name) {
    $line = Get-Content -LiteralPath $Sym | Where-Object {
        ($_ -split '\s+')[2] -match ('^'+[regex]::Escape($name)+'(\.lto_priv\.\d+)?$')
    } | Select-Object -First 1
    if (-not $line) { throw "Missing replay symbol: $name" }
    [Convert]::ToUInt32(($line -split '\s+')[0],16)
}
function Mcp([string]$name,[hashtable]$arguments) {
    $body = @{jsonrpc='2.0';id=1;method='tools/call';params=@{name=$name;arguments=$arguments}} |
        ConvertTo-Json -Depth 6 -Compress
    $r = Invoke-RestMethod -Uri "http://127.0.0.1:$Port/mcp" -Method Post `
        -ContentType application/json -Body $body -TimeoutSec 30
    if ($r.error -or $r.result.isError) { throw "MCP failed: $name" }
    ($r.result.content | ForEach-Object { $_.text }) -join "`n"
}
function Hex([string]$text) {
    ((($text -split "`n") | ForEach-Object {
        if ($_ -match '^[0-9A-Fa-f]{8}:\s+((?:[0-9A-Fa-f]{2} ?)+)') { $Matches[1] }
    }) -join '') -replace '\s+',''
}
function ReadWord([uint32]$a) {
    $hex = Hex (Mcp memory_read @{address=(Addr $a);length=4})
    if ($hex.Length -ne 8) { throw 'Short replay read' }
    [Convert]::ToUInt32($hex,16)
}
function WriteWord([uint32]$a,[uint32]$v) {
    $r = Mcp memory_write @{address=(Addr $a);size='long';value=(Addr $v)}
    if ($r -notmatch '^wrote') { throw 'Replay hook write failed' }
}
$ini = Join-Path $env:APPDATA 'YabaSanshiro\yabause.ini'
$launched = $false
New-Item -ItemType Directory -Force $OutDir | Out-Null
$iniSnapshot = Join-Path $OutDir 'yabause.ini.before'
Copy-Item -LiteralPath $ini -Destination $iniSnapshot -Force
try {
    $launched = $true
    & (Join-Path $PSScriptRoot 'yaba_boot.ps1') -Iso $Iso -Sym $Sym -Port $Port `
        -Until '_fzx_timing:0' -UntilValue 0x465A5854 -TimeoutSeconds 45 `
        -KeepRunning -Visible -OutDir $OutDir | Out-Null
    Mcp pause @{} | Out-Null
    $log = S '_fzx_input_log'
    for ($offset=0; $offset -lt $all.Length; $offset+=2048) {
        $chunk = $all.Substring($offset,[Math]::Min(2048,$all.Length-$offset))
        Mcp memory_write @{address=(Addr ($log+$offset/2));bytes=$chunk} | Out-Null
        $read = Hex (Mcp memory_read @{address=(Addr ($log+$offset/2));length=($chunk.Length/2)})
        if ($read.ToUpperInvariant() -ne $chunk) { throw 'Uploaded replay differs from input' }
    }
    WriteWord (S '_fzx_debug_autopilot') 0
    WriteWord (S '_fzx_debug_n64') 0
    # Explicit test phase; ordinary player retries preserve the animation clock.
    if ($SourceRoad) { WriteWord (S '_fzx_road_next_frame') 0 }
    WriteWord (S '_fzx_debug_replay_count') $targets[0]
    WriteWord (S '_fzx_debug_restart') 1
    Mcp resume @{} | Out-Null
    $count = S '_fzx_input_count'
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    foreach ($target in $targets) {
        WriteWord (S '_fzx_debug_replay_count') $target
        do {
            Start-Sleep -Seconds 1
            $done = ReadWord $count
            if ($SourceCameraTrace -and (ReadWord (S '_fzx_camera_status')) -ge 2) {
                throw "Camera renderer fault while advancing to tick $target"
            }
            if ($SourceRoad -and (ReadWord (S '_fzx_road_plan_status')) -ge 2) {
                throw "Road planner fault while advancing to tick $target"
            }
        } while ($done -ne $target -and (Get-Date) -lt $deadline)
        if ($done -ne $target) { throw "Replay did not reach exact tick $target (at $done)" }
        # Simulation is held at N; display remains active for settled capture.
        Start-Sleep -Seconds 1
        if ($SourceRoad) {
            $sequence = ReadWord (S '_fzx_road_plan_sequence')
            $roadHex = Hex (Mcp memory_read @{address=(Addr (S '_fzx_source_road_plan'));length=796})
            $roadFrame = ReadWord (S '_fzx_road_frame')
            $roadStatus = ReadWord (S '_fzx_road_plan_status')
            if ($sequence -eq 0 -or $sequence -ne (ReadWord (S '_fzx_road_plan_sequence')) -or
                $roadHex.Length -ne 1592 -or $roadFrame -ne $target-1 -or $roadStatus -ne 1) {
                throw "Unstable or invalid road plan at tick $target"
            }
            $roadSamples += @{tick=$target;frame=$roadFrame;status=$roadStatus;sequence=$sequence;plan_hex=$roadHex}
            $roadSamples | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutDir 'road-samples.json')
            Write-Output "FZX-ROAD tick=$target stable-packet=796 frame=$roadFrame"
        }
        if ($SourceCameraTrace) {
            $cameraHex = Hex (Mcp memory_read @{address=(Addr (S '_fzx_source_camera'));length=252})
            $cameraStatus = ReadWord (S '_fzx_camera_status')
            $overflows = ReadWord (S '_fzx_camera_overflows')
            if ($cameraStatus -ne 1 -or $overflows -ne 0) { throw "Camera render fault: status=$cameraStatus overflow=$overflows" }
            if (-not $cameraExpected.ContainsKey([int]$target) -or $cameraHex.ToUpperInvariant() -ne $cameraExpected[[int]$target]) {
                throw "Source camera differs from host at tick $target"
            }
            $viewHex = Hex (Mcp memory_read @{address=(Addr (S '_fzx_native_view'));length=80})
            $cameraSamples += @{tick=$target;camera_hex=$cameraHex;view_hex=$viewHex;status=$cameraStatus;overflows=$overflows}
            $cameraSamples | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutDir 'camera-samples.json')
            Mcp framebuffer_screenshot @{path=(Join-Path $OutDir "camera-$target.png").Replace('\','/')} | Out-Null
            Write-Output "FZX-VIEW tick=$target source-camera=match overflow=0"
        }
    }
    $pose = Hex (Mcp memory_read @{address=(Addr (S '_orig_pose'));length=76})
    $stateFlags = [Convert]::ToUInt32($pose.Substring(144,8),16)
    $raceTime = [Convert]::ToUInt32($pose.Substring(136,8),16)
    $lap = [Convert]::ToUInt32($pose.Substring(120,8),16)
    Mcp framebuffer_screenshot @{path=(Join-Path $OutDir 'held.png').Replace('\','/')} | Out-Null
    $timingHex = Hex (Mcp memory_read @{address=(Addr (S '_fzx_timing'));length=48})
    $receipt = [ordered]@{tier='EMULATOR';inputs_sha256=(Get-FileHash $Inputs -Algorithm SHA256).Hash.ToLower();
        iso_sha256=(Get-FileHash $Iso -Algorithm SHA256).Hash.ToLower();
        words=$words.Count;ticks=$done;lap=$lap;race_time=$raceTime;flags=('{0:X8}' -f $stateFlags);
        finished=([bool]($stateFlags -band 0x02000000));pose_hex=$pose;timing_hex=$timingHex}
    $receipt | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutDir 'replay.json')
    if (-not $receipt.finished) { throw 'Replay reached its end without finishing the race' }
    Write-Output "FZX-REPLAY PASS words=$done lap=$lap finish_ms=$raceTime"
} finally {
    if ($launched) {
        Get-Process yabasanshiro -ErrorAction SilentlyContinue | Where-Object {
            $_.Path -eq $exe
        } | Stop-Process
        Copy-Item -LiteralPath $iniSnapshot -Destination $ini -Force
    }
}
