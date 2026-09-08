; WallDesk 安装程序脚本 —— Inno Setup 6
;
; 前置步骤：先运行 tools/make_release.py 生成 dist\WallDesk-<版本>-win64\ 目录，
; 再执行：  iscc installer\WallDesk.iss
; 产物：    dist\WallDesk-<版本>-setup.exe
;
; 安装位置为当前用户的 %LOCALAPPDATA%\Programs\WallDesk（PrivilegesRequired=lowest），
; 无需管理员权限，不写系统目录。

#define MyAppName "WallDesk"
#define MyAppVersion "4.3.0"
#define MyAppPublisher "WallDesk Project"
#define MyAppExeName "WallDesk.exe"
#define MyAppSourceDir "..\dist\WallDesk-" + MyAppVersion + "-win64"

[Setup]
; AppId 一经发布不要改动，升级安装靠它识别同一产品
AppId={{4B7E2C6D-8A1F-4E93-B5D2-6F9A1C3D7E50}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\WallDesk
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=WallDesk-{#MyAppVersion}-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
SetupIconFile=..\resources\app.ico
UninstallDisplayIcon={app}\WallDesk.exe
LicenseFile=..\LICENSE
MinVersion=10.0
CloseApplications=force
CloseApplicationsFilter=WallDesk.exe

[Languages]
Name: "chinesesimplified"; MessagesFile: "Languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "autostart"; Description: "开机自动启动（仅当前用户）"; GroupDescription: "其他设置:"; Flags: unchecked

[Files]
Source: "{#MyAppSourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\卸载 {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "WallDesk"; ValueData: """{app}\{#MyAppExeName}"" --minimized"; Tasks: autostart; Flags: uninsdeletevalue

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "启动 {#MyAppName}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; 卸载时不主动删除用户数据（壁纸清单配置、日志、缓存都在 %LOCALAPPDATA% 或程序 data 目录），
; 需要彻底清除时提示用户手动删除，避免误删。
