$ErrorActionPreference = 'Stop'
$buildPath = Join-Path ([IO.Path]::GetTempPath()) ('ditboard-tests-' + [guid]::NewGuid())
New-Item -ItemType Directory -Path $buildPath | Out-Null
try {
    $sketchPath = Join-Path (Split-Path $PSScriptRoot -Parent) 'ditboard_rp2040zero.ino'
    $sketch = [IO.File]::ReadAllText($sketchPath)
    $sketch = [regex]::Replace($sketch, '(?m)^#include[^\r\n]*\r?\n', '')
    [IO.File]::WriteAllText((Join-Path $buildPath 'firmware-under-test.inc'), $sketch)
    $testPath = Join-Path $PSScriptRoot 'firmware_tests.cpp'
    $executable = Join-Path $buildPath 'firmware_tests.exe'

    if (Get-Command cl.exe -ErrorAction SilentlyContinue) {
        & cl.exe /nologo /EHsc /std:c++17 "/I$buildPath" "/Fe:$executable" "/Fo:$buildPath\" $testPath
    } elseif (Get-Command g++.exe -ErrorAction SilentlyContinue) {
        & g++.exe -std=c++17 -pthread "-I$buildPath" $testPath -o $executable
    } else {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (!(Test-Path $vswhere)) { throw 'Install a C++ compiler or run from a Visual Studio developer shell.' }
        $visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if (!$visualStudio) { throw 'Visual Studio C++ build tools were not found.' }
        $setup = Join-Path $visualStudio 'VC\Auxiliary\Build\vcvars64.bat'
        & $env:ComSpec /c "call `"$setup`" >nul && cl /nologo /EHsc /std:c++17 /I`"$buildPath`" /Fe:`"$executable`" /Fo:`"$buildPath\firmware_tests.obj`" `"$testPath`""
    }
    if ($LASTEXITCODE -ne 0) { throw 'Host test compilation failed.' }
    & $executable
    if ($LASTEXITCODE -ne 0) { throw 'Firmware regression tests failed.' }
} finally {
    Remove-Item -LiteralPath $buildPath -Recurse -Force
}
