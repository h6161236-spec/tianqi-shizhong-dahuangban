# Pikachu side-view run cycle - design self-check renderer.
# ASCII only: Windows PowerShell 5.1 reads .ps1 as ANSI, so no CJK here.
# Usage: powershell -File pika-run.ps1 [out.png]
# Parts: body 24x20 (head+ears+face+torso) / tail 11x12 / legs 24x4 x4 / arm 5x3 x4
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$pal = @{}
$pal['Y'] = @(248,208,48)    # pikachu yellow
$pal['L'] = @(252,232,144)   # highlight
$pal['D'] = @(214,154,16)    # dark yellow (paws, shading)
$pal['K'] = @(40,32,16)      # black (ear tips, eye, nose, mouth)
$pal['R'] = @(232,64,40)     # cheek red
$pal['W'] = @(248,248,240)   # eye glint
$pal['B'] = @(140,96,44)     # brown (back stripes, tail root)

# ---- body: head right, torso left, ears swept up-back (24 x 20) ----
$body = @(
  '.........KK...KK........'   # r0  ear tips
  '.........KK...KK........'   # r1
  '.........KK...KK........'   # r2
  '.........YY...YY........'   # r3  ear shafts
  '..........YY...YY.......'   # r4
  '..........YYYYYYY.......'   # r5  crown
  '.........YYYYYYYYYY.....'   # r6
  '........YYYYYYYYYYYY....'   # r7
  '.......YYYYYYYYKKYYYY...'   # r8  eye
  '......YYYYYYYYYKWYYYY...'   # r9  eye glint
  '.....YYYYYYYYYYYYYYYYK..'   # r10 nose
  '...YYYYYYYYYYRRRYYYYKK..'   # r11 cheek + mouth
  '..YYYYYYYYYYYRRRYYYYY...'   # r12
  '.BBYYYYYYYYYYYYYYYYYY...'   # r13 back stripe 1
  'BBBYYYYYYYYYYYYYYYYY....'   # r14 back stripe 2
  '.YYYYYYYYYYYYYYYYYYY....'   # r15
  '.YYYYYYYYYYYYYYYYYY.....'   # r16
  '..YYYYYYYYYYYYYYYY......'   # r17
  '...YYYYYYYYYYYYYY.......'   # r18 belly
  '.....YYYYYYYYYY.........'   # r19 belly bottom
)

# ---- lightning tail (11 x 12), brown root bottom-right hooks onto the rump ----
$tail = @(
  '..YYYYYY...'
  '..YYYYYY...'
  '..YYYYY....'
  '...YYY.....'
  '..YYY......'
  '..YYY......'
  '...YYY.....'
  '....YYY....'
  '.....YYY...'
  '......YYY..'
  '.......BB..'
  '........B..'
)

# ---- legs (24 x 4), 4-frame gallop: stride / tuck / cross / tuck alt ----
$legs = @(
  @(  # f0 full stride, front paw forward + rear paw back
    '.....YYY.....YYY........'
    '....YYY.......YYY.......'
    '...YYY.........YYY......'
    '..DDDD.........DDDD.....'
  ),
  @(  # f1 airborne tuck, both legs gathered
    '......YYY..YYY..........'
    '......YYY..YYY..........'
    '.....DDDD..DDDD.........'
    '........................'
  ),
  @(  # f2 crossing, legs scissor past each other
    '.......YYY.YYY..........'
    '......YYY...YYY.........'
    '.....YYY.....YYY........'
    '....DDDD.....DDDD.......'
  ),
  @(  # f3 tuck, opposite phase
    '.......YYY..YYY.........'
    '.......YYY..YYY.........'
    '......DDDD..DDDD........'
    '........................'
  )
)

# ---- front paw (5 x 3), tucked under the chest, swings with the step ----
$arms = @(
  @('..YYY','.YYDD','.....'),
  @('.YYY.','.YDD.','.....'),
  @('YYY..','YDD..','.....'),
  @('.YYY.','.YDD.','.....')
)

# ---- width self-check ----
$bad = 0
foreach ($r in $body) { if ($r.Length -ne 24) { Write-Host "BAD body $($r.Length) $r"; $bad++ } }
foreach ($r in $tail) { if ($r.Length -ne 11) { Write-Host "BAD tail $($r.Length) $r"; $bad++ } }
for ($i = 0; $i -lt 4; $i++) {
  foreach ($r in $legs[$i]) { if ($r.Length -ne 24) { Write-Host "BAD legs$i $($r.Length) $r"; $bad++ } }
  foreach ($r in $arms[$i]) { if ($r.Length -ne 5)  { Write-Host "BAD arms$i $($r.Length) $r"; $bad++ } }
}
if ($bad -gt 0) { throw "$bad rows have wrong width" }

function DrawRows($gfx, $rows, $ox, $oy, $sc) {
  for ($y = 0; $y -lt $rows.Count; $y++) {
    $line = $rows[$y]
    for ($x = 0; $x -lt $line.Length; $x++) {
      $c = $line.Substring($x, 1)
      if ($pal.ContainsKey($c)) {
        $rgb = $pal[$c]
        $br = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb($rgb[0], $rgb[1], $rgb[2]))
        $gfx.FillRectangle($br, ($ox + $x*$sc), ($oy + $y*$sc), $sc, $sc)
        $br.Dispose()
      }
    }
  }
}

# frame layout in sprite pixels: tail at (-9,+5) from body, legs at (+0,+19), arm at (+13,+16)
function DrawPika($gfx, $f, $ox, $oy, $sc) {
  $bob = 0; if ($f -eq 1 -or $f -eq 3) { $bob = -1 }
  DrawRows $gfx $tail      ($ox - 9*$sc)  ($oy + (5 + $bob)*$sc)  $sc
  DrawRows $gfx $body       $ox           ($oy + $bob*$sc)        $sc
  DrawRows $gfx $legs[$f]   $ox           ($oy + (19 + $bob)*$sc) $sc
  DrawRows $gfx $arms[$f]  ($ox + 13*$sc) ($oy + (16 + $bob)*$sc) $sc
}

# ---- compose: 4 frames blown up + a 2x strip at real on-panel scale ----
$SC = 7
$FW = 34; $FH = 28; $PAD = 8
$BX = 10; $BY = 2
$sheetW = ($FW*$SC + $PAD)*4 + $PAD
$sheetH = $FH*$SC + $PAD*2 + 80
$bmp = New-Object System.Drawing.Bitmap($sheetW, $sheetH)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.Clear([System.Drawing.Color]::FromArgb(126,176,220))

for ($f = 0; $f -lt 4; $f++) {
  $ox = $PAD + ($FW*$SC + $PAD)*$f
  $gb = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(94,156,58))
  $g.FillRectangle($gb, $ox, ($PAD + 25*$SC), ($FW*$SC), (3*$SC)); $gb.Dispose()
  DrawPika $g $f ($ox + $BX*$SC) ($PAD + $BY*$SC) $SC
}

$stripY = $FH*$SC + $PAD + 16
$gb2 = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(94,156,58))
$g.FillRectangle($gb2, 0, ($stripY + 23*2), $sheetW, 10); $gb2.Dispose()
for ($f = 0; $f -lt 4; $f++) { DrawPika $g $f ($PAD + 24 + $f*80) $stripY 2 }

$g.Dispose()
$out = if ($args.Count -gt 0) { $args[0] } else { '_pika_run_check.png' }
$out = Join-Path (Split-Path $PSScriptRoot -Parent) ('_pika\' + $out)
$bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Host "saved $out"
