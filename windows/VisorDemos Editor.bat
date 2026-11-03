@echo off
rem Abre VisorDemos en modo editor: botones, barra de tiempo con clic y
rem clic derecho mantenido para mover la camara. Puedes arrastrar un demo
rem sobre este archivo para abrirlo directo. Tab cambia de modo dentro del
rem visor, y el visor recuerda el ultimo modo que usaste.
start "" "%~dp0VisorDemos.exe" %* +set visor_editor 1
