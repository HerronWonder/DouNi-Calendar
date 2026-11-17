#ifndef AppVersion
  #define AppVersion "1.0.1"
#endif
#ifndef InputDirectory
  #define InputDirectory "..\dist\豆泥日历"
#endif
#ifndef ReleaseDirectory
  #define ReleaseDirectory "..\release"
#endif

[Setup]
AppId={{9F74A62B-6F41-4BDF-AB72-B0F3CD9E7A1B}
AppName=豆泥日历
AppVersion={#AppVersion}
AppVerName=豆泥日历 {#AppVersion}
AppPublisher=DouNi Calendar
DefaultDirName={localappdata}\Programs\DouNi Calendar
DefaultGroupName=豆泥日历
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
DisableDirPage=no
DisableProgramGroupPage=yes
CloseApplications=yes
CloseApplicationsFilter=DouNiCalendar.exe
RestartApplications=no
UninstallDisplayName=豆泥日历
UninstallDisplayIcon={app}\DouNiCalendar.exe
SetupIconFile=..\src\windows\app.ico
OutputDir={#ReleaseDirectory}
OutputBaseFilename=DouNiCalendar-{#AppVersion}-Windows-x64-Setup
VersionInfoVersion={#AppVersion}.0
VersionInfoDescription=豆泥日历安装程序
VersionInfoProductName=DouNi Calendar
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ShowLanguageDialog=no

[Languages]
Name: "chinesesimp"; MessagesFile: "installer\ChineseSimplified.isl"

[Tasks]
Name: "autostart"; Description: "开机启动"; Flags: unchecked

[Files]
Source: "{#InputDirectory}\DouNiCalendar.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\docs\使用说明-Windows.txt"; DestDir: "{app}"; DestName: "使用说明.txt"; Flags: ignoreversion

[Icons]
Name: "{group}\豆泥日历"; Filename: "{app}\DouNiCalendar.exe"; WorkingDir: "{app}"
Name: "{group}\卸载豆泥日历"; Filename: "{uninstallexe}"
Name: "{userstartup}\DouNi Calendar"; Filename: "{app}\DouNiCalendar.exe"; WorkingDir: "{app}"; Tasks: autostart

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: none; ValueName: "DouNiCalendar"; Flags: deletevalue
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: none; ValueName: "DesktopNoteCalendar"; Flags: deletevalue

[InstallDelete]
Type: files; Name: "{userstartup}\DouNi Calendar.lnk"; Tasks: not autostart

[UninstallDelete]
Type: files; Name: "{userstartup}\DouNi Calendar.lnk"

[Run]
Filename: "{app}\DouNiCalendar.exe"; Description: "运行豆泥日历"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent
