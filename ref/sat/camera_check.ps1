<# Source-camera SH-2 diagnostic. Inputs are synthetic; timing is MODELLED. #>
[CmdletBinding()]
param([string[]]$Scripts=@('camera_controls'),[Parameter(Mandatory)][string]$OutDir)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$repo=[IO.Path]::GetFullPath($root)+[IO.Path]::DirectorySeparatorChar
if (([IO.Path]::GetFullPath($OutDir)+[IO.Path]::DirectorySeparatorChar).StartsWith($repo,[StringComparison]::OrdinalIgnoreCase)) {
    throw 'Camera evidence must stay outside the repository'
}
$ini=Join-Path $env:APPDATA 'YabaSanshiro\yabause.ini'
New-Item -ItemType Directory -Force $OutDir | Out-Null
$backup=Join-Path $OutDir 'yabause.ini.before'
Copy-Item -LiteralPath $ini -Destination $backup -Force
$rows=@()
try {
    foreach($s in $Scripts) {
        if($s -notmatch '^(T[0-9]{2}_[a-z_]+|camera_controls)$') { throw 'Invalid probe name' }
        $dir=Join-Path $PSScriptRoot ('build\camera\'+$s)
        $want=(Get-Content (Join-Path $dir 'expected_hash.txt')).Trim()
        $wantCamera=(Get-Content (Join-Path $dir 'expected_camera.txt')).Trim()
        $run=Join-Path $OutDir $s
        $output=& (Join-Path $root 'tools\emu\yaba_boot.ps1') -Iso (Join-Path $dir 'fzx-ref-sat.iso') `
            -Sym (Join-Path $dir 'yaul\fzx-ref-sat.sym') -Watch '' -Until '_fzx_sat_result:0' `
            -UntilValue 0x465A5852 -TimeoutSeconds 180 -Dump '_fzx_sat_result:56' `
            -Visible -OutDir $run
        $output | Set-Content (Join-Path $run 'result.log')
        $line=$output | Where-Object { $_ -like 'FZX-DUMP _fzx_sat_result *' } | Select-Object -First 1
        if(-not $line) { throw "Missing camera result: $s" }
        $hex=($line -split ' ')[2]
        if($hex.Length -ne 112) { throw 'Short camera result' }
        $w=@(0..13 | ForEach-Object { [Convert]::ToUInt32($hex.Substring($_*8,8),16) })
        $got=('{0:x8}{1:x8}' -f $w[3],$w[4]);$gotCamera=('{0:x8}' -f $w[11])
        $row=[ordered]@{script=$s;tier='EMULATOR';ticks=$w[2];state=$w[1];race_hash=$got;camera_hash=$gotCamera;
            expected_race=$want;expected_camera=$wantCamera;faults=$w[12];cameras=$w[13];
            modelled_frt_mean=($w[5]/[Math]::Max(1,$w[2]));modelled_frt_max=$w[6];max_tick=$w[7]}
        $rows+=$row
        $rows | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $OutDir 'camera-results.json')
        if($w[1] -ne 1 -or $w[12] -ne 0 -or $w[13] -ne 2 -or $got -ne $want -or $gotCamera -ne $wantCamera) {
            throw "Camera probe mismatch: $s race=$got/$want camera=$gotCamera/$wantCamera"
        }
        if($w[6] -gt 1000000) { throw "Invalid timer interval: $s" }
        Write-Output "CAMERA $s PASS $($w[2]) ticks, both source cameras exact"
    }
} finally {
    Copy-Item -LiteralPath $backup -Destination $ini -Force
}
