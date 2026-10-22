@echo off
rem Asocia los archivos .dm2 y .mvd2 con VisorDemos (solo para tu usuario,
rem no necesita permisos de administrador). Despues basta con hacer doble
rem clic en un demo para abrirlo.
setlocal
set "EXE=%~dp0VisorDemos.exe"
if not exist "%EXE%" (
    echo No encuentro VisorDemos.exe junto a este archivo.
    pause
    exit /b 1
)
reg add "HKCU\Software\Classes\.dm2" /ve /d "VisorDemos.Demo" /f >nul
reg add "HKCU\Software\Classes\.mvd2" /ve /d "VisorDemos.Demo" /f >nul
reg add "HKCU\Software\Classes\VisorDemos.Demo" /ve /d "Demo de Quake 2" /f >nul
reg add "HKCU\Software\Classes\VisorDemos.Demo\DefaultIcon" /ve /d "\"%EXE%\",0" /f >nul
reg add "HKCU\Software\Classes\VisorDemos.Demo\shell\open\command" /ve /d "\"%EXE%\" \"%%1\"" /f >nul
echo.
echo Listo: los demos .dm2 y .mvd2 se abren con VisorDemos.
echo Si Windows pregunta con que programa abrirlos, elige VisorDemos.
echo.
pause
