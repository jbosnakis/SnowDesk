@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if not exist build\test mkdir build\test
cl /nologo /std:c++17 /utf-8 /O2 /EHsc /W3 /MT /DNOMINMAX /Fobuild\test\ /Fe:build\test\playtest.exe ^
   tools\playtest.cpp src\world.cpp src\weather.cpp src\playground.cpp src\cursor.cpp || exit /b 1
build\test\playtest.exe
