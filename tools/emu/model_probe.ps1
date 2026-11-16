<# Fixed-pose native model probe: compare host hashes and collect modeled FRT costs.
   Expected JSON is generated locally from tools/model_probe/host.c. All retail
   pixels, screenshots, images and detailed rows remain outside the repository. #>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Iso,
    [Parameter(Mandatory)][string]$Sym,
    [Parameter(Mandatory)][string]$Expected,
    [Parameter(Mandatory)][string]$OutDir,
    [int]$Samples=8,
    [int]$Port=9641
)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))+[IO.Path]::DirectorySeparatorChar
if(([IO.Path]::GetFullPath($OutDir)+[IO.Path]::DirectorySeparatorChar).StartsWith($repo,[StringComparison]::OrdinalIgnoreCase)){
    throw 'Model evidence must stay outside the repository'
}
if($Samples -lt 1 -or $Samples -gt 120){throw 'Samples must be 1..120'}
$exe=(Resolve-Path -LiteralPath $env:FZX_YABA_EXE).Path
if(Get-Process yabasanshiro -ErrorAction SilentlyContinue|Where-Object Path -eq $exe){throw 'This emulator is already running'}
$expectedRows=@(Get-Content -LiteralPath $Expected -Raw|ConvertFrom-Json)
if($expectedRows.Count -ne 8 -or @($expectedRows|Where-Object {$_.hash -notmatch '^[a-fA-F0-9]{8}$'}).Count){throw 'Eight host model hashes required'}
$symbols=@{}
foreach($line in Get-Content -LiteralPath $Sym){
    $parts=$line -split '\s+'
    if($parts.Count -ge 3 -and $parts[2].StartsWith('_fzx_model_probe_')){$symbols[$parts[2]]='0x'+$parts[0]}
}
function Mcp($name,$arguments){
    $body=@{jsonrpc='2.0';id=1;method='tools/call';params=@{name=$name;arguments=$arguments}}|ConvertTo-Json -Depth 5 -Compress
    $r=Invoke-RestMethod "http://127.0.0.1:$Port/mcp" -Method Post -ContentType application/json -Body $body -TimeoutSec 30
    if($r.error -or $r.result.isError){throw "MCP failed: $name"}
    ($r.result.content|ForEach-Object text)-join "`n"
}
function Read($short){
    $name='_fzx_model_probe_'+$short
    if(-not $symbols.ContainsKey($name)){throw "Missing probe symbol: $name"}
    $text=Mcp memory_read @{address=$symbols[$name];length=4}
    if($text -notmatch ':\s+([A-Fa-f0-9]{2}) ([A-Fa-f0-9]{2}) ([A-Fa-f0-9]{2}) ([A-Fa-f0-9]{2})'){throw 'Short model read'}
    [Convert]::ToUInt32($Matches[1]+$Matches[2]+$Matches[3]+$Matches[4],16)
}
function WriteWord($short,[uint32]$value){
    $name='_fzx_model_probe_'+$short
    if(-not $symbols.ContainsKey($name)){throw "Missing probe symbol: $name"}
    $result=Mcp memory_write @{address=$symbols[$name];size='long';value=('0x{0:x}' -f $value)}
    if($result -notmatch '^wrote'){throw 'Model probe hook write failed'}
}
New-Item -ItemType Directory -Force $OutDir|Out-Null
$ini=Join-Path $env:APPDATA 'YabaSanshiro\yabause.ini'
$iniBefore=Join-Path $OutDir 'yabause.ini.before'
Copy-Item -LiteralPath $ini -Destination $iniBefore -Force
$launched=$false
$rows=@()
try {
    $launched=$true
    $boot = & (Join-Path $PSScriptRoot 'yaba_boot.ps1') -Iso $Iso -Sym $Sym -Port $Port -Until '_fzx_model_probe_status' `
        -UntilValue 1 -TimeoutSeconds 45 -KeepRunning -Visible -OutDir $OutDir
    if(($boot -join "`n") -notmatch 'FZX-EMU PASS'){throw "Model boot failed: $boot"}
    Mcp pause @{}|Out-Null
    WriteWord hold 1
    for($case=0;$case -lt 8;$case++){
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
                if((Get-Date) -gt $deadline){throw "Model case $case stalled"}
            }while($sequence -eq $previous -or $completed -ne $case)
            Mcp pause @{}|Out-Null
            $actual=Read hash
            $wanted=[Convert]::ToUInt32($expectedRows[$case].hash,16)
            if($actual -ne $wanted){throw ('Model case {0} pixel hash mismatch {1:x8} != {2:x8}' -f $case,$actual,$wanted)}
            if($sample -lt 0){$previous=Read sequence;Mcp resume @{}|Out-Null;continue}
            $row=[ordered]@{case=$case;sample=$sample;hash=('{0:x8}' -f $actual);sequence=(Read sequence);render_ticks=(Read render_ticks);total_ticks=(Read total_ticks);render_fields=(Read render_fields);pixels=(Read pixels);tested=(Read tested);triangles=(Read draws)}
            $rows+=[PSCustomObject]$row
            $previous=$row.sequence
            if($sample -eq 0){
                $capture=Join-Path $OutDir "view-$case.png"
                Mcp framebuffer_screenshot @{path=$capture.Replace('\','/')}|Out-Null
                if(-not (Test-Path -LiteralPath $capture)){throw 'Model screenshot was not saved'}
            }
            Mcp resume @{}|Out-Null
        }
        Mcp pause @{}|Out-Null
        Write-Output "MODEL view=$case host-pixel-hash PASS samples=$Samples"
    }
    [ordered]@{tier='EMULATOR pixels; MODELLED costs';timer='FRT divide32 with overflow extension';emulator_sha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLower();iso_sha256=(Get-FileHash -LiteralPath $Iso -Algorithm SHA256).Hash.ToLower();rows=$rows}|ConvertTo-Json -Depth 7|Set-Content -LiteralPath (Join-Path $OutDir 'model-probe.json')
}finally{
    if($launched){
        Get-Process yabasanshiro -ErrorAction SilentlyContinue|Where-Object Path -eq $exe|Stop-Process
        Copy-Item -LiteralPath $iniBefore -Destination $ini -Force
    }
}
