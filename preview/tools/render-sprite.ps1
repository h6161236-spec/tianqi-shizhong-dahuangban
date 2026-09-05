# 渲染皮卡丘像素精灵到 PNG,用于设计时自检
# 用法: powershell -File render-sprite.ps1 [输出.png]
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

# ---- 调色板 ----
$pal = [ordered]@{
  '.' = $null
  'Y' = @(248,208,48)    # 皮卡丘黄
  'D' = @(224,166,20)    # 暗黄(脚)
  'K' = @(40,32,16)      # 黑(耳尖/眼/鼻/嘴)
  'R' = @(232,64,40)     # 脸颊红
  'W' = @(250,250,250)   # 眼白
  'B' = @(142,94,44)     # 棕(背纹/尾根)
}

# ---- 身体+头(44×17,朝右,趴姿奔跑;耳朵后掠,眼/颊/嘴在右端) ----
$body = @(
  '.................KKKK...KKK.................'   #  r0 耳尖(后掠)
  '..................KKKYY..KKYY...............'   #  r1
  '...................KKYYY..KYYY..............'   #  r2
  '....................KYYYY..YYYY.............'   #  r3
  '.....................YYYYY..YYYYYYYYYYY.....'   #  r4 耳根并入头顶
  '......................YYYYY...YYYYYYYYYYY...'   #  r5
  '.......................YYYYYYYYYYYYYYYYYY...'   #  r6 头顶
  '........................YYYYYYYYYYYYYYYYY...'   #  r7
  '....................YYYYYYYYYYYYYYKWKYYYYY..'   #  r8 眼(K+白点)
  '.............BBYYYYYYYYYYYYYYYYYYYKKKYYYYY..'   #  r9 背纹1
  '........BBYYYYYYYYYYYYYYYYYYYYYYYYKKKYYYKK..'   # r10 背纹2 + 鼻尖
  '....YYYYBBYYYBBYYYYYYYYYYYYYYYYYYYYYYRRRYY..'   # r11 脸颊
  '...YYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYRRRYY..'   # r12
  '...YYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYKKKY..'   # r13 嘴
  '.....YYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYY...'   # r14
  '........YYYYYYYYYYYYYYYYYYYYYYYYYYYYYYY.....'   # r15
  '............YYYYYYYYYYYYYYYYYYYYYYY.........'   # r16 肚底
)

# ---- 闪电尾巴(15×12,旗面在左上,右下棕块是尾根) ----
$tail = @(
  '..YYYYYYYYYY...'   # t0 旗面
  '..YYYYYYYYYY...'   # t1
  '...YYYYYYYY....'   # t2
  '.....YYYY......'   # t3
  '....YYYY.......'   # t4
  '....YYYY.......'   # t5
  '...YYY.........'   # t6
  '...YYY.........'   # t7
  '...YYY.........'   # t8
  '..YYYY.........'   # t9
  '..YYYYYYY......'   # t10
  '.........BBB...'   # t11 棕色尾根
)

# ---- 腿(44×6,4 帧奔跑循环) ----
$legs = @(
  @(  # f0 伸展:前腿前探、后腿后蹬
    '..............YY...YY......YY...YY..........'
    '.............YY...YY........YY...YY.........'
    '............YY...YY..........YY...YY........'
    '...........YY...YY............YY...YY.......'
    '..........YY...YY.............YY...YY.......'
    '.........DD...DD...............DD...DD......'
  ),
  @(  # f1 收腿腾空(身体抬高 1 精灵px)
    '............YY............YY....YY..........'
    '.............YY............YY...YY..........'
    '.............YY............YY...YY..........'
    '..............YY............YY..YY..........'
    '..............DD............DD..DD..........'
    '............................................'
  ),
  @(  # f2 交叉:前腿后扫、后腿前迈
    '.........YY...YY................YY...YY.....'
    '..........YY...YY...............YY...YY.....'
    '...........YY...YY.............YY...YY......'
    '............YY...YY...........YY...YY.......'
    '.............YY...YY.........YY...YY........'
    '..............DD...DD........DD...DD........'
  ),
  @(  # f3 收腿(另一相位)
    '..........YY...............YY....YY.........'
    '...........YY...............YY...YY.........'
    '...........YY...............YY...YY.........'
    '............YY...............YY..YY.........'
    '............DD...............DD..DD.........'
    '............................................'
  )
)

# ---- 渲染 ----
function Draw-Sprite([System.Drawing.Graphics]$g, [string[]]$rows, [int]$ox, [int]$oy, [int]$scale) {
  for ($y = 0; $y -lt $rows.Count; $y++) {
    $line = $rows[$y]
    for ($x = 0; $x -lt $line.Length; $x++) {
      $c = $line.Substring($x, 1)
      if ($pal.Contains($c) -and $null -ne $pal[$c]) {
        $rgb = $pal[$c]
        $brush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb($rgb[0], $rgb[1], $rgb[2]))
        $g.FillRectangle($brush, $ox + $x * $scale, $oy + $y * $scale, $scale, $scale)
        $brush.Dispose()
      }
    }
  }
}

$SC = 5                      # 每精灵像素放大倍数
# 帧内布局(精灵像素): 身体在(16,10),尾在(10,11),腿在(16,27)
$frameW = 64; $frameH = 36
$BX = 16; $BY = 10
$pad = 10
$partsW = $frameW * $SC
$sheetW = ($frameW * $SC + $pad) * 4 + $partsW + 20
$sheetH = $frameH * $SC + 200
$bmp = New-Object System.Drawing.Bitmap($sheetW, $sheetH)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.Clear([System.Drawing.Color]::FromArgb(90, 120, 170))   # 天空色衬底
$g.InterpolationMode = 'NearestNeighbor'

# 4 个奔跑帧合成: 尾(后) -> 身体 -> 腿
for ($f = 0; $f -lt 4; $f++) {
  $ox = ($frameW * $SC + $pad) * $f
  $oy = $pad
  $bob = if ($f % 2 -eq 1) { -1 } else { 0 }        # 收腿帧=腾空,抬高 1 精灵px
  $tailLift = if ($f % 2 -eq 1) { -1 } else { 0 }   # 尾巴同拍上翘
  # 尾巴:尾根(尾图 col 10, row 11)贴到身体臀部(col 4, row 12)
  Draw-Sprite $g $tail ($ox + ($BX - 6) * $SC) ($oy + ($BY + 1 + $tailLift + $bob) * $SC) $SC
  # 身体
  Draw-Sprite $g $body ($ox + $BX * $SC) ($oy + ($BY + $bob) * $SC) $SC
  # 腿:接在身体正下方(17 行之后)
  Draw-Sprite $g $legs[$f] ($ox + $BX * $SC) ($oy + ($BY + $bob + 17) * $SC) $SC
}

# 零件单独展示(右侧)
$px = ($frameW * $SC + $pad) * 4 + 10
Draw-Sprite $g $body ($px) ($pad) $SC
Draw-Sprite $g $tail ($px) ($pad + 18 * $SC) $SC
for ($f = 0; $f -lt 4; $f++) {
  Draw-Sprite $g $legs[$f] ($px) ($pad + 33 * $SC + $f * 8 * $SC) $SC
}

$g.Dispose()
$out = if ($args.Count -gt 0) { $args[0] } else { 'pikachu-preview.png' }
$out = Join-Path $PSScriptRoot $out
$bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Host "saved: $out"
