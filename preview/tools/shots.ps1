# Batch screenshots for design review. Drives the preview via its #-hooks.
# ASCII only. Usage: powershell -File shots.ps1
# NOTE: no $ErrorActionPreference='Stop' -- see shot.ps1 for why.
$ErrorActionPreference = 'Continue'

$edge = "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"
if (-not (Test-Path $edge)) { $edge = "C:\Program Files\Microsoft\Edge\Application\msedge.exe" }
$root = Split-Path $PSScriptRoot -Parent
$shotDir = Join-Path $root '_shot'
if (-not (Test-Path $shotDir)) { New-Item -ItemType Directory -Path $shotDir | Out-Null }
$base = ([System.Uri](Join-Path $root 'ui.html')).AbsoluteUri

Add-Type -AssemblyName System.Drawing

# name -> hash fragment
$shots = [ordered]@{
  'clock'        = 'page=0'
  'music_spec0'  = 'page=1&spec=0'
  'music_spec1'  = 'page=1&spec=1'
  'music_spec2'  = 'page=1&spec=2'
  'music_paused' = 'page=1&play=0'
  'music_btoff'  = 'page=1&bt=off'
  'weather'      = 'page=2'
  'weather7'     = 'page=2&v=1'
  'date'         = 'page=3'
  'settings'     = 'page=4'
  'clock_night'  = 'page=0&tod=night'
  'clock_rain'   = 'page=0&wx=rain'
  'clock_snow'   = 'page=0&wx=snow&tod=dusk'
  'clock_storm'  = 'page=0&wx=thunder'
}

foreach ($name in $shots.Keys) {
  $out = Join-Path $shotDir "$name.png"
  if (Test-Path $out) { Remove-Item $out -Force }
  $profileDir = Join-Path $env:TEMP ("edge_shot_" + [guid]::NewGuid().ToString('N').Substring(0,8))
  $uri = $base + '#' + $shots[$name]
  $a = @('--headless=new','--disable-gpu','--hide-scrollbars','--no-first-run',
         '--no-default-browser-check','--disable-extensions',
         "--user-data-dir=$profileDir",'--window-size=1280,900',
         '--virtual-time-budget=3200',"--screenshot=$out",$uri)
  & $edge $a 2>&1 | Out-Null
  Remove-Item $profileDir -Recurse -Force -ErrorAction SilentlyContinue
  if (Test-Path $out) {
    $src = [System.Drawing.Image]::FromFile($out)
    $bmp = New-Object System.Drawing.Bitmap(960, 720)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.DrawImage($src, (New-Object System.Drawing.Rectangle(0,0,960,720)),
                       (New-Object System.Drawing.Rectangle(60,148,960,720)), 'Pixel')
    $g.Dispose(); $src.Dispose()
    $bmp.Save((Join-Path $shotDir "crop_$name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Host "ok   $name"
  } else {
    Write-Host "FAIL $name"
  }
}

# Contact sheet so one image shows every page at once.
$names = @($shots.Keys)
$cols = 4
$rows = [Math]::Ceiling($names.Count / $cols)
$cw = 320; $ch = 240; $pad = 6; $labelH = 14
$sheet = New-Object System.Drawing.Bitmap((($cw+$pad)*$cols+$pad), (($ch+$labelH+$pad)*$rows+$pad))
$sg = [System.Drawing.Graphics]::FromImage($sheet)
$sg.Clear([System.Drawing.Color]::FromArgb(20,22,26))
$font = New-Object System.Drawing.Font('Consolas', 8)
$wbr = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(230,230,220))
for ($i = 0; $i -lt $names.Count; $i++) {
  $f = Join-Path $shotDir ("crop_" + $names[$i] + ".png")
  if (-not (Test-Path $f)) { continue }
  $x = $pad + ($i % $cols) * ($cw + $pad)
  $y = $pad + [Math]::Floor($i / $cols) * ($ch + $labelH + $pad)
  $im = [System.Drawing.Image]::FromFile($f)
  $sg.DrawImage($im, $x, $y, $cw, $ch)
  $im.Dispose()
  $sg.DrawString($names[$i], $font, $wbr, $x, ($y + $ch + 1))
}
$sg.Dispose(); $font.Dispose(); $wbr.Dispose()
$sheet.Save((Join-Path $shotDir '_sheet.png'), [System.Drawing.Imaging.ImageFormat]::Png)
$sheet.Dispose()
Write-Host "saved contact sheet: $(Join-Path $shotDir '_sheet.png')"
