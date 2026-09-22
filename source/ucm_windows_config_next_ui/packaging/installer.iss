#ifndef PortableSource
  #error PortableSource must be defined
#endif
#ifndef InstallerOutput
  #error InstallerOutput must be defined
#endif
#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef OutputBaseName
  #define OutputBaseName "QIZHEN_UCM_CONFIG_STUDIO_NEXT_Setup"
#endif
#ifndef NumericVersion
  #define NumericVersion "0.1.0.0"
#endif
#ifndef BrandIcon
  #error BrandIcon must be defined
#endif

#define AppName "启真传感 UCM Config Studio Next"
#define AppExeName "UcmConfigStudioNext.exe"
#define AppPublisher "启真传感 / Qizhen Sensing"

[Setup]
AppId={{A4E755A8-4D87-4DA5-9C17-B482D0CE996B}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
VersionInfoVersion={#NumericVersion}
VersionInfoCompany={#AppPublisher}
VersionInfoDescription={#AppName} installer
VersionInfoProductName={#AppName}
VersionInfoProductVersion={#NumericVersion}
DefaultDirName={localappdata}\Programs\Qizhen Sensing\UCM Config Studio Next
DefaultGroupName=启真传感
DisableProgramGroupPage=yes
; The bundled Hikrobot GigE/USB3 kernel drivers require elevation to register.
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#InstallerOutput}
OutputBaseFilename={#OutputBaseName}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
UninstallDisplayIcon={app}\{#AppExeName}
SetupLogging=yes
UsePreviousAppDir=yes
SetupIconFile={#BrandIcon}
#ifdef ReleaseSignTool
SignTool={#ReleaseSignTool}
SignedUninstaller=yes
#endif

[Languages]
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加快捷方式："; Flags: unchecked

[Files]
Source: "{#PortableSource}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\启真传感 UCM"; Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"
Name: "{group}\USB 驱动和连接检查"; Filename: "{app}\{#AppExeName}"; Parameters: "--usb-driver-check"; WorkingDir: "{app}"
Name: "{autodesktop}\启真传感 UCM"; Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{cmd}"; Parameters: "/c ""{app}\Install-Hikrobot-MVS-Drivers.cmd"""; Description: "安装 Hikrobot MVS 相机驱动（仅未安装时）"; Flags: postinstall shellexec waituntilterminated unchecked skipifsilent
Filename: "{app}\{#AppExeName}"; Description: "启动启真传感 UCM"; Flags: nowait postinstall skipifsilent
