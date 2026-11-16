<# Fixed-pose native palette probe: compare host hashes and collect paletteed FRT costs.
   Expected JSON comes from the local host.c vertex/lighting output. All retail
   pixels, screenshots, images and detailed rows remain outside the repository. #>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Iso,
    [Parameter(Mandatory)][string]$Sym,
    [Parameter(Mandatory)][string]$Expected,
    [Parameter(Mandatory)][string]$OutDir,
    [int]$Samples=8,
    [int]$Port=9641,
    [int[]]$Cases=(0..7)
)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))+[IO.Path]::DirectorySeparatorChar
if(([IO.Path]::GetFullPath($OutDir)+[IO.Path]::DirectorySeparatorChar).StartsWith($repo,[StringComparison]::OrdinalIgnoreCase)){
    throw 'Palette evidence must stay outside the repository'
}
if($Samples -lt 1 -or $Samples -gt 120){throw 'Samples must be 1..120'}
$exe=(Resolve-Path -LiteralPath $env:FZX_YABA_EXE).Path
if(Get-Process yabasanshiro -ErrorAction SilentlyContinue|Where-Object Path -eq $exe){throw 'This emulator is already running'}
$expectedRows=@(Get-Content -LiteralPath $Expected -Raw|ConvertFrom-Json)
if($expectedRows.Count -ne 72 -or @($expectedRows|Where-Object {$_.vertices -notmatch '^[a-fA-F0-9]{8}$' -or $_.lights -notmatch '^[a-fA-F0-9]{8}$'}).Count){throw '72 host vertex and lighting hashes required'}
$symbols=@{}
foreach($line in Get-Content -LiteralPath $Sym){
    $parts=$line -split '\s+'
    if($parts.Count -ge 3 -and $parts[2].StartsWith('_fzx_palette_')){$symbols[$parts[2]]='0x'+$parts[0]}
}
function Mcp($name,$arguments){
    $body=@{jsonrpc='2.0';id=1;method='tools/call';params=@{name=$name;arguments=$arguments}}|ConvertTo-Json -Depth 5 -Compress
    $r=Invoke-RestMethod "http://127.0.0.1:$Port/mcp" -Method Post -ContentType application/json -Body $body -TimeoutSec 30
    if($r.error -or $r.result.isError){throw "MCP failed: $name"}
    ($r.result.content|ForEach-Object text)-join "`n"
}
function Read($short){
    $name='_fzx_palette_'+$short
    if(-not $symbols.ContainsKey($name)){throw "Missing probe symbol: $name"}
    $text=Mcp memory_read @{address=$symbols[$name];length=4}
    if($text -notmatch ':\s+([A-Fa-f0-9]{2}) ([A-Fa-f0-9]{2}) ([A-Fa-f0-9]{2}) ([A-Fa-f0-9]{2})'){throw 'Short palette read'}
    [Convert]::ToUInt32($Matches[1]+$Matches[2]+$Matches[3]+$Matches[4],16)
}
function WriteWord($short,[uint32]$value){
    $name='_fzx_palette_'+$short
    if(-not $symbols.ContainsKey($name)){throw "Missing probe symbol: $name"}
    $result=Mcp memory_write @{address=$symbols[$name];size='long';value=('0x{0:x}' -f $value)}
    if($result -notmatch '^wrote'){throw 'Palette probe hook write failed'}
}
New-Item -ItemType Directory -Force $OutDir|Out-Null
$ini=Join-Path $env:APPDATA 'YabaSanshiro\yabause.ini'
$iniBefore=Join-Path $OutDir 'yabause.ini.before'
Copy-Item -LiteralPath $ini -Destination $iniBefore -Force
$launched=$false
$rows=@()
try {
    $launched=$true
    $boot = & (Join-Path $PSScriptRoot 'yaba_boot.ps1') -Iso $Iso -Sym $Sym -Port $Port -Until '_fzx_palette_status' `
        -UntilValue 1 -TimeoutSeconds 45 -KeepRunning -Visible -OutDir $OutDir
    if(($boot -join "`n") -notmatch 'FZX-EMU PASS'){throw "Palette boot failed: $boot"}
    Mcp pause @{}|Out-Null
    WriteWord hold 1
    foreach($case in $Cases){
        if($case -lt 0 -or $case -ge 72){throw "Invalid pose index"}
        WriteWord case $case
        $previous=Read sequence
        Mcp resume @{}|Out-Null
        # Discard one completed draw after changing the view, so captures show
        # the submitted view even if the CPU is paused before VDP1 submission.
        for($sample=-1;$sample -lt $Samples;$sample++){
            $deadline=(Get-Date).AddSeconds(20)
            do {
                Start-Sleep -Milliseconds 60
                $sequence=Read sequence
                $completed=Read completed_case
                if((Get-Date) -gt $deadline){throw "Palette case $case stalled"}
            }while($sequence -eq $previous -or $completed -ne $case)
            Mcp pause @{}|Out-Null
            $actual=Read vertex_hash
            $wanted=[Convert]::ToUInt32($expectedRows[$case].vertices,16)
            if((Read count) -ne $expectedRows[$case].triangles -or (Read bytes) -ne $expectedRows[$case].bytes){throw 'Palette source triangle or byte count mismatch'}
            if((Read light_hash) -ne [Convert]::ToUInt32($expectedRows[$case].lights,16)){throw 'Palette lighting hash mismatch'}
            if((Read saturated) -ne $expectedRows[$case].saturated){throw 'Palette saturation count mismatch'}
            if($actual -ne $wanted){throw ('Palette case {0} projected vertex hash mismatch {1:x8} != {2:x8}' -f $case,$actual,$wanted)}
            if($sample -lt 0){$previous=Read sequence;Mcp resume @{}|Out-Null;continue}
            $row=[ordered]@{case=$case;sample=$sample;hash=('{0:x8}' -f $actual);sequence=(Read sequence);project_ticks=(Read project_ticks);build_ticks=(Read build_ticks);saturated=(Read saturated);upload_ticks=(Read upload_ticks);wait_fields=(Read wait_fields);bytes=(Read bytes);triangles=(Read count)}
            $rows+=[PSCustomObject]$row
            $previous=$row.sequence
            if($sample -eq 0){
                $capture=Join-Path $OutDir "view-$case.png"
                Mcp framebuffer_screenshot @{path=$capture.Replace('\','/')}|Out-Null
                if(-not (Test-Path -LiteralPath $capture)){throw 'Palette screenshot was not saved'}
            }
            Mcp resume @{}|Out-Null
        }
        Mcp pause @{}|Out-Null
        Write-Output "PALETTE view=$case host-vertex-light-hashes PASS samples=$Samples"
    }
    WriteWord hold 0
    Mcp resume @{}|Out-Null
    $motion=@()
    for($i=0;$i -lt 20;$i++){
        Start-Sleep -Milliseconds 100
        $motion+=Read completed_case
    }
    Mcp pause @{}|Out-Null
    if(@($motion|Sort-Object -Unique).Count -lt 3){throw 'Automatic turning/banking did not advance'}
    Mcp framebuffer_screenshot @{path=(Join-Path $OutDir 'motion-after.png').Replace('\','/')}|Out-Null
    [ordered]@{tier='EMULATOR projection/light identity; MODELLED costs; visual adaptation unreviewed';timer='FRT divide32 with overflow extension';emulator_sha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLower();iso_sha256=(Get-FileHash -LiteralPath $Iso -Algorithm SHA256).Hash.ToLower();rows=$rows;motion_cases=$motion}|ConvertTo-Json -Depth 7|Set-Content -LiteralPath (Join-Path $OutDir 'palette-probe.json')
}finally{
    if($launched){
        Get-Process yabasanshiro -ErrorAction SilentlyContinue|Where-Object Path -eq $exe|Stop-Process
        Copy-Item -LiteralPath $iniBefore -Destination $ini -Force
    }
}
