#ifndef AppPayload
  #error AppPayload must specify the deployed native Qt application folder.
#endif
#ifndef AppOutput
  #define AppOutput "dist"
#endif
[Setup]
AppId={{FA314CE1-42F0-4D27-86B6-4375143BD155}
AppName=Ember
AppVersion=0.1.0-dev
AppVerName=Ember 0.1.0 - Development Foundation
AppPublisher=Ember Contributors
AppPublisherURL=https://github.com/ProhorovIlja/Ember
AppSupportURL=https://github.com/ProhorovIlja/Ember/issues
AppUpdatesURL=https://github.com/ProhorovIlja/Ember/releases
DefaultDirName={code:PreferredInstallDir}
DefaultGroupName=Ember
UninstallDisplayName=Ember (development foundation)
UninstallDisplayIcon={app}\Ember.exe
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
WizardStyle=modern
Compression=lzma2
SolidCompression=yes
OutputDir={#AppOutput}
OutputBaseFilename=Ember-0.1.0-dev-Windows-x64
DisableProgramGroupPage=yes
VersionInfoVersion=0.1.0.0
UsePreviousAppDir=yes
LicenseFile={#AppPayload}\LICENSE
[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked
[Files]
Source: "{#AppPayload}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "*.pdb,*.ilk,*.exp,*.obj,*.lib,qml-smoke-report.txt"
[Icons]
Name: "{userprograms}\Ember"; Filename: "{app}\Ember.exe"; WorkingDir: "{app}"
Name: "{userdesktop}\Ember"; Filename: "{app}\Ember.exe"; WorkingDir: "{app}"; Tasks: desktopicon
[UninstallDelete]
Type: filesandordirs; Name: "{app}\cache"
Type: files; Name: "{app}\data\ember.ini"
Type: files; Name: "{app}\qml-smoke-report.txt"
Type: dirifempty; Name: "{app}\data"
[Code]
function PreferredInstallDir(Param: String): String;
begin
  if DirExists('D:\') then
    Result := 'D:\Games\Ember'
  else
    Result := ExpandConstant('{localappdata}\Programs\Ember');
end;
