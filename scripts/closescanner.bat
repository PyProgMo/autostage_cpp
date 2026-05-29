@echo off

taskkill /IM RasterScan64.exe /F

taskkill /IM StageServer.exe /F

taskkill /IM ConsoleApp.exe /F

echo Fertig.
pause