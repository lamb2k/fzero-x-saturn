<# Boot-only road storage gate. Evidence and asset bytes remain outside git.
Validates the actual cartridge bytes, not just the loader's success flag.
Use CartridgeType=0 / ExpectedStatus=2 for the missing-cartridge control;
an image with one changed ROAD.PAK byte must yield ExpectedStatus=6. #>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$Iso,
    [Parameter(Mandatory)] [string]$Sym,
    [Parameter(Mandatory)] [string]$Pack,
    [Parameter(Mandatory)] [string]$OutDir,
    [ValidateSet(0,7)] [int]$CartridgeType=7,
    [ValidateSet(1,2,4,5,6)] [int]$ExpectedStatus=1,
    [int]$Port=9641
)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))+[IO.Path]::DirectorySeparatorChar
if(([IO.Path]::GetFullPath($OutDir)+[IO.Path]::DirectorySeparatorChar).StartsWith($repo,[StringComparison]::OrdinalIgnoreCase)) {
    throw 'Road evidence must stay outside the repository'
}
$exe=(Resolve-Path -LiteralPath $env:FZX_YABA_EXE).Path
if(Get-Process yabasanshiro -ErrorAction SilentlyContinue | Where-Object {$_.Path -eq $exe}) {throw 'This emulator build is already running'}
$ini=Join-Path $env:APPDATA 'YabaSanshiro\yabause.ini'
New-Item -ItemType Directory -Force $OutDir | Out-Null
$backup=Join-Path $OutDir 'yabause.ini.before'
Copy-Item -LiteralPath $ini -Destination $backup -Force
$launched=$false
function Mcp([string]$name,[hashtable]$arguments) {
    $body=@{jsonrpc='2.0';id=1;method='tools/call';params=@{name=$name;arguments=$arguments}} | ConvertTo-Json -Depth 6 -Compress
    $r=Invoke-RestMethod -Uri "http://127.0.0.1:$Port/mcp" -Method Post -ContentType application/json -Body $body -TimeoutSec 30
    if($r.error -or $r.result.isError) {throw "MCP failed: $name"}
    ($r.result.content | ForEach-Object {$_.text}) -join "`n"
}
function ReadBytes([uint32]$at,[int]$length) {
    $text=Mcp memory_read @{address=('0x{0:X8}' -f $at);length=$length}
    $hex=((($text -split "`n") | ForEach-Object {if($_ -match '^[0-9A-Fa-f]{8}:\s+((?:[0-9A-Fa-f]{2} ?)+)') {$Matches[1]}}) -join '') -replace '\s+',''
    if($hex.Length -ne $length*2) {throw 'Short cartridge read'}
    return ,([Convert]::FromHexString($hex))
}
function Symbol([string]$name) {
    $line=Get-Content -LiteralPath $Sym | Where-Object {($_ -split '\s+')[2] -match ('^'+[regex]::Escape($name)+'(\.lto_priv\.\d+)?$')} | Select-Object -First 1
    if(-not $line) {throw "Missing symbol $name"}
    [Convert]::ToUInt32(($line -split '\s+')[0],16)
}
try {
    $launched=$true
    & (Join-Path $PSScriptRoot 'yaba_boot.ps1') -Iso $Iso -Sym $Sym -Port $Port -CartridgeType $CartridgeType `
        -Watch '' -Until '_fzx_road_load_status:0' -UntilValue $ExpectedStatus -TimeoutSeconds 60 -KeepRunning -Visible -OutDir $OutDir
    $state=ReadBytes (Symbol '_fzx_road_load_status') 32
    $words=@(for($i=0;$i -lt 8;$i++) {[Convert]::ToUInt32([Convert]::ToHexString($state[($i*4)..($i*4+3)]),16)})
    if($words[0] -ne $ExpectedStatus) {throw 'Road status differs'}
    $report=[ordered]@{tier='EMULATOR';status=$words[0];cartridge_id=$words[1];bytes=$words[2];hash=$words[3];sectors=$words[4];
        vertices=$words[5];triangles=$words[6];chunks=$words[7];iso_sha256=(Get-FileHash $Iso -Algorithm SHA256).Hash.ToLower()}
    if($ExpectedStatus -eq 1) {
        $expected=[IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $Pack).Path)
        if($words[2] -ne $expected.Length) {throw 'Loaded pack size differs'}
        $actual=[byte[]]::new($expected.Length)
        for($at=0;$at -lt $expected.Length;$at+=4096) {
            $n=[Math]::Min(4096,$expected.Length-$at)
            $part=ReadBytes ([uint32](0x22400000+$at)) $n
            [Array]::Copy($part,0,$actual,$at,$n)
        }
        $actualHash=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($actual))
        $expectedHash=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($expected))
        if($actualHash -ne $expectedHash) {throw 'Cartridge bytes differ from the receipted pack'}
        [IO.File]::WriteAllBytes((Join-Path $OutDir 'cartridge-road.bin'),$actual)
        $report.pack_sha256=$actualHash.ToLower()
    } else {
        $timing=ReadBytes (Symbol '_fzx_timing') 4
        if([Convert]::ToHexString($timing) -ne '00000000') {throw 'Failed road load entered the race loop'}
    }
    $report | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutDir 'road-load.json')
    Write-Output "FZX-ROAD-LOAD PASS status=$ExpectedStatus bytes=$($words[2]) triangles=$($words[6])"
} catch {
    if($launched) {
        try {
            Mcp framebuffer_screenshot @{path=(Join-Path $OutDir 'failed.png').Replace('\','/')} | Out-Null
            Mcp registers_get @{cpu='master'} | Set-Content -LiteralPath (Join-Path $OutDir 'failed-registers.txt')
        } catch {}
    }
    throw
} finally {
    if($launched) {
        Get-Process yabasanshiro -ErrorAction SilentlyContinue | Where-Object {$_.Path -eq $exe} | Stop-Process
        Copy-Item -LiteralPath $backup -Destination $ini -Force
    }
}
