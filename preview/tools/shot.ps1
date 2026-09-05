# Screenshot the preview at a given page for design self-check.
# ASCII only (PowerShell 5.1 reads .ps1 as ANSI).
# Usage: powershell -File shot.ps1 <pageIndex> <outName.png> [budgetMs]
# NOTE: do NOT set $ErrorActionPreference='Stop' here. Edge writes benign noise
# to stderr, PS 5.1 wraps that as a NativeCommandError, and under 'Stop' the
# script aborts before Edge has written the PNG.
$ErrorActionPreference = 'Continue'
$page = if ($args.Count -gt 0) { $args[0] } else { '0' }
$name = if ($args.Count -gt 1) { $args[1] } else { "page$page.png" }
$budget = if ($args.Count -gt 2) { $args[2] } else { '3500' }

$edge = "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"
if (-not (Test-Path $edge)) { $edge = "C:\Program Files\Microsoft\Edge\Application\msedge.exe" }

$root = Split-Path $PSScriptRoot -Parent
$shotDir = Join-Path $root '_shot'
if (-not (Test-Path $shotDir)) { New-Item -ItemType Directory -Path $shotDir | Out-Null }
$out = Join-Path $shotDir $name
if (Test-Path $out) { Remove-Item $out -Force }

$uri = ([System.Uri](Join-Path $root 'ui.html')).AbsoluteUri + "#page=$page"
# Edge writes harmless noise to stderr; PS 5.1 turns that into NativeCommandError,
# so swallow both streams and judge success by whether the file appeared.
# A dedicated throwaway profile keeps concurrent/back-to-back runs from fighting
# over the default user data dir (which silently aborts the screenshot).
$profileDir = Join-Path $env:TEMP ("edge_shot_" + [guid]::NewGuid().ToString('N').Substring(0,8))
$edgeArgs = @(
  '--headless=new', '--disable-gpu', '--hide-scrollbars', '--no-first-run',
  '--no-default-browser-check', '--disable-extensions', '--allow-file-access-from-files',
  "--user-data-dir=$profileDir",
  '--window-size=1280,900', "--virtual-time-budget=$budget",
  "--screenshot=$out", $uri
)
& $edge $edgeArgs 2>&1 | Out-Null
Remove-Item $profileDir -Recurse -Force -ErrorAction SilentlyContinue

if (-not (Test-Path $out)) { throw "screenshot failed: $out" }

# Crop the 320x240 canvas at 3x out of the page, then upscale 2x for review.
Add-Type -AssemblyName System.Drawing
$src = [System.Drawing.Image]::FromFile($out)
$cw = 960; $ch = 720
$cx = 60; $cy = 148
$crop = New-Object System.Drawing.Rectangle($cx, $cy, $cw, $ch)
$bmp = New-Object System.Drawing.Bitmap($cw, $ch)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.DrawImage($src, (New-Object System.Drawing.Rectangle(0,0,$cw,$ch)), $crop, 'Pixel')
$g.Dispose(); $src.Dispose()
$cropOut = Join-Path $shotDir ("crop_" + $name)
$bmp.Save($cropOut, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Host "saved $out"
Write-Host "saved $cropOut"
