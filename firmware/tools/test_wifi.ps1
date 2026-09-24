$ErrorActionPreference = 'Stop'
$PSNativeCommandArgumentPassing = 'Legacy'
$fw = Split-Path -Parent $PSScriptRoot
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'MSVC C++ Build Tools required' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$build = Join-Path $fw '.pio\wifi-tests'
New-Item -ItemType Directory -Force -Path $build | Out-Null
Push-Location $build
try {
    $command = 'call "{0}" >nul && cl /nologo /utf-8 /D_CRT_SECURE_NO_WARNINGS /EHsc /std:c++14 /I"{1}\tests\host\wifi" /I"{1}\src" "{1}\tests\host\wifi_provision.cpp" "{1}\src\wifi_config.cpp" "{1}\src\wifi_policy.cpp" /Fe:wifi_provision.exe && wifi_provision.exe' -f $vcvars, $fw
    & cmd.exe /d /s /c $command
    if ($LASTEXITCODE -ne 0) { throw 'WiFi provision regression failed' }
} finally { Pop-Location }
