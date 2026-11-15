@echo off
setlocal
set "PET_OUTPUT=LetterPet.exe"
if not "%~1"=="" set "PET_OUTPUT=%~1"
cd /d "%~dp0"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo Visual Studio C++ tools are required.
  exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "PET_VS=%%i"
if not defined PET_VS exit /b 1
call "%PET_VS%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b 1
set "PET_SDK=%ProgramFiles(x86)%\Windows Kits\10"
for /f "delims=" %%i in ('dir /b /ad /o-n "%PET_SDK%\Include\10.*"') do if not defined PET_SDKVER set "PET_SDKVER=%%i"
for /f "delims=" %%i in ('dir /b /ad /o-n "%PET_VS%\VC\Tools\MSVC\*"') do if not defined PET_MSCVER set "PET_MSCVER=%%i"
set "INCLUDE=%PET_VS%\VC\Tools\MSVC\%PET_MSCVER%\include;%PET_SDK%\Include\%PET_SDKVER%\ucrt;%PET_SDK%\Include\%PET_SDKVER%\shared;%PET_SDK%\Include\%PET_SDKVER%\um"
set "LIB=%PET_VS%\VC\Tools\MSVC\%PET_MSCVER%\lib\x64;%PET_SDK%\Lib\%PET_SDKVER%\ucrt\x64;%PET_SDK%\Lib\%PET_SDKVER%\um\x64"
if not exist build mkdir build
"%PET_SDK%\bin\%PET_SDKVER%\x64\rc.exe" /nologo /fo build\letterpet.res src\letterpet.rc
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c11 /utf-8 /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /Fo:build\ /Fe:"%PET_OUTPUT%" src\main.c src\account.c src\account_net.c src\rewards.c src\appearance.c src\ui_theme.c src\animation.c src\render.c src\live2d_host.c src\monitor.c src\gpu_monitor.c src\companion_core.c src\companion.c src\focus_apps.c src\companion_tests.c src\resources.c src\study_core.c src\study.c src\study_tests.c src\growth_core.c src\extras.c src\growth_tests.c src\backup_io.c src\desktop_core.c src\desktop.c src\desktop_tests.c src\dialogue_core.c src\dialogue.c src\dialogue_tests.c src\music_core.c src\music.c src\music_tests.c src\timetable_core.c src\timetable.c src\timetable_tests.c build\letterpet.res /link /SUBSYSTEM:WINDOWS comctl32.lib shell32.lib psapi.lib version.lib advapi32.lib gdi32.lib user32.lib pdh.lib wtsapi32.lib comdlg32.lib winmm.lib windowscodecs.lib ole32.lib uuid.lib winhttp.lib crypt32.lib
exit /b %errorlevel%
