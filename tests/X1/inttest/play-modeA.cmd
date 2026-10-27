@echo off
rem OpenMerc int-test: switch base\gamex86.dll + base\player.dll to mode A, then start (arguments go to play.ps1).
rem Mode A = the original 1.07f modules (default), mode B = the OpenMerc modules. The choice stays until the other .cmd runs.
setlocal
set "B=%~dp0base"
copy /y "%B%\gamex86.retail.dll" "%B%\gamex86.dll" >nul || goto err
copy /y "%B%\player.retail.dll" "%B%\player.dll" >nul || goto err
> "%B%\MODE.txt" echo A - original 1.07f gamex86.dll + player.dll (retail)
echo modules: A - original 1.07f gamex86.dll + player.dll (retail)
call "%~dp0play.cmd" %*
exit /b
:err
echo Could not replace base\gamex86.dll / player.dll - is the game still running?
pause
exit /b 1
