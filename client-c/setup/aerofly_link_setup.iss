; Aerofly Link 安装包脚本 — Inno Setup 6（C 版单文件分发，P4）
; 编译: ISCC.exe client-c\setup\aerofly_link_setup.iss
; 前置: client-c\build.cmd 已构建；AeroflyBridge.dll 放在本目录
;       （gitignored 载荷，来源见 docs/RELEASE.md）

#define MyAppName "Aerofly Link"
#define MyAppPublisher "Aerofly Link Team"
#define MyAppURL "https://github.com/Flying-han/aerofly-link"
#define MyAppExeName "AeroflyLink.exe"
; 版本单一来源：client-c\VERSION（与构建注入的 AEROFLYLINK_VERSION 一致）
#define VerHandle FileOpen(SourcePath + "\..\VERSION")
#define MyAppVersion Trim(FileRead(VerHandle))
#expr FileClose(VerHandle)

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
OutputDir=..\..\dist_installer
OutputBaseFilename=AeroflyLink_Setup_v{#MyAppVersion}
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
; C 版主程序（静态 CRT 单文件）+ comctl v6 / DPI 外挂清单
Source: "..\build\aeroflylink.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\aeroflylink.exe.manifest"; DestDir: "{app}"; Flags: ignoreversion
; 游戏 Bridge DLL — 备份一份到应用目录，方便手动复制
Source: "AeroflyBridge.dll"; DestDir: "{app}"; Flags: ignoreversion
; 部署 DLL 到 Aerofly FS 4 的 external_dll 目录（游戏加载所需，卸载时保留）
Source: "AeroflyBridge.dll"; DestDir: "{userdocs}\Aerofly FS 4\external_dll"; Flags: ignoreversion uninsneveruninstall

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Comment: "Aerofly FS 4 第三方联机平台"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon; Comment: "Aerofly FS 4 第三方联机平台"

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent unchecked
