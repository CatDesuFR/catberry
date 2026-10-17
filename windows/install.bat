@echo off
rem Installs catberry for the current user: copies catberry.exe into
rem %LOCALAPPDATA%\Programs\catberry and adds that folder to the user PATH.
rem No administrator rights needed.
setlocal
set "DEST=%LOCALAPPDATA%\Programs\catberry"

if not exist "%~dp0catberry.exe" (
    echo catberry.exe must be in the same folder as install.bat.
    pause
    exit /b 1
)

rem An older copy that is running would block the copy below.
if exist "%DEST%\catberry.exe" "%DEST%\catberry.exe" -configunload >nul 2>&1

if not exist "%DEST%" mkdir "%DEST%"
copy /y "%~dp0catberry.exe" "%DEST%\catberry.exe" >nul
if errorlevel 1 (
    echo Could not copy catberry.exe. Close any open catberry windows and try again.
    pause
    exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$dir = Join-Path $env:LOCALAPPDATA 'Programs\catberry';" ^
  "$path = [Environment]::GetEnvironmentVariable('Path', 'User');" ^
  "$parts = @($path -split ';' | Where-Object { $_ -ne '' });" ^
  "if ($parts -notcontains $dir) { [Environment]::SetEnvironmentVariable('Path', (($parts + $dir) -join ';'), 'User'); 'Added catberry to your PATH.' } else { 'catberry is already on your PATH.' }"

echo.
echo Installed to %DEST%
echo Open a NEW terminal (cmd or PowerShell) and run:  catberry ac gui
echo.
pause
