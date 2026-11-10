@echo off
rem Imports Thornwake art (rock textures, tree models) into Content. The source folder is only read.
rem Set WW_THORNWAKE to the Thornwake project folder first (default below).
if "%WW_THORNWAKE%"=="" set WW_THORNWAKE=F:\Codex\ThornwakeNEW
set UE=D:\EPIC\UE_5.9
"%UE%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%~dp0..\WayfarersWake.uproject" -run=pythonscript -script="%~dp0ImportAssets.py" -unattended -nosplash -AllowCommandletRendering -stdout
