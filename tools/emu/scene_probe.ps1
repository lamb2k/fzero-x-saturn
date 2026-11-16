<# Numeric scene-state gate. Synthetic input only; evidence tier EMULATOR.
Build tools/scene_probe first. FZX_SCENE_CONTROL=1 must yield 256 differences.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Iso,
    [Parameter(Mandatory)][string]$Sym,
    [Parameter(Mandatory)][string]$OutDir,
    [string]$HostResults=(Join-Path $PSScriptRoot '..\scene_probe\build\host.txt'),
    [ValidateSet(0,256)][int]$ExpectedBad=0,
    [int]$Port=9641,
    [switch]$Visible
)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))+[IO.Path]::DirectorySeparatorChar
if (([IO.Path]::GetFullPath($OutDir)+[IO.Path]::DirectorySeparatorChar).StartsWith($repo,[StringComparison]::OrdinalIgnoreCase)) {
    throw 'Scene evidence must stay outside the repository'
}
$rows=@(Get-Content -LiteralPath $HostResults)
if ($rows.Count -ne 256 -or $rows[-1] -notmatch '^255 ([0-9a-f]{8}) ([0-9a-f]{8})$') {
    throw 'Invalid source scene oracle results'
}
$wantBackdrop=[Convert]::ToUInt32($Matches[1],16)
$wantSkyline=[Convert]::ToUInt32($Matches[2],16)
if ($ExpectedBad) { $wantBackdrop=$wantBackdrop -bxor 1 }
$lines=@(& (Join-Path $PSScriptRoot 'yaba_boot.ps1') -Iso $Iso -Sym $Sym -Watch '' `
    -Until '_fzx_scene_result:0' -UntilValue 1 -TimeoutSeconds 180 -Dump '_fzx_scene_result:24' `
    -OutDir $OutDir -Port $Port -Visible:$Visible)
$lines | Set-Content -LiteralPath (Join-Path $OutDir 'run.log')
$dump=@($lines | Where-Object { $_ -match '^FZX-DUMP _fzx_scene_result [0-9A-Fa-f]{48}$' })
if ($dump.Count -ne 1) { throw 'No complete SH-2 scene result' }
$hex=($dump[0] -split ' ')[2]
$words=@(0..5 | ForEach-Object { [Convert]::ToUInt32($hex.Substring($_*8,8),16) })
$first=if ($ExpectedBad) {0u} else {[uint32]::MaxValue}
if ($words[0] -ne 1 -or $words[1] -ne 256 -or $words[2] -ne $ExpectedBad -or
    $words[3] -ne $first -or $words[4] -ne $wantBackdrop -or $words[5] -ne $wantSkyline) {
    throw "SH-2 scene state gate failed: $hex"
}
$receipt=[ordered]@{tier='EMULATOR';cases=256;differences=$words[2];expected_bad=$ExpectedBad;
    iso_sha256=(Get-FileHash -LiteralPath $Iso -Algorithm SHA256).Hash.ToLower();
    symbols_sha256=(Get-FileHash -LiteralPath $Sym -Algorithm SHA256).Hash.ToLower();
    emulator_sha256=(Get-FileHash -LiteralPath $env:FZX_YABA_EXE -Algorithm SHA256).Hash.ToLower();
    oracle_sha256=(Get-FileHash -LiteralPath $HostResults -Algorithm SHA256).Hash.ToLower();
    result_hex=$hex;hardware='UNRUN';native_render='UNRUN'}
$receipt | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutDir 'result.json')
Write-Output "FZX-SCENE PASS cases=256 differences=$ExpectedBad tier=EMULATOR"
