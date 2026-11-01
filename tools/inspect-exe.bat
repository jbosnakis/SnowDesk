@echo off
rem Shows the protections, dependencies and sensitive imports of the built exe: inspect-exe.bat [path]
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set EXE=%~1
if "%EXE%"=="" set EXE=%~dp0..\build\SnowDesk.exe
echo === PROTECTIONS
dumpbin /nologo /headers "%EXE%" | findstr /i "Dynamic NX High Control Terminal"
dumpbin /nologo /loadconfig "%EXE%" | findstr /i "Guard CET Cookie"
echo === DLLS
dumpbin /nologo /dependents "%EXE%" | findstr /i ".dll"
echo === SENSITIVE IMPORTS
dumpbin /nologo /imports "%EXE%" | findstr /i "GetAsyncKeyState SetWindowsHook GetKeyboard VirtualAlloc VirtualProtect WriteProcessMemory CreateRemoteThread OpenProcess LoadLibrary GetProcAddress ShellExecute CreateProcess WinExec RegSetValue RegOpenKey URLDownload WSAStartup socket InternetOpen IsDebuggerPresent BitBlt SetWinEventHook TerminateProcess"
