@echo off
rem Rigs and animates the base bodies (see RigCharacters.py). Writes Art\Characters\*.fbx and *.png. The source file is only read.
set BLENDER=C:\Program Files\Blender Foundation\Blender 5.2\blender.exe
"%BLENDER%" -b "%~dp0..\docs\ConceptArt\ThornwakeBaseChars.blend" --python "%~dp0RigCharacters.py" -- "%~dp0..\Art\Characters" %*
