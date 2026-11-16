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
    [switch]$SourceRoadTiles,
    [switch]$SourceSky,
    [switch]$SourceSkyline,
    [switch]$SourcePlanes,
    [switch]$TextureCd,
    [ValidateRange(4,524288)] [int]$TextureCdBytes = 8320,
    [int[]]$HoldTicks = @(),
    # Bounded rendering probes cannot establish race completion.
    [switch]$DiagnosticOnly
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
$skySamples = @()
$skylineSamples = @()
$planeSamples = @()
$roadTileSamples = @()
if ($SourceRoadTiles -and (-not $SourceRoad -or -not $SourceSky)) {throw "Native road capture requires road and sky gates"}
if ($SourceSkyline -and -not $SourceSky) {throw 'Skyline capture requires the source sky frame gate'}
if ($SourceSky -and -not $SourceCameraTrace) { throw 'Sky captures require the matching source camera trace' }
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
        -Until '_fzx_timing:0' -UntilValue 0x465A5854 -TimeoutSeconds $(if ($SourceRoadTiles) {180} elseif ($SourcePlanes) {120} else {45}) `
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
    if ($SourceRoadTiles) { WriteWord (S '_fzx_debug_frame_hold') 1 }
    WriteWord (S '_fzx_debug_restart') 1
    Mcp resume @{} | Out-Null
    $count = S '_fzx_input_count'
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    foreach ($target in $targets) {
        if ($SourceRoadTiles -and $target -ne $targets[0]) {
            do { Start-Sleep -Milliseconds 50; $released=ReadWord (S '_fzx_debug_frame_ready') }
            while ($released -ne 0 -and (Get-Date) -lt $deadline)
            if ($released -ne 0) {throw 'Previous display capture did not release'}
        }
        WriteWord (S '_fzx_debug_replay_count') $target
        if ($SourceRoadTiles) { WriteWord (S '_fzx_debug_frame_hold') 1 }
        do {
            Start-Sleep -Seconds 1
            $done = ReadWord $count
            if ($SourceCameraTrace -and (ReadWord (S '_fzx_camera_status')) -ge 2) {
                throw "Camera renderer fault while advancing to tick $target"
            }
            if ($SourceRoad -and (ReadWord (S '_fzx_road_plan_status')) -ge 2) {
                throw "Road planner fault while advancing to tick $target"
            }
            if ($SourceSky -and (ReadWord (S '_fzx_sky_status')) -ge 2) {
                throw "Sky renderer fault while advancing to tick $target"
            }
            if ($SourceRoadTiles -and (ReadWord (S '_fzx_road_tile_status')) -ge 2) {
                Mcp pause @{} | Out-Null
                @{tick=(ReadWord $count);target=$target;status=(ReadWord (S '_fzx_road_tile_status'));
                    stats_hex=(Hex (Mcp memory_read @{address=(Addr (S '_fzx_road_tile_stats'));length=32}));
                    view_hex=(Hex (Mcp memory_read @{address=(Addr (S '_fzx_native_view'));length=80}));
                    camera_hex=(Hex (Mcp memory_read @{address=(Addr (S '_fzx_source_camera'));length=252}));
                    plan_hex=(Hex (Mcp memory_read @{address=(Addr (S '_fzx_source_road_plan'));length=796}))} |
                    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutDir 'road-tile-fault.json')
                Mcp framebuffer_screenshot @{path=(Join-Path $OutDir 'road-tile-fault.png').Replace('\','/')} | Out-Null
                throw "Native road tile renderer fault at tick $done"
            }
            if ($SourcePlanes -and (ReadWord (S '_fzx_plane_status')) -ge 2) {
                Mcp pause @{} | Out-Null
                $planeFault=@{tick=(ReadWord $count);target=$target;
                    status=(ReadWord (S '_fzx_plane_status'));
                    stats_hex=(Hex (Mcp memory_read @{address=(Addr (S '_fzx_plane_stats'));length=28}));
                    view_hex=(Hex (Mcp memory_read @{address=(Addr (S '_fzx_native_view'));length=80}));
                    backdrop_hex=(Hex (Mcp memory_read @{address=(Addr (S '_fzx_source_backdrop'));length=424}))}
                $planeFault | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutDir 'plane-fault.json')
                Mcp framebuffer_screenshot @{path=(Join-Path $OutDir 'plane-fault.png').Replace('\','/')} | Out-Null
                throw "Floor/cloud renderer fault at tick $($planeFault.tick)"
            }
            if ($SourceSkyline -and (ReadWord (S '_fzx_skyline_status')) -ge 2) {
                throw "Skyline fault while advancing to tick $target"
            }
        } while ($done -ne $target -and (Get-Date) -lt $deadline)
        if ($done -ne $target) { throw "Replay did not reach exact tick $target (at $done)" }
        if ($SourceRoadTiles) {
            do { Start-Sleep -Milliseconds 100; $ready=ReadWord (S '_fzx_debug_frame_ready') }
            while ($ready -ne $target -and (Get-Date) -lt $deadline)
            if ($ready -ne $target) {throw "Completed display frame not reached at tick $target"}
        }
        # Simulation is held at N; display remains active for settled capture.
        Start-Sleep -Seconds 1
        if ($SourceSky) {
            # Backdrop publication clears its output before rebuilding it, and
            # renderer statistics are filled during list generation. Stop only
            # outside both writers; two live reads alone can capture torn data.
            $snapshotReady=$false
            for ($attempt=0;$attempt -lt 100;++$attempt) {
                Mcp pause @{} | Out-Null
                if ((ReadWord (S '_fzx_sky_busy')) -eq 0 -and
                    (-not $SourceSkyline -or (ReadWord (S '_fzx_skyline_busy')) -eq 0) -and
                    (-not $SourcePlanes -or (ReadWord (S '_fzx_plane_busy')) -eq 0) -and
                    (-not $SourceRoadTiles -or (ReadWord (S '_fzx_road_tile_busy')) -eq 0)) {$snapshotReady=$true;break}
                if ((ReadWord (S '_fzx_sky_status')) -ge 2) {throw 'Sky fault while capturing'}
                Mcp resume @{} | Out-Null
                Start-Sleep -Milliseconds 37
            }
            if (-not $snapshotReady) {throw 'Could not capture completed sky publication and draw'}
        }
        if ($SourceRoad) {
            $sequence = ReadWord (S '_fzx_road_plan_sequence')
            $roadHex = Hex (Mcp memory_read @{address=(Addr (S '_fzx_source_road_plan'));length=796})
            $roadFrame = ReadWord (S '_fzx_road_frame')
            $roadStatus = ReadWord (S '_fzx_road_plan_status')
            if ($sequence -eq 0 -or $sequence -ne (ReadWord (S '_fzx_road_plan_sequence')) -or
                $roadHex.Length -ne 1592 -or $roadFrame -ne $target-1 -or $roadStatus -ne 1) {
                throw "Invalid road plan tick=$target sequence=$sequence frame=$roadFrame status=$roadStatus hex_length=$($roadHex.Length)"
            }
            $roadSamples += @{tick=$target;frame=$roadFrame;status=$roadStatus;sequence=$sequence;plan_hex=$roadHex}
            $roadSamples | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutDir 'road-samples.json')
            Write-Output "FZX-ROAD tick=$target stable-packet=796 frame=$roadFrame"
        }
        if ($SourceRoadTiles) {
            $tileStats=Hex (Mcp memory_read @{address=(Addr (S '_fzx_road_tile_stats'));length=32})
            if ((ReadWord (S '_fzx_road_tile_status')) -ne 1 -or $tileStats.Length -ne 64 -or $tileStats.Substring(48,8) -ne '00000000') {throw 'Native road tile draw failed'}
            $roadTileSamples+=@{tick=$target;stats_hex=$tileStats;draw_ticks=(ReadWord (S '_fzx_road_tile_ticks'));
                draw_fields=(ReadWord (S '_fzx_road_tile_fields'));field_ticks=(ReadWord ((S '_fzx_timing')+8))}
            $roadTileSamples | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutDir 'road-tile-samples.json')
            Write-Output "FZX-ROAD-TILES tick=$target draw=valid"
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
        if ($SourceSky) {
            $skyHex = Hex (Mcp memory_read @{address=(Addr (S '_fzx_source_backdrop'));length=424})
            $skyStats = Hex (Mcp memory_read @{address=(Addr (S '_fzx_sky_stats'));length=16})
            $skyStatus = ReadWord (S '_fzx_sky_status')
            if ($skyStatus -ne 1 -or $skyHex.Length -ne 848 -or $skyStats.Length -ne 32 -or
                $skyStats.Substring(24,8) -ne '00000000' -or
                $skyHex -ne (Hex (Mcp memory_read @{address=(Addr (S '_fzx_source_backdrop'));length=424}))) {
                throw "Unstable or failed sky at tick $target"
            }
            $guardStart=ReadWord (S '_fzx_sky_stack_start')
            $guardEnd=ReadWord (S '_fzx_sky_stack_end')
            $heapBefore=ReadWord (S '_fzx_sky_heap_before')
            $heapAfter=ReadWord (S '_fzx_sky_heap_after')
            if ($heapBefore -ne $heapAfter) {throw 'Stack paint changed allocator metadata'}
            if ($guardStart -lt 0x06004000 -or $guardEnd -gt 0x06100000 -or $guardEnd-$guardStart -lt 256) {
                throw 'Invalid stack guard bounds'
            }
            $guardHex=Hex (Mcp memory_read @{address=(Addr $guardStart);length=256})
            if ($guardHex -ne ('A55AA55A'*64)) {throw "Stack guard changed at tick $target"}
            $skySamples += @{tick=$target;backdrop_hex=$skyHex;stats_hex=$skyStats;
                sky_ticks=(ReadWord (S '_fzx_sky_ticks'));field_ticks=(ReadWord ((S '_fzx_timing')+8));
                max_commands=(ReadWord (S '_fzx_sky_count_max'));status=$skyStatus;
                stack_guard_start=$guardStart;stack_guard_end=$guardEnd;first_guard_bytes=256;
                heap_prefix_bytes=(ReadWord (S '_fzx_sky_heap_prefix'));heap_before=$heapBefore;heap_after=$heapAfter}
            $skySamples | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutDir 'sky-samples.json')
            Write-Output "FZX-SKY tick=$target stable-packet=424 status=1"
            if ($SourceSkyline) {
                $skylineHex=Hex (Mcp memory_read @{address=(Addr (S '_fzx_source_skyline'));length=580})
                $skylineStats=Hex (Mcp memory_read @{address=(Addr (S '_fzx_skyline_stats'));length=12})
                if ((ReadWord (S '_fzx_skyline_status')) -ne 1 -or $skylineHex.Length -ne 1160 -or
                    $skylineHex.Substring(0,16) -ne '0000000000000006' -or $skylineStats.Length -ne 24 -or
                    $skylineStats.Substring(16,8) -ne '00000000') {throw 'Invalid skyline packet or draw status'}
                $skylineSamples+=@{tick=$target;packet_hex=$skylineHex;stats_hex=$skylineStats;
                    skyline_ticks=(ReadWord (S '_fzx_skyline_ticks'));field_ticks=(ReadWord ((S '_fzx_timing')+8))}
                $skylineSamples | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutDir 'skyline-samples.json')
                Write-Output "FZX-SKYLINE tick=$target state=6-placements draw=valid"
            }
            if ($SourcePlanes) {
                $planeHex=Hex (Mcp memory_read @{address=(Addr (S '_fzx_plane_stats'));length=28})
                if ((ReadWord (S '_fzx_plane_status')) -ne 1 -or $planeHex.Length -ne 56 -or
                    $planeHex.Substring(40,8) -ne '00000000') {throw 'Invalid floor/cloud draw status'}
                $planeSamples+=@{tick=$target;stats_hex=$planeHex;
                    draw_ticks=(ReadWord (S '_fzx_plane_ticks'));draw_fields=(ReadWord (S '_fzx_plane_fields'));
                    field_ticks=(ReadWord ((S '_fzx_timing')+8))}
                $planeSamples | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutDir 'plane-samples.json')
                Write-Output "FZX-PLANES tick=$target draw=valid"
            }
            if ($SourceRoadTiles -and $target -ne $words.Count) { WriteWord (S '_fzx_debug_frame_hold') 0 }
            Mcp resume @{} | Out-Null
        }
    }
    $pose = Hex (Mcp memory_read @{address=(Addr (S '_orig_pose'));length=76})
    $stateFlags = [Convert]::ToUInt32($pose.Substring(144,8),16)
    $raceTime = [Convert]::ToUInt32($pose.Substring(136,8),16)
    $lap = [Convert]::ToUInt32($pose.Substring(120,8),16)
    Mcp framebuffer_screenshot @{path=(Join-Path $OutDir 'held.png').Replace('\','/')} | Out-Null
    $timingHex = Hex (Mcp memory_read @{address=(Addr (S '_fzx_timing'));length=48})
    $receipt = [ordered]@{tier='EMULATOR';scope=$(if ($DiagnosticOnly) {'diagnostic'} else {'race'});inputs_sha256=(Get-FileHash $Inputs -Algorithm SHA256).Hash.ToLower();
        iso_sha256=(Get-FileHash $Iso -Algorithm SHA256).Hash.ToLower();
        words=$words.Count;ticks=$done;lap=$lap;race_time=$raceTime;flags=('{0:X8}' -f $stateFlags);
        finished=([bool]($stateFlags -band 0x02000000));pose_hex=$pose;timing_hex=$timingHex}
    $receipt | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutDir 'replay.json')
    if (-not $DiagnosticOnly -and -not $receipt.finished) { throw 'Replay reached its end without finishing the race' }
    if ($SourceSky) {
        Mcp pause @{} | Out-Null
        $start=ReadWord (S '_fzx_sky_stack_start');$end=ReadWord (S '_fzx_sky_stack_end')
        if ($start -lt 0x06004000 -or $end -gt 0x06100000 -or $end -le $start -or ($start -band 3) -or ($end -band 3)) {
            throw 'Invalid stack paint bounds'
        }
        $firstChanged=$end
        for ($at=$start;$at -lt $end;$at+=1024) {
            $size=[Math]::Min(1024,$end-$at)
            $data=Hex (Mcp memory_read @{address=(Addr $at);length=$size})
            if ($data.Length -ne $size*2) { throw 'Short stack read' }
            for ($i=0;$i -lt $data.Length;$i+=8) {
                if ($data.Substring($i,8) -ne 'A55AA55A' -and $at+$i/2 -lt $firstChanged) {$firstChanged=$at+$i/2}
            }
        }
        @{tier='EMULATOR';paint_start=$start;paint_end=$end;first_changed=$firstChanged;
            untouched_guard_bytes=$firstChanged-$start;hardware='UNRUN'} |
            ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutDir 'sky-stack.json')
        if ($firstChanged -le $start) {throw 'Sky candidate exhausted its painted stack guard'}
    }
    if ($TextureCd) {
        if (-not $SourceSky) {Mcp pause @{} | Out-Null}
        $address=S '_fzx_texture_cd_status'
        $values=@(0..6 | ForEach-Object {ReadWord ($address+$_*4)})
        $start=$values[4];$end=$values[5]
        if ($values[0] -ne 1 -or $values[1] -ne $TextureCdBytes -or $values[2] -ne [Math]::Ceiling($TextureCdBytes/2048.0) -or $values[6] -ne 0 -or
            $start -lt (S '_sat_stub_data_end') -or $end -gt 0x00300000 -or $end -le $start -or ($start -band 31)) {
            throw 'Invalid native texture CD load status or scratch bounds'
        }
        $before=Hex (Mcp memory_read @{address=(Addr $start);length=32})
        $after=Hex (Mcp memory_read @{address=(Addr ($end-32));length=32})
        if ($before -ne ('39A5C27E'*8) -or $after -ne ('39A5C27E'*8)) {throw 'Texture CD scratch guard changed'}
        @{tier='EMULATOR';status=$values[0];bytes=$values[1];sectors=$values[2];hash=('{0:X8}' -f $values[3]);
            scratch_start=$start;scratch_end=$end;guards_intact=$true} |
            ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutDir 'texture-cd.json')
    }
    if ($SourceRoadTiles) {
        $start=ReadWord (S '_fzx_road_tile_low_start');$end=ReadWord (S '_fzx_road_tile_low_end')
        if ($start -lt 0x00200000 -or $end+32 -gt 0x00300000 -or $end-$start -lt 49152) {throw 'Native road low-RAM bounds failed'}
        foreach ($address in @(($start-32),($start+49152),$end)) {
            if ((Hex (Mcp memory_read @{address=(Addr $address);length=32})) -ne ('51A7C36D'*8)) {throw 'Native road low-RAM guard changed'}
        }
        @{tier='EMULATOR';low_start=$start;low_end=$end;guards_intact=$true} | ConvertTo-Json |
            Set-Content -LiteralPath (Join-Path $OutDir 'road-tile-memory.json')
    }
    Write-Output "FZX-REPLAY $(if ($DiagnosticOnly) {'DIAGNOSTIC'} else {'PASS'}) words=$done lap=$lap finish_ms=$raceTime"
} finally {
    if ($launched) {
        Get-Process yabasanshiro -ErrorAction SilentlyContinue | Where-Object {
            $_.Path -eq $exe
        } | Stop-Process
        Copy-Item -LiteralPath $iniSnapshot -Destination $ini -Force
    }
}
