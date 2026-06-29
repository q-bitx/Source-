[Setup]
AppName=QBitX Wallet
AppVersion=0.3.2
DefaultDirName={autopf}\QBitX Wallet
DefaultGroupName=QBitX Wallet
OutputDir=dist
OutputBaseFilename=QBitX-Wallet-Setup-0.3.2
Compression=lzma
SolidCompression=yes
WizardStyle=modern
SetupIconFile=..\assets\qbitx_wallet_icon.ico
UninstallDisplayIcon={app}\qbitx-gui.exe
PrivilegesRequired=lowest

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional icons:"

[Files]
Source: "..\dist\bundle\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\assets\qbitx_wallet_icon.ico"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\QBitX Wallet"; Filename: "{app}\qbitx-gui.exe"; WorkingDir: "{app}"; IconFilename: "{app}\qbitx-gui.exe"
Name: "{autodesktop}\QBitX Wallet"; Filename: "{app}\qbitx-gui.exe"; WorkingDir: "{app}"; IconFilename: "{app}\qbitx-gui.exe"; Tasks: desktopicon

[UninstallRun]
Filename: "taskkill"; Parameters: "/F /IM qbitx-gui.exe"; Flags: runhidden; RunOnceId: "KillQBitXGui"
Filename: "taskkill"; Parameters: "/F /IM qbitx.exe"; Flags: runhidden; RunOnceId: "KillQBitXNode"

[UninstallDelete]
Type: filesandordirs; Name: "{app}"

[Run]
Filename: "{app}\qbitx-gui.exe"; Description: "Launch QBitX Wallet"; Flags: nowait postinstall skipifsilent
