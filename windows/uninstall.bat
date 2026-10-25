@echo off
rem Removes catberry.exe. Once no catberry program is left, also removes the folder
rem and its PATH entry. Settings in %APPDATA%\catberry are kept.
setlocal
set "EXE=catberry.exe"
set "DEST=%LOCALAPPDATA%\Programs\catberry"

if exist "%DEST%\%EXE%" del /f /q "%DEST%\%EXE%"

dir /b "%DEST%\*.exe" >nul 2>&1
if errorlevel 1 (
    if exist "%DEST%" rmdir /s /q "%DEST%"
    powershell -NoProfile -ExecutionPolicy Bypass -Command ^
      "$dir = Join-Path $env:LOCALAPPDATA 'Programs\catberry';" ^
      "$path = [Environment]::GetEnvironmentVariable('Path', 'User');" ^
      "$parts = @($path -split ';' | Where-Object { $_ -ne '' -and $_ -ne $dir });" ^
      "[Environment]::SetEnvironmentVariable('Path', ($parts -join ';'), 'User')"
)

echo %EXE% has been uninstalled. Your settings are still in %APPDATA%\catberry
pause
