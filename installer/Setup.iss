; PretClient custom setup (Inno Setup 6). No MSIX.
; Build from repo root: ISCC.exe /DMyAppVersion=0.0.1-dev installer\Setup.iss
#ifndef MyAppVersion
  #define MyAppVersion "0.0.1-dev"
#endif

[Setup]
AppId={{8A81D479-8604-44BF-8C72-38FB118FFEBE}
AppName=PretClient
AppVersion={#MyAppVersion}
AppPublisher=PretClient
DefaultDirName={localappdata}\Programs\PretClient
DefaultGroupName=PretClient
OutputDir=Output
OutputBaseFilename=PretClient-Setup
Compression=lzma2
SolidCompression=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
WizardStyle=modern

[Files]
Source: "..\x64\Release\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "*.pdb,*.ilk,*.exp,*.lib,*.iobj,*.ipdb"

[Icons]
Name: "{autoprograms}\PretClient"; Filename: "{app}\PretClient.exe"
Name: "{autodesktop}\PretClient"; Filename: "{app}\PretClient.exe"; Tasks: desktopicon

[Tasks]
Name: desktopicon; Description: "Create a desktop shortcut"; Flags: unchecked

[Run]
Filename: "{app}\PretClient.exe"; Description: "Launch PretClient"; Flags: nowait postinstall skipifsilent
