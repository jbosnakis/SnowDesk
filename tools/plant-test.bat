@echo off
rem Builds the harmless stand-in DLL for attack-test.ps1: plant-test.bat <output dll>
setlocal
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if not exist build\fuzz mkdir build\fuzz
cl /nologo /LD /MT /DUNICODE /D_UNICODE /Fobuild\fuzz\ tools\plant-test.cpp /Fe:%1 kernel32.lib user32.lib
