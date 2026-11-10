@echo off
rem Builds the headless world-generation parity test into Intermediate\WorldGenTest (no engine needed).
rem Usage: Tools\WorldGenTest\build.cmd   then   Intermediate\WorldGenTest\WorldGenTest.exe . [--quick] [--rev=N] [--sizes] [--bench] [--types] [--ocean] [--rules]
setlocal
set ROOT=%~dp0..\..
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do set VS=%%i
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if not exist "%ROOT%\Intermediate\WorldGenTest" mkdir "%ROOT%\Intermediate\WorldGenTest"
cd /d "%ROOT%\Intermediate\WorldGenTest"
cl /nologo /std:c++20 /O2 /fp:precise /EHsc /W3 /MP /MD /I "%ROOT%\Source\WayfarersWake" "%~dp0*.cpp" "%ROOT%\Source\WayfarersWake\WorldGen\*.cpp" "%ROOT%\Source\WayfarersWake\Rules\*.cpp" /Fe:WorldGenTest.exe
exit /b %errorlevel%
