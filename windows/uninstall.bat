@echo off
rem Removes catberry and its PATH entry. Settings, macros and configs in
rem %APPDATA%\catberry are kept; delete that folder too to remove them.
setlocal
set "DEST=%LOCALAPPDATA%\Programs\catberry"

if exist "%DEST%\catberry.exe" "%DEST%\catberry.exe" -configunload >nul 2>&1
if exist "%DEST%" rmdir /s /q "%DEST%"

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$dir = Join-Path $env:LOCALAPPDATA 'Programs\catberry';" ^
  "$path = [Environment]::GetEnvironmentVariable('Path', 'User');" ^
  "$parts = @($path -split ';' | Where-Object { $_ -ne '' -and $_ -ne $dir });" ^
  "[Environment]::SetEnvironmentVariable('Path', ($parts -join ';'), 'User')"

echo catberry has been uninstalled. Your settings are still in %APPDATA%\catberry
pause
