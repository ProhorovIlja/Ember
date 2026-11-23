#ifndef AppPayload
  #error AppPayload must specify the deployed native Qt application folder.
#endif
#ifndef AppOutput
  #define AppOutput "dist"
#endif
#ifndef BuildVersion
  #define BuildVersion "0.1.0-dev"
#endif
#ifndef BuildNumericVersion
  #define BuildNumericVersion "0.1.0.0"
#endif
[Setup]
AppId={{FA314CE1-42F0-4D27-86B6-4375143BD155}
AppName=Ember
AppVersion={#BuildVersion}
AppVerName=Ember {#BuildVersion}
AppPublisher=Ember Contributors
AppPublisherURL=https://github.com/ProhorovIlja/Ember
AppSupportURL=https://github.com/ProhorovIlja/Ember/issues
AppUpdatesURL=https://github.com/ProhorovIlja/Ember/releases
DefaultDirName={code:PreferredInstallDir}
DefaultGroupName=Ember
UninstallDisplayName=Ember {#BuildVersion}
UninstallDisplayIcon={app}\Ember.exe
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
WizardStyle=modern
Compression=lzma2
SolidCompression=yes
OutputDir={#AppOutput}
OutputBaseFilename=Ember-{#BuildVersion}-Windows-x64
DisableProgramGroupPage=yes
VersionInfoVersion={#BuildNumericVersion}
UsePreviousAppDir=yes
LicenseFile={#AppPayload}\LICENSE
[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked
[Files]
; Inno's default solid LZMA2 is useful for compact Qt/application code but
; wastes significant compilation CPU/time on multi-gigabyte quantized model
; weights. Avoid copying models TWICE: root's anchored exclude removes the
; whole models subtree, handled once below with nocompression. Inno's documented
; Excludes semantics interpret a leading backslash relative to source root.
Source: "{#AppPayload}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "*.pdb,*.ilk,*.exp,*.obj,*.lib,qml-smoke-report.txt,\models\*"
; skipifsourcedoesntexist retains the historical text-only CI smoke which
; intentionally has no model folder. Full release preflight elsewhere REQUIRES
; genuine LLM/ASR/TTS assets and verifies installed hashes after install.
Source: "{#AppPayload}\models\*"; DestDir: "{app}\models"; Flags: ignoreversion recursesubdirs createallsubdirs nocompression skipifsourcedoesntexist
[Icons]
Name: "{userprograms}\Ember"; Filename: "{app}\Ember.exe"; WorkingDir: "{app}"
Name: "{userdesktop}\Ember"; Filename: "{app}\Ember.exe"; WorkingDir: "{app}"; Tasks: desktopicon
[UninstallDelete]
Type: filesandordirs; Name: "{app}\cache"
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
