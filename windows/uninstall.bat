@echo off
rem Removes catberry-scan.exe from %LOCALAPPDATA%\Programs\catberry.
rem Your results_scan_*.txt reports are kept.
setlocal
set "EXE=%LOCALAPPDATA%\Programs\catberry\catberry-scan.exe"
if exist "%EXE%" del /f "%EXE%"
echo Removed catberry-scan.exe
pause
