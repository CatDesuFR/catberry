@echo off
rem Installs catberry-ac.exe for the current user into %LOCALAPPDATA%\Programs\catberry,
rem the folder shared by all catberry programs, and adds it to the user PATH.
rem No administrator rights needed.
setlocal
set "EXE=catberry-ac.exe"
set "DEST=%LOCALAPPDATA%\Programs\catberry"

if not exist "%~dp0%EXE%" (
    echo %EXE% must be in the same folder as install.bat.
    pause
    exit /b 1
)

if not exist "%DEST%" mkdir "%DEST%"
copy /y "%~dp0%EXE%" "%DEST%\%EXE%" >nul
if errorlevel 1 (
    echo Could not copy %EXE%. Close any open catberry windows and try again.
    pause
    exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$dir = Join-Path $env:LOCALAPPDATA 'Programs\catberry';" ^
  "$path = [Environment]::GetEnvironmentVariable('Path', 'User');" ^
  "$parts = @($path -split ';' | Where-Object { $_ -ne '' });" ^
  "if ($parts -notcontains $dir) { [Environment]::SetEnvironmentVariable('Path', (($parts + $dir) -join ';'), 'User'); 'Added catberry to your PATH.' } else { 'catberry is already on your PATH.' }"

echo.
echo Installed %EXE% to %DEST%
echo Open a NEW terminal (cmd or PowerShell) and run:  catberry ac gui
echo (This needs the catberry launcher too: install catberry-windows.zip if you have not.)
echo.
pause
