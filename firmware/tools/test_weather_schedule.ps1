# Compile the production weather scheduling policy without the board.
$ErrorActionPreference = 'Stop'
$fw = Split-Path -Parent $PSScriptRoot
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'MSVC C++ Build Tools required' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$build = Join-Path $fw '.pio\host-tests'
New-Item -ItemType Directory -Force -Path $build | Out-Null
Push-Location $build
try {
    $command = 'call "{0}" >nul && cl /nologo /utf-8 /EHsc /std:c++14 /I"{1}\src" "{1}\tests\host\weather_schedule_test.cpp" /Fe:weather_schedule_test.exe && weather_schedule_test.exe' -f $vcvars, $fw
    & cmd.exe /d /s /c $command
    if ($LASTEXITCODE -ne 0) { throw 'Weather schedule test failed' }
} finally { Pop-Location }
