$ErrorActionPreference = 'Stop'
$PSNativeCommandArgumentPassing = 'Legacy'
$fw = Split-Path -Parent $PSScriptRoot
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'MSVC C++ Build Tools required' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$build = Join-Path $fw '.pio\scene-tests'
New-Item -ItemType Directory -Force -Path $build | Out-Null
Push-Location $build
try {
    $command = 'call "{0}" >nul && cl /nologo /utf-8 /D_CRT_SECURE_NO_WARNINGS /EHsc /std:c++14 /I"{1}\tests\host\scene" /I"{1}\tests\host" /I"{1}\src" "{1}\tests\host\scene_pixel.cpp" "{1}\src\scene.cpp" /Fe:scene_pixel.exe && scene_pixel.exe' -f $vcvars, $fw
    & cmd.exe /d /s /c $command
    if ($LASTEXITCODE -ne 0) { throw 'Scene pixel regression failed' }
} finally { Pop-Location }
