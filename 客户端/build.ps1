param([switch]$SkipLive2D, [string]$OutputPath = 'LetterPet.exe', [string]$SdkPath = 'D:\CubismSdkForNative-5-r.5')
$ErrorActionPreference = 'Stop'
$projectDir = $PSScriptRoot
$compiler = Get-Command gcc -ErrorAction SilentlyContinue
if (-not $compiler) { throw 'MinGW-w64 GCC is required. Add gcc.exe to PATH.' }
$resourceCompiler = Join-Path (Split-Path $compiler.Source) 'windres.exe'
if (-not (Test-Path -LiteralPath $resourceCompiler)) { throw 'MinGW-w64 windres.exe is required.' }
Push-Location $projectDir
try {
  if (-not $SkipLive2D) { & "$projectDir\live2d-preview\build-preview.ps1" -SdkPath $SdkPath }
  New-Item -ItemType Directory -Path build -Force | Out-Null
  & $resourceCompiler src/letterpet.rc -O coff -o build/letterpet-resource.o
  if ($LASTEXITCODE -ne 0) { throw 'Icon resource compilation failed.' }
  & $compiler.Source -std=c11 -O2 -Wall -Wextra -Wno-missing-field-initializers -Wno-misleading-indentation -municode -mwindows -static -static-libgcc -finput-charset=UTF-8 src/main.c src/account.c src/account_net.c src/rewards.c src/appearance.c src/ui_theme.c src/animation.c src/render.c src/live2d_host.c src/monitor.c src/gpu_monitor.c src/companion_core.c src/companion.c src/focus_apps.c src/companion_tests.c src/resources.c src/study_core.c src/study.c src/study_tests.c src/growth_core.c src/extras.c src/growth_tests.c src/backup_io.c src/desktop_core.c src/desktop.c src/desktop_tests.c src/dialogue_core.c src/dialogue.c src/dialogue_tests.c src/music_core.c src/music.c src/music_tests.c src/timetable_core.c src/timetable.c src/timetable_tests.c build/letterpet-resource.o -o $OutputPath -lcomctl32 -lshell32 -lpsapi -lversion -ladvapi32 -lgdi32 -luser32 -lpdh -lwtsapi32 -lcomdlg32 -lwinmm -lwindowscodecs -lole32 -luuid -lwinhttp -lcrypt32 -lm
  if ($LASTEXITCODE -ne 0) { throw 'C compilation failed. See diagnostics above.' }
  Write-Host "Built: $OutputPath"
} finally { Pop-Location }
