<#
G1.5 emulator lane: boot a Saturn image on a Yaba Sanshiro build that has the MCP debug server,
prove its main loop runs, and save a screenshot.

  tools/emu/yaba_boot.ps1 -Iso X.iso -Sym X.sym [-Watch _video_fields] [-WaitSeconds N]
                          [-Dump _sym:len,...] [-OutDir DIR] [-KeepRunning]
Needs $env:FZX_YABA_EXE (a Yaba Sanshiro 1.20.37 build with the MCP debug server). Each run
sets Debug\McpServerEnabled and Debug\McpServerPort (-Port, 9641) in %APPDATA%\YabaSanshiro\yabause.ini. The ini is shared
with other projects: it is backed up, set for this run (OpenGL video core 1, 4 MB DRAM cart
type 7, autostart, an absolute-path cue), and restored on exit.
Prints one line: "FZX-EMU PASS|FAIL ..." and exits 0 on PASS. PASS means the watched 32-bit
counter advanced between two reads three seconds apart (-Watch '' skips that check). After
-WaitSeconds, each -Dump symbol is read over MCP and printed as "FZX-DUMP <sym> <hex>".
Tier: EMULATOR.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$Iso,
    [Parameter(Mandatory)] [string]$Sym,
    [string]$Watch = '_video_fields',
    [int]$WaitSeconds = 0,
    [string[]]$Dump = @(),
    # Poll (every 2 s, up to -TimeoutSeconds) until the 32-bit word at 'symbol:offset' equals
    # -UntilValue, then dump. Exits FAIL on timeout.
    [string]$Until = '',
    [uint32]$UntilValue = 1,
    [int]$TimeoutSeconds = 300,
    [string]$OutDir = $(if ($env:FZX_EVIDENCE) { Join-Path $env:FZX_EVIDENCE 'emu' } else { Join-Path $HOME 'fzx-evidence\emu' }),
    [int]$Port = 9641,
    [switch]$Visible,
    [switch]$KeepRunning
)
$ErrorActionPreference = 'Stop'
$exe = $env:FZX_YABA_EXE
if (-not $exe -or -not (Test-Path -LiteralPath $exe)) { throw 'Set FZX_YABA_EXE to the Yaba Sanshiro MCP build' }
$Iso = (Resolve-Path -LiteralPath $Iso).Path
function SymAddr([string]$name) {
    $l = Get-Content -LiteralPath $Sym | Where-Object { ($_ -split '\s+')[2] -match ('^' + [regex]::Escape($name) + '(\.lto_priv\.\d+)?$') } | Select-Object -First 1
    if (-not $l) { throw "symbol $name not in $Sym" }
    return '0x' + ($l -split '\s+')[0]
}
$addr = if ($Watch) { SymAddr $Watch } else { $null }
New-Item -ItemType Directory -Force $OutDir | Out-Null

function Mcp([string]$name, [hashtable]$arguments) {
    $body = @{ jsonrpc = '2.0'; id = 1; method = 'tools/call'; params = @{ name = $name; arguments = $arguments } } |
        ConvertTo-Json -Depth 6 -Compress
    try {
        $r = Invoke-WebRequest -UseBasicParsing -TimeoutSec 30 "http://127.0.0.1:$Port/mcp" -Method Post `
            -ContentType 'application/json' -Headers @{ Accept = 'application/json, text/event-stream' } -Body $body
        return (($r.Content | ConvertFrom-Json).result.content | ForEach-Object { $_.text }) -join "`n"
    } catch { return 'down' }
}
function ReadWord { $t = Mcp 'memory_read' @{ address = $addr; length = 4 }
    if ($t -match ':\s+([0-9A-Fa-f]{2}) ([0-9A-Fa-f]{2}) ([0-9A-Fa-f]{2}) ([0-9A-Fa-f]{2})') {
        return [Convert]::ToUInt32($Matches[1] + $Matches[2] + $Matches[3] + $Matches[4], 16) }
    return $null }

# Other projects run their own Yaba builds (and their own MCP ports) beside this one: only
# another copy of this build is a conflict.
if (Get-Process yabasanshiro -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq (Resolve-Path -LiteralPath $exe).Path }) {
    throw 'This Yaba Sanshiro build is already running; stop it first' }
