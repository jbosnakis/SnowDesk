@echo off
rem build.bat        the release build: build\SnowDesk.exe
rem build.bat dev    with the developer switches, for the tools: build\dev\SnowDesk.exe
rem build.bat noauto without the Start with Windows option, for scanner tests: build\noauto\SnowDesk.exe
setlocal
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
where cl >nul 2>&1 || (echo MSVC not found & exit /b 1)
set OUT=build
set DEFS=
if /i "%~1"=="dev" (
    set OUT=build\dev
    set DEFS=/DSNOW_DEV
)
if /i "%~1"=="noauto" (
    set OUT=build\noauto
    set DEFS=/DSNOW_NO_AUTOSTART
)
if not exist %OUT% mkdir %OUT%
if not exist src\app.ico python tools\make_icon.py || exit /b 1
rc /nologo /fo %OUT%\app.res src\app.rc || exit /b 1
rem /DEPENDENTLOADFLAG:0x800 loads the DLLs the exe depends on from System32 only, never from its own folder.
cl /nologo /std:c++17 /utf-8 /O2 /GL /EHsc /W3 /MT /sdl /guard:cf /guard:ehcont /DUNICODE /D_UNICODE /DNOMINMAX %DEFS% ^
   /Fo%OUT%\ /Fd%OUT%\ /Fe:%OUT%\SnowDesk.exe ^
   src\main.cpp src\scene.cpp src\pointer.cpp src\shell.cpp src\panel.cpp src\settings.cpp src\diagnostics.cpp ^
   src\world.cpp src\weather.cpp src\playground.cpp src\cursor.cpp src\render.cpp src\icons.cpp %OUT%\app.res ^
   /link /LTCG /SUBSYSTEM:WINDOWS /MANIFEST:NO /GUARD:CF /guard:ehcont /CETCOMPAT /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA ^
   /DEPENDENTLOADFLAG:0x800 /Brepro ^
   d3d11.lib dxgi.lib dcomp.lib d3dcompiler.lib dwmapi.lib user32.lib gdi32.lib shell32.lib ole32.lib oleaut32.lib advapi32.lib wtsapi32.lib d2d1.lib dwrite.lib shcore.lib
