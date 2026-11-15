<#
Statistical profile of a running Saturn image over the Yaba MCP lane: sample the master SH-2's
PC repeatedly and attribute each sample to the function containing it (from the image's .sym).

  tools/emu/pc_profile.ps1 -Sym X.sym [-Samples 2000] [-Top 25] [-Cpu master]

Run it while the image is in the phase to measure (boot with yaba_boot.ps1 -KeepRunning).
MODELLED: sampling an emulator says where emulated time goes, not hardware time.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$Sym,
    [int]$Samples = 2000,
    [int]$Top = 25,
    [string]$Cpu = 'master',
    [int]$Port = 9640
)
$ErrorActionPreference = 'Stop'
$funcs = Get-Content -LiteralPath $Sym | ForEach-Object {
    $f = $_ -split '\s+'
    if ($f.Count -ge 3 -and $f[1] -match '^[Tt]$') { [pscustomobject]@{ Addr = [Convert]::ToUInt32($f[0], 16); Name = $f[2] } }
} | Sort-Object Addr
$addrs = [uint32[]]($funcs | ForEach-Object { $_.Addr })
$names = [string[]]($funcs | ForEach-Object { $_.Name })
$counts = @{}
$body = (@{ jsonrpc = '2.0'; id = 1; method = 'tools/call'; params = @{ name = 'registers_get'; arguments = @{ cpu = $Cpu } } } |
    ConvertTo-Json -Depth 5 -Compress)
$got = 0
for ($i = 0; $i -lt $Samples; $i++) {
    try {
        $r = Invoke-WebRequest -UseBasicParsing -TimeoutSec 10 "http://127.0.0.1:$Port/mcp" -Method Post `
            -ContentType 'application/json' -Headers @{ Accept = 'application/json, text/event-stream' } -Body $body
    } catch { continue }
    if ($r.Content -match 'PC=0x([0-9A-Fa-f]+)') {
        $pc = [Convert]::ToUInt32($Matches[1], 16)
        $lo = 0; $hi = $addrs.Length - 1; $idx = -1
        while ($lo -le $hi) { $mid = [int](($lo + $hi) / 2); if ($addrs[$mid] -le $pc) { $idx = $mid; $lo = $mid + 1 } else { $hi = $mid - 1 } }
        $n = if ($idx -ge 0) { $names[$idx] } else { '?' }
        $counts[$n] = 1 + $(if ($counts.ContainsKey($n)) { $counts[$n] } else { 0 })
        $got++
    }
}
Write-Output "pc_profile: $got samples of the $Cpu SH-2 (MODELLED)"
$counts.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First $Top | ForEach-Object {
    Write-Output ('{0,6:P1} {1,6} {2}' -f ($_.Value / [Math]::Max(1, $got)), $_.Value, $_.Key)
}
