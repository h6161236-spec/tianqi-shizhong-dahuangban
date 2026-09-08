# Flash the Pikachu weather clock firmware to an ESP32-2432S028 (CYD).
# Standalone: flashes the prebuilt .bin files next to this script, no
# PlatformIO project needed.
#
# ASCII only: Windows PowerShell 5.1 reads .ps1 as ANSI, CJK would break parsing.
#
# Usage:
#   powershell -File flash.ps1                 # auto-detect port, flash final build
#   powershell -File flash.ps1 -Port COM3      # force a port
#   powershell -File flash.ps1 -Monitor        # open serial monitor after flashing
#   powershell -File flash.ps1 -Erase          # full chip erase first (clears WiFi/prefs)
#   powershell -File flash.ps1 -Baud 460800    # faster, if your cable is reliable
#
# Do NOT set $ErrorActionPreference='Stop': esptool writes progress to stderr,
# and PS 5.1 wraps native stderr into NativeCommandError, which would abort the
# script mid-flash.

param(
    [string]$Port = "",
    [int]$Baud = 115200,
    [switch]$Monitor,
    [switch]$Erase
)

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$dir  = Join-Path $here "final"
$label = "final"

# ---- locate the three images ------------------------------------------
$boot = Join-Path $dir "bootloader.bin"
$part = Join-Path $dir "partitions.bin"
$app  = Join-Path $dir "firmware.bin"
foreach ($f in @($boot, $part, $app)) {
    if (-not (Test-Path $f)) {
        Write-Host "Missing image: $f" -ForegroundColor Red
        exit 1
    }
}

# ---- locate esptool ----------------------------------------------------
# Prefer the copy PlatformIO built with, fall back to a pip-installed one.
$pioPy   = Join-Path $env:USERPROFILE ".platformio\penv\Scripts\python.exe"
$pioTool = Join-Path $env:USERPROFILE ".platformio\packages\tool-esptoolpy\esptool.py"
$exe = $null
$pre = @()
if ((Test-Path $pioPy) -and (Test-Path $pioTool)) {
    $exe = $pioPy
    $pre = @($pioTool)
} else {
    $py = (Get-Command python -ErrorAction SilentlyContinue)
    if ($py) {
        & $py.Source -c "import esptool" 2>$null
        if ($LASTEXITCODE -eq 0) {
            $exe = $py.Source
            $pre = @("-m", "esptool")
        }
    }
}
if (-not $exe) {
    Write-Host "esptool not found." -ForegroundColor Red
    Write-Host "Install it with:  pip install esptool"
    exit 1
}

# ---- pick a port -------------------------------------------------------
# The board shows up as a CH340 / USB-SERIAL bridge. Laptops also expose
# Bluetooth virtual COM ports, so filter those out before guessing.
if (-not $Port) {
    $cands = @()
    try {
        # "Bluetooth" in Chinese, by codepoint: this file must stay ASCII.
        $bt = [char]0x84DD + [char]0x7259
        # @(...) is required: with a single candidate the pipeline yields a
        # scalar string, and $cands[0] on a string returns its first char ("C").
        $cands = @(
            Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue |
                Where-Object {
                    $_.Name -match '\(COM\d+\)' -and
                    $_.Name -notmatch 'Bluetooth' -and
                    -not $_.Name.Contains($bt)
                } | ForEach-Object {
                    if ($_.Name -match '\((COM\d+)\)') { $matches[1] }
                }
        )
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
        Write-Host "Plug the ESP32-2432S028 in over USB, then re-run"
        Write-Host "(or pass -Port COMx if you know which port it is)."
        exit 2
    }
}

$kb = [math]::Round((Get-Item $app).Length / 1024, 1)
Write-Host "=== Flashing $label build ($kb KB) to $Port ===" -ForegroundColor Cyan

if ($Erase) {
    Write-Host "Erasing whole chip (this clears saved WiFi and settings)..." -ForegroundColor Yellow
    & $exe @pre --chip esp32 --port $Port erase_flash
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Erase FAILED" -ForegroundColor Red
        exit 1
    }
}

# Offsets come from the huge_app partition table this firmware is built with:
#   0x1000  bootloader     0x8000  partition table     0x10000  app
#
# 115200, matching platformio.ini: the CH340 link on this board drops out
# intermittently at 460800. ~1.7MB takes about 2.5 minutes -- slow but reliable.
# Pass -Baud 460800 if your cable handles it.
& $exe @pre --chip esp32 --port $Port --baud $Baud `
    write_flash -z --flash_mode dio --flash_freq 40m --flash_size detect `
    0x1000 $boot 0x8000 $part 0x10000 $app

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "Flash FAILED" -ForegroundColor Red
    Write-Host "Things to try:"
    Write-Host "  - hold the BOOT button while the flash starts"
    Write-Host "  - a different USB cable or port (some cables are power-only)"
    Write-Host "  - a lower speed: -Baud 115200 (or even 74880)"
    exit 1
}

Write-Host ""
Write-Host "Flashed OK - the board should reboot into the clock page." -ForegroundColor Green
Write-Host "First boot has no WiFi saved: swipe to Settings > WiFi to set it up."

if ($Monitor) {
    Write-Host "=== Serial monitor at 115200 (Ctrl+C to quit) ===" -ForegroundColor Cyan
    & $exe @pre version | Out-Null   # keep esptool from holding the port
    $sp = New-Object System.IO.Ports.SerialPort $Port, 115200, 'None', 8, 'One'
    try {
        $sp.Open()
        while ($true) {
            if ($sp.BytesToRead -gt 0) { Write-Host -NoNewline $sp.ReadExisting() }
            Start-Sleep -Milliseconds 40
        }
    } finally {
        if ($sp.IsOpen) { $sp.Close() }
    }
}
