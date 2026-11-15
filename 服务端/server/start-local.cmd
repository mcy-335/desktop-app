@echo off
setlocal
cd /d "%~dp0"
where py >nul 2>nul
if errorlevel 1 (
  python letterpet_server.py serve
) else (
  py -3 letterpet_server.py serve
)
exit /b %errorlevel%
