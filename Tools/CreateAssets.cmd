@echo off
rem Creates the code-defined material assets in Content/Materials (see CreateAssets.py).
set UE=D:\EPIC\UE_5.9
"%UE%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%~dp0..\WayfarersWake.uproject" -run=pythonscript -script="%~dp0CreateAssets.py" -unattended -nosplash -AllowCommandletRendering -stdout
