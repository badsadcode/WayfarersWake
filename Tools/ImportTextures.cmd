@echo off
rem Brings Tom's world textures (Art\Textures) into the project (see PrepareTextures.py and ImportTextures.py).
rem Run Tools\CreateAssets.cmd afterwards.
set UE=D:\EPIC\UE_5.9
python "%~dp0PrepareTextures.py" || exit /b 1
"%UE%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%~dp0..\WayfarersWake.uproject" -run=pythonscript -script="%~dp0ImportTextures.py" -unattended -nosplash -AllowCommandletRendering -stdout
