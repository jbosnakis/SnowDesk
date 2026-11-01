# Builds SnowDesk with Visual Studio 2022. A running copy locks the exe: run stop.ps1 first.
# build.ps1          the release exe and the setup: build\SnowDesk.exe, build\setup\SnowDesk-Setup-<version>.exe, SnowDesk-Setup.exe
# build.ps1 release  the release exe only
# build.ps1 dev      with the developer switches, for the tools: build\dev\SnowDesk.exe
# build.ps1 noauto   without the Start with Windows option, for scanner tests: build\noauto\SnowDesk.exe
param([ValidateSet('setup', 'release', 'dev', 'noauto')][string]$Target = 'setup')
$ErrorActionPreference = 'Stop'
Set-Location "$PSScriptRoot\.."

$vcvars = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path -LiteralPath $vcvars)) { throw 'MSVC not found' }
cmd /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2]) }
}

function Check($tool) { if ($LASTEXITCODE -ne 0) { throw "$tool failed with code $LASTEXITCODE" } }

$out = 'build'
$defs = @()
if ($Target -eq 'dev') { $out = 'build\dev'; $defs = @('/DSNOW_DEV') }
if ($Target -eq 'noauto') { $out = 'build\noauto'; $defs = @('/DSNOW_NO_AUTOSTART') }
[void](New-Item -ItemType Directory -Force $out)
if (-not (Test-Path -LiteralPath 'src\app.ico')) { python tools\make_icon.py; Check 'make_icon' }

rc /nologo /fo "$out\app.res" src\app.rc
Check 'rc'
$sources = 'main', 'scene', 'pointer', 'shell', 'panel', 'settings', 'diagnostics', 'world', 'weather', 'playground', 'cursor', 'render', 'icons' |
    ForEach-Object { "src\$_.cpp" }
# /DEPENDENTLOADFLAG:0x800 loads the DLLs the exe depends on from System32 only, never from its own folder.
cl /nologo /std:c++17 /utf-8 /O2 /GL /EHsc /W3 /MT /sdl /guard:cf /guard:ehcont /DUNICODE /D_UNICODE /DNOMINMAX @defs `
    "/Fo$out\" "/Fd$out\" "/Fe:$out\SnowDesk.exe" @sources "$out\app.res" `
    /link /LTCG /SUBSYSTEM:WINDOWS /MANIFEST:NO /GUARD:CF /guard:ehcont /CETCOMPAT /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA `
    /DEPENDENTLOADFLAG:0x800 /Brepro `
    d3d11.lib dxgi.lib dcomp.lib d3dcompiler.lib dwmapi.lib user32.lib gdi32.lib shell32.lib ole32.lib oleaut32.lib advapi32.lib `
    wtsapi32.lib d2d1.lib dwrite.lib shcore.lib
Check 'cl'

if ($Target -eq 'setup') {
    $iscc = "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe", 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe' |
        Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if (-not $iscc) { throw 'Inno Setup 6 not found' }
    & $iscc /Q installer\SnowDesk.iss
    Check 'ISCC'
    # The repository carries the latest setup too, so a plain download of it includes the installer.
    Get-ChildItem build\setup\*.exe | Sort-Object LastWriteTime | Select-Object -Last 1 | Copy-Item -Destination SnowDesk-Setup.exe
}