$ini = Join-Path $env:APPDATA 'YabaSanshiro\yabause.ini'
$backup = "$ini.fzx-backup"
Copy-Item -LiteralPath $ini -Destination $backup -Force
$proc = $null
try {
    # Relative FILE entries resolve against stale process state in this build: use an absolute cue.
    $cue = Join-Path $env:TEMP 'fzx-emu\fzx.yaba.cue'
    New-Item -ItemType Directory -Force (Split-Path $cue) | Out-Null
    Set-Content -LiteralPath $cue -Encoding ASCII -Value @("FILE `"$Iso`" BINARY", '  TRACK 01 MODE1/2048', '    INDEX 01 00:00:00')
    $s = Get-Content -LiteralPath $ini -Raw
    $set = [ordered]@{ 'General\CdRom' = '1'; 'General\CdRomISO' = $cue.Replace('\', '/'); 'Cartridge\Type' = '7';
                       'autostart' = 'true'; 'Video\VideoCore' = '1';  # core 4 (Vulkan) fails to start on this host
                       'Debug\McpServerEnabled' = 'true'; 'Debug\McpServerPort' = "$Port" }
    foreach ($e in $set.GetEnumerator()) {
        $pattern = '(?m)^' + [regex]::Escape($e.Key) + '=.*$'
        if ($s -match $pattern) { $s = $s -replace $pattern, ($e.Key + '=' + $e.Value) }
        else { $s = $s.TrimEnd() + "`r`n" + $e.Key + '=' + $e.Value + "`r`n" }
    }
    Set-Content -LiteralPath $ini -Value $s -NoNewline
    $proc = Start-Process -FilePath $exe -ArgumentList @('-nb', '-a', '-i', "`"$cue`"") -WorkingDirectory (Split-Path $cue) -WindowStyle $(if ($Visible) { 'Normal' } else { 'Hidden' }) -PassThru

    $status = 'down'
    for ($i = 0; $i -lt 60; $i++) {
        Start-Sleep 2
        $status = Mcp 'debug_status' @{}
        if ($status -match 'state: running') { break }
        if ($proc.HasExited) { break }
        $title = (Get-Process -Id $proc.Id -ErrorAction SilentlyContinue).MainWindowTitle
        if ($title -like 'Information*') { $status = 'emulator raised an error dialog (video or BIOS init)'; break }
    }
    if ($status -notmatch 'state: running') { Write-Output "FZX-EMU FAIL boot: $status"; exit 1 }
    Start-Sleep 2
    if ($WaitSeconds -gt 0) { Start-Sleep $WaitSeconds }
    if ($Until) {
        $uname, $uoff = $Until -split ':'
        $uaddr = '0x{0:X8}' -f ([Convert]::ToUInt32((SymAddr $uname).Substring(2), 16) + [int]$uoff)
        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        $seen = $null
        while ((Get-Date) -lt $deadline) {
            $t = Mcp 'memory_read' @{ address = $uaddr; length = 4 }
            if ($t -match ':\s+([0-9A-Fa-f]{2}) ([0-9A-Fa-f]{2}) ([0-9A-Fa-f]{2}) ([0-9A-Fa-f]{2})') {
                $seen = [Convert]::ToUInt32($Matches[1] + $Matches[2] + $Matches[3] + $Matches[4], 16)
                if ($seen -eq $UntilValue) { break }
            }
            Start-Sleep 2
        }
        if ($seen -ne $UntilValue) { Write-Output "FZX-EMU FAIL until $Until never became $UntilValue (last $seen)"; exit 1 }
    }
    $a = $null; $b = $null
    if ($addr) { $a = ReadWord; Start-Sleep 3; $b = ReadWord }
    foreach ($d in $Dump) {
        $name, $len = $d -split ':'
        $t = Mcp 'memory_read' @{ address = (SymAddr $name); length = [int]$len }
        $hex = (($t -split "`n") | ForEach-Object { if ($_ -match '^[0-9A-Fa-f]{8}:\s+((?:[0-9A-Fa-f]{2} ?)+)') { $Matches[1] } }) -join ' '
        Write-Output ("FZX-DUMP {0} {1}" -f $name, ($hex -replace '\s+', ''))
    }
    $png = Join-Path $OutDir ('boot-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.png')
    $shot = Mcp 'framebuffer_screenshot' @{ path = $png.Replace('\', '/') }
    $isoHash = (Get-FileHash -LiteralPath $Iso -Algorithm SHA256).Hash.ToLower()
    $pngHash = if (Test-Path -LiteralPath $png) { (Get-FileHash -LiteralPath $png -Algorithm SHA256).Hash.ToLower() } else { 'none' }
    $pass = (-not $addr) -or (($null -ne $a) -and ($null -ne $b) -and ($b -ne $a))
    $game = if ($status -match 'game: (\S+)') { $Matches[1] } else { '?' }
    Write-Output ("FZX-EMU {0} emulator yaba-1.20.37-mcp game {1} iso {2} watch {3}@{4} {5}->{6} png {7} png_sha256 {8}" -f `
        $(if ($pass) { 'PASS' } else { 'FAIL' }), $game, $isoHash.Substring(0, 16), $Watch, $addr, $a, $b, $png, $pngHash.Substring(0, 16))
    if (-not $pass) { exit 1 }
}
finally {
    if ($proc -and -not $KeepRunning -and -not $proc.HasExited) { Stop-Process -Id $proc.Id -Force; Start-Sleep 1 }
    if (-not $KeepRunning) { Copy-Item -LiteralPath $backup -Destination $ini -Force }
}
