@echo off
rem Removes catberry-cbmm.exe from %LOCALAPPDATA%\Programs\catberry.
rem Run "catberry cbmm unload" first to take the mod out of Geometry Dash.
setlocal
set "EXE=%LOCALAPPDATA%\Programs\catberry\catberry-cbmm.exe"
if exist "%EXE%" del /f "%EXE%"
echo Removed catberry-cbmm.exe
pause
