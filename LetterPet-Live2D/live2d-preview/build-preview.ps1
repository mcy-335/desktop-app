param([string]$SdkPath = 'D:\CubismSdkForNative-5-r.5', [switch]$Test)
$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio C++ Build Tools are required.' }
$vsInfo = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json)
if (-not $vsInfo) { throw 'Install the Desktop development with C++ workload.' }
$vsPath = $vsInfo[0].installationPath
$vsMajor = ([version]$vsInfo[0].installationVersion).Major
$generator = switch ($vsMajor) { 18 { 'Visual Studio 18 2026' } 17 { 'Visual Studio 17 2022' } default { throw 'Use Visual Studio 2022 or 2026.' } }
$cmake = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path -LiteralPath $cmake)) { $cmake = (Get-Command cmake -ErrorAction Stop).Source }
& $cmake -S $PSScriptRoot -B (Join-Path $PSScriptRoot 'build') -G $generator -A x64 "-DCUBISM_SDK=$SdkPath"
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& $cmake --build (Join-Path $PSScriptRoot 'build') --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw 'Preview compilation failed.' }
$exe = Join-Path $PSScriptRoot 'bin\Release\Live2DPetPreview.exe'
if ($Test) {
    $process = Start-Process -FilePath $exe -ArgumentList '--self-test' -WindowStyle Hidden -PassThru -Wait
    if ($process.ExitCode -ne 0) { throw "Runtime self-test failed: $PSScriptRoot\bin\Release\self-test.log" }
}
Write-Host "Preview built: $exe"
