$ErrorActionPreference = 'Stop'
$projectDir = $PSScriptRoot
$compiler = Get-Command gcc -ErrorAction SilentlyContinue
if (-not $compiler) { throw 'MinGW-w64 GCC is required. Add gcc.exe to PATH.' }
Push-Location $projectDir
try {
  & $compiler.Source -std=c11 -O2 -Wall -Wextra -Wno-missing-field-initializers -Wno-misleading-indentation -municode -mwindows -static -static-libgcc -finput-charset=UTF-8 src/main.c src/animation.c src/render.c src/monitor.c -o LetterPet.exe -lcomctl32 -lshell32 -lpsapi -lversion -ladvapi32 -lgdi32 -luser32 -lm
  if ($LASTEXITCODE -ne 0) { throw 'C compilation failed. See diagnostics above.' }
  Write-Host "Built: $projectDir\LetterPet.exe"
} finally { Pop-Location }
