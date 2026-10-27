@echo off
rem OpenMerc int-test: start with the modules currently in base\ (see base\MODE.txt). Arguments go to play.ps1.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0play.ps1" %*
