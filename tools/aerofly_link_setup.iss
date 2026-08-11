; Aerofly Link 安装包脚本 — Inno Setup 6
; 编译: ISCC.exe aerofly_link_setup.iss

#define MyAppName "Aerofly Link"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "Aerofly Link Team"
#define MyAppURL "https://github.com/jlgabriel/Aerofly-FS4-Bridge"
#define MyAppExeName "AeroflyLink.exe"

[Setup]
AppId={{AERO-BRIDGE-DIST32-2026-AF4F-5D7E8F9A0B1C}}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
OutputDir=..\dist_installer
OutputBaseFilename=AeroflyLink_Setup_v1.0.0
Compression=lzma2/max
SolidCompression=yes
WizardStyle=classic
DisableProgramGroupPage=yes
PrivilegesRequiredOverridesAllowed=dialog
UsePreviousAppDir=yes
UninstallDisplayName={#MyAppName} {#MyAppVersion}
UninstallDisplayIcon={app}\{#MyAppExeName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "..\dist_new34\AeroflyLink\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
; 游戏 Bridge DLL — 备份一份到应用目录，方便手动复制
Source: "..\installer\data\AeroflyBridge.dll"; DestDir: "{app}"; Flags: ignoreversion
; 部署 DLL 到 Aerofly FS 4 的 external_dll 目录（游戏加载所需，卸载时保留）
Source: "..\installer\data\AeroflyBridge.dll"; DestDir: "{userdocs}\Aerofly FS 4\external_dll"; Flags: ignoreversion uninsneveruninstall

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Comment: "Aerofly FS 4 第三方联机平台"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon; Comment: "Aerofly FS 4 第三方联机平台"

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent unchecked
