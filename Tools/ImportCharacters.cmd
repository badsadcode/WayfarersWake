@echo off
rem Brings the rigged bodies into the project (see ImportCharacters.py). Run Tools\RigCharacters.cmd first.
set UE=D:\EPIC\UE_5.9
"%UE%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%~dp0..\WayfarersWake.uproject" -run=pythonscript -script="%~dp0ImportCharacters.py" -unattended -nosplash -AllowCommandletRendering -stdout
