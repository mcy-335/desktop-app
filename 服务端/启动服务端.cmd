@echo off
setlocal
call "%~dp0server\start-local.cmd"
exit /b %errorlevel%
