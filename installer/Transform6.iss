; TRANSFORM-6 installer (Inno Setup 6)
; Built automatically by the GitHub workflow. Puts:
;   - the VST3 plugin where FL Studio and Ableton look for it
;   - the standalone app + engine in Program Files
;   - Start menu / desktop shortcuts

#ifndef Stage
  #define Stage "..\stage"
#endif

[Setup]
AppId={{6C1F4E7A-2B8D-4E6B-9A51-5A2B1D7C3E90}
AppName=TRANSFORM-6
AppVersion=1.0.0
AppPublisher=Pathfinder Pressings
DefaultDirName={autopf}\TRANSFORM-6
DisableDirPage=yes
DefaultGroupName=TRANSFORM-6
DisableProgramGroupPage=yes
OutputBaseFilename=TRANSFORM-6-Setup
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
UninstallDisplayName=TRANSFORM-6

[Tasks]
Name: "desktopicon"; Description: "Put a TRANSFORM-6 shortcut on the desktop"; Flags: unchecked

[Files]
; Plugin — the standard VST3 folder (C:\Program Files\Common Files\VST3)
Source: "{#Stage}\plugin\VST3\TRANSFORM-6.vst3\*"; DestDir: "{commoncf64}\VST3\TRANSFORM-6.vst3"; Flags: recursesubdirs createallsubdirs ignoreversion
; Standalone app
Source: "{#Stage}\plugin\Standalone\TRANSFORM-6.exe"; DestDir: "{app}"; Flags: ignoreversion
; Engine (the plugin finds it in Program Files\TRANSFORM-6\engine)
Source: "{#Stage}\engine\*"; DestDir: "{app}\engine"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\TRANSFORM-6"; Filename: "{app}\TRANSFORM-6.exe"
Name: "{group}\Uninstall TRANSFORM-6"; Filename: "{uninstallexe}"
Name: "{autodesktop}\TRANSFORM-6"; Filename: "{app}\TRANSFORM-6.exe"; Tasks: desktopicon

[Dirs]
Name: "{userdocs}\TRANSFORM-6"

[Run]
Filename: "{app}\TRANSFORM-6.exe"; Description: "Open TRANSFORM-6 now"; Flags: nowait postinstall skipifsilent
