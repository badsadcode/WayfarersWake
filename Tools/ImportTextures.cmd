@echo off
rem Brings Tom's world texture sets (Art\Textures\<Set>) into the project (see ImportTextures.py).
set UE=D:\EPIC\UE_5.9
"%UE%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%~dp0..\WayfarersWake.uproject" -run=pythonscript -script="%~dp0ImportTextures.py" -unattended -nosplash -AllowCommandletRendering -stdout
