param([switch]$SkipLive2D, [string]$OutputPath = 'LetterPet.exe', [string]$SdkPath = 'D:\CubismSdkForNative-5-r.5')
$ErrorActionPreference = 'Stop'
$projectDir = $PSScriptRoot
$compiler = Get-Command gcc -ErrorAction SilentlyContinue
if (-not $compiler) { throw 'MinGW-w64 GCC is required. Add gcc.exe to PATH.' }
Push-Location $projectDir
try {
  if (-not $SkipLive2D) { & "$projectDir\live2d-preview\build-preview.ps1" -SdkPath $SdkPath }
  & $compiler.Source -std=c11 -O2 -Wall -Wextra -Wno-missing-field-initializers -Wno-misleading-indentation -municode -mwindows -static -static-libgcc -finput-charset=UTF-8 src/main.c src/animation.c src/render.c src/live2d_host.c src/monitor.c src/gpu_monitor.c -o $OutputPath -lcomctl32 -lshell32 -lpsapi -lversion -ladvapi32 -lgdi32 -luser32 -lpdh -lm
  if ($LASTEXITCODE -ne 0) { throw 'C compilation failed. See diagnostics above.' }
  Write-Host "Built: $OutputPath"
} finally { Pop-Location }
