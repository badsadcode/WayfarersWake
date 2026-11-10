@echo off
rem Gives the foliage and plant materials light through the leaves (see FoliageLight.py).
set UE=D:\EPIC\UE_5.9
"%UE%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%~dp0..\WayfarersWake.uproject" -run=pythonscript -script="%~dp0FoliageLight.py" -unattended -nosplash -AllowCommandletRendering -stdout
