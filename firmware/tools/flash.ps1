# Build and flash the Pikachu weather clock firmware to an ESP32-2432S028 (CYD).
# ASCII only: Windows PowerShell 5.1 reads .ps1 as ANSI, CJK would break parsing.
#
# Usage:
#   powershell -File firmware\tools\flash.ps1              # auto-detect port, build + upload
#   powershell -File firmware\tools\flash.ps1 -Port COM3   # force a port
#   powershell -File firmware\tools\flash.ps1 -Demo        # flash the demo env (fake data)
#   powershell -File firmware\tools\flash.ps1 -Monitor     # open serial monitor after upload
#
# Do NOT set $ErrorActionPreference='Stop' here: esptool writes progress to
# stderr, and PS 5.1 wraps native stderr into NativeCommandError, which would
# abort the script mid-flash.

param(
    [string]$Port = "",
    [switch]$Demo,
    [switch]$Monitor,
    [switch]$BuildOnly
)

$pio = Join-Path $env:USERPROFILE ".platformio\penv\Scripts\platformio.exe"
if (-not (Test-Path $pio)) {
    Write-Host "PlatformIO not found at $pio" -ForegroundColor Red
    Write-Host "Install it, or run: pip install platformio"
    exit 1
}

# firmware/ is the project dir (this script lives in firmware/tools/)
$proj = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$env:PLATFORMIO_DISABLE_PROGRESSBAR = "true"
$envName = if ($Demo) { "cyd-demo" } else { "cyd" }

Write-Host "=== Building [$envName] ===" -ForegroundColor Cyan
& $pio run -d $proj -e $envName
if ($LASTEXITCODE -ne 0) {
    Write-Host "Build FAILED" -ForegroundColor Red
    exit 1
}

$bin = Join-Path $proj ".pio\build\$envName\firmware.bin"
if (Test-Path $bin) {
    $kb = [math]::Round((Get-Item $bin).Length / 1024, 1)
    Write-Host "Built firmware.bin ($kb KB)" -ForegroundColor Green
}
if ($BuildOnly) { exit 0 }

# ---- pick a port -------------------------------------------------------
# The board shows up as a CH340 / USB-SERIAL bridge. Bluetooth virtual COM
# ports also exist on most laptops, so filter those out before guessing.
if (-not $Port) {
    $cands = @()
    try {
        $cands = Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue |
            Where-Object {
                # Match "... (COM7)" but skip Bluetooth virtual ports. The CJK
                # name for those is matched by codepoint escape, because this
                # file must stay ASCII (PS 5.1 reads .ps1 as ANSI).
                $bt = [char]0x84DD + [char]0x7259     # "Bluetooth" in Chinese
                $_.Name -match '\(COM\d+\)' -and
                $_.Name -notmatch 'Bluetooth' -and
                -not $_.Name.Contains($bt)
            } |
            ForEach-Object {
                if ($_.Name -match '\((COM\d+)\)') { $matches[1] }
            }
    } catch {}
    if ($cands.Count -eq 1) {
        $Port = $cands[0]
        Write-Host "Auto-detected board on $Port" -ForegroundColor Green
    } elseif ($cands.Count -gt 1) {
        Write-Host "Multiple candidate ports: $($cands -join ', ')" -ForegroundColor Yellow
        Write-Host "Re-run with -Port <COMx> to pick one."
        exit 1
    } else {
        Write-Host "No board detected." -ForegroundColor Yellow
        Write-Host "Plug the ESP32-2432S028 in over USB, then re-run."
        Write-Host "(All COM ports currently present look like Bluetooth virtual ports.)"
        Write-Host ""
        Write-Host "The build succeeded, so you can also flash manually later:"
        Write-Host "  powershell -File firmware\tools\flash.ps1 -Port COM3"
        exit 2
    }
}

Write-Host "=== Uploading to $Port ===" -ForegroundColor Cyan
& $pio run -d $proj -e $envName -t upload --upload-port $Port
if ($LASTEXITCODE -ne 0) {
    Write-Host "Upload FAILED" -ForegroundColor Red
    Write-Host "Tips: hold BOOT while it starts, or try a different USB cable/port."
    exit 1
}
Write-Host "Flashed OK" -ForegroundColor Green

if ($Monitor) {
    Write-Host "=== Serial monitor (Ctrl+C to quit) ===" -ForegroundColor Cyan
    & $pio device monitor -p $Port -b 115200
}
