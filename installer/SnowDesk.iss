; The Windows setup for SnowDesk. Build the release exe first, then compile this file with Inno Setup 6:
;   ISCC installer\SnowDesk.iss
; Output: build\setup\SnowDesk-Setup-<version>.exe
#define Exe "..\build\SnowDesk.exe"
#define Version GetStringFileInfo(Exe, "FileVersion")

[Setup]
AppId={{CCABC85A-2EC9-4E8B-90AF-40CD2674190D}
AppName=SnowDesk
AppVersion={#Version}
AppVerName=SnowDesk {#Version}
AppPublisher=Perforks
VersionInfoCompany=Perforks
VersionInfoDescription=SnowDesk Setup
VersionInfoVersion={#Version}
; Per user: no administrator rights, and the program lands in %LOCALAPPDATA%\Programs.
PrivilegesRequired=lowest
DefaultDirName={autopf}\SnowDesk
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
SetupIconFile=..\src\app.ico
UninstallDisplayIcon={app}\SnowDesk.exe
UninstallDisplayName=SnowDesk
WizardStyle=modern
Compression=lzma2
SolidCompression=yes
OutputDir=..\build\setup
OutputBaseFilename=SnowDesk-Setup-{#Version}

[Tasks]
Name: startup; Description: "Start SnowDesk when I sign in to Windows"
Name: desktop; Description: "Add a desktop shortcut"; Flags: unchecked

[Files]
Source: "{#Exe}"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\SnowDesk"; Filename: "{app}\SnowDesk.exe"
Name: "{autodesktop}\SnowDesk"; Filename: "{app}\SnowDesk.exe"; Tasks: desktop
; The same shortcut the panel's Start with Windows option makes, so the two always agree.
Name: "{userstartup}\SnowDesk"; Filename: "{app}\SnowDesk.exe"; Tasks: startup

[UninstallDelete]
Type: files; Name: "{userstartup}\SnowDesk.lnk"

[Run]
Filename: "{app}\SnowDesk.exe"; Description: "Start SnowDesk now"; Flags: nowait postinstall skipifsilent

[Code]
const
  WM_CLOSE = 16;

// A running copy locks the exe, and a tray app has no window for the person to close.
procedure CloseSnowDesk;
var
  Tries: Integer;
begin
  PostMessage(FindWindowByClassName('SnowDeskHost'), WM_CLOSE, 0, 0);
  Tries := 0;
  while (FindWindowByClassName('SnowDeskHost') <> 0) and (Tries < 50) do begin
    Sleep(100);
    Tries := Tries + 1;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  CloseSnowDesk;
  Result := '';
end;

procedure CurUninstallStepChanged(Step: TUninstallStep);
begin
  if Step = usUninstall then CloseSnowDesk;
end;
