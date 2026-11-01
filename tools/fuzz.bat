@echo off
rem Builds the fuzzer with AddressSanitizer and checked containers, then runs it: fuzz.bat [seconds] [first seed]
setlocal
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if not exist build\fuzz mkdir build\fuzz
cl /nologo /std:c++17 /utf-8 /Od /Zi /EHsc /W3 /MTd /fsanitize=address /D_ITERATOR_DEBUG_LEVEL=2 /DNOMINMAX ^
   /Fobuild\fuzz\ /Fdbuild\fuzz\ /Fe:build\fuzz\fuzz.exe ^
   tools\fuzz.cpp src\world.cpp src\weather.cpp src\playground.cpp src\cursor.cpp || exit /b 1
build\fuzz\fuzz.exe %1 %2
