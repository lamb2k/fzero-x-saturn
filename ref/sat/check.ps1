<#
Reference-on-SH-2 check: boot each probe image built by ref/sat/build.sh on the Yaba MCP lane,
wait until it finishes, and compare its trace hash with the host reference (STRICT).

  ref/sat/check.ps1 [-Scripts T01_idle,T02_full_throttle,...]

Prints one line per script and a summary; exits 0 only if every hash matches. Cycle figures
come from the SH-2 free-running timer inside the emulator: MODELLED, not hardware.
#>
[CmdletBinding()]
param([string[]]$Scripts = @())
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$build = Join-Path $PSScriptRoot 'build'
if (-not $Scripts) { $Scripts = Get-Content (Join-Path $root 'ref\expected\fixture.hashes') | ForEach-Object { ($_ -split '\s+')[0] } }
$boot = Join-Path $root 'tools\emu\yaba_boot.ps1'
$fail = 0
$budget = 229091  # 8 ms of the 28.636 MHz master SH-2
foreach ($s in $Scripts) {
    $dir = Join-Path $build $s
    $want = (Get-Content (Join-Path $dir 'expected_hash.txt')).Trim()
    $out = & $boot -Iso (Join-Path $dir 'fzx-ref-sat.iso') -Sym (Join-Path $dir 'yaul\fzx-ref-sat.sym') -Watch '' `
        -Until '_fzx_sat_result:4' -UntilValue 1 -TimeoutSeconds 600 -Dump '_fzx_sat_result:44'
    $line = $out | Where-Object { $_ -like 'FZX-DUMP _fzx_sat_result *' } | Select-Object -First 1
    if (-not $line) { Write-Output "$s FAIL $($out -join ' ')"; $fail++; continue }
    $hex = ($line -split ' ')[2]
    $w = 0..10 | ForEach-Object { [Convert]::ToUInt32($hex.Substring($_ * 8, 8), 16) }
    $got = ('{0:x8}{1:x8}' -f $w[3], $w[4])
    $ticks = $w[2]; $raceTicks = [Math]::Max(1, $w[9])
    $mean = 32.0 * $w[5] / [Math]::Max(1, $ticks); $raceMean = 32.0 * $w[8] / $raceTicks; $max = 32 * $w[6]
    $ok = $got -eq $want
    if (-not $ok) { $fail++ }
    Write-Output ('{0} {1} ticks {2} hash {3} host {4} | MODELLED cycles/tick all {5:N0}, race {6:N0} ({7:P0} of 8 ms), max {8:N0} at tick {9}' -f `
        $s, $(if ($ok) { 'STRICT' } else { 'DIFF' }), $ticks, $got, $want, $mean, $raceMean, ($raceMean / $budget), $max, $w[7])
}
Write-Output ("ref-on-SH-2: {0} of {1} scripts STRICT" -f ($Scripts.Count - $fail), $Scripts.Count)
exit [int]($fail -ne 0)
