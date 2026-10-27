@echo off
rem openMERC launcher. Needs a licensed Soldier of Fortune 1.07f installation (GOG, or retail + 1.07f patch).
rem The game directory is read from game_path.txt next to this file (asked for on the first start).
rem OpenMerc never writes into the game directory: settings, saves and logs go to .\user\.
rem Usage: openMERC.cmd [extra engine arguments, e.g. +map kos1]
setlocal
cd /d "%~dp0"
set "SOF_DIR="
if exist game_path.txt set /p SOF_DIR=<game_path.txt
if not "%SOF_DIR%"=="" if exist "%SOF_DIR%\base\pak0.pak" goto run
echo Path to your Soldier of Fortune installation (the folder that contains base\pak0.pak),
echo for example: C:\GOG Games\Soldier of Fortune
set /p SOF_DIR=Game folder:
if not exist "%SOF_DIR%\base\pak0.pak" (
    echo base\pak0.pak not found under "%SOF_DIR%".
    pause
    exit /b 1
)
> game_path.txt echo %SOF_DIR%
:run
set "SOF_DIR=%SOF_DIR:\=/%"
set "HERE=%~dp0"
set "HERE=%HERE:~0,-1%"
set "HERE=%HERE:\=/%"
openMerc.exe -portable -basedir "%HERE%" -cddir "%SOF_DIR%" +set logfile 3 %*
