; QArchiver (小Q解压) Windows installer (Inno Setup 6.3+).
; 小Q解压 Windows 安装器（Inno Setup 6.3+）。
;
; Build from the staged portable package:
; 从已部署好的便携包目录构建：
;
;   ISCC.exe QArchiver.iss /DAppVersion=2.4.2 ^
;       /DSourceDir=C:\path\to\staged ^
;       /DOutputDir=C:\path\to\dist /DArch=x64
;
; SourceDir must contain QArchiver.exe, 7zip\ and the deployed Qt runtime.
; SourceDir 中必须包含 QArchiver.exe、7zip\ 与部署后的 Qt 运行库。
;
; The Explorer menu written here mirrors install-shell-menu.ps1.
; Keep both definitions in sync when adding a format.
; 此处写入的资源管理器菜单与 install-shell-menu.ps1 一致；
; 新增格式时请同步修改两处。

#ifndef AppVersion
  #define AppVersion "2.4.2"
#endif
#ifndef AppVersionNumeric
  #define AppVersionNumeric "2.4.2.0"
#endif

; Versioned shell-extension filename lets an in-place upgrade install a new
; DLL while Explorer still has the old one loaded.
; 带版本号的扩展文件名使覆盖升级可写入新 DLL，不受 Explorer 旧模块占用影响。
#define ShellDll "QArchiverShell-" + AppVersion + ".dll"
#ifndef SourceDir
  #define SourceDir "..\..\build-windows\package"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\dist"
#endif
#ifndef Arch
  #define Arch "x64"
#endif
#ifndef ArchAllowed
  #define ArchAllowed "x64compatible"
#endif

#define AppExeName "QArchiver.exe"
#define AppDisplayName "QArchiver (小Q解压)"
#define AppPublisher "QArchiver"
#define AppUrl "https://github.com/yincolor/QArchiver"

[Setup]
AppId={{7D2C4A63-8B1E-4C9F-9A57-2E6B0D4F1C38}
AppName={#AppDisplayName}
AppVersion={#AppVersion}
AppVerName={#AppDisplayName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppUrl}
AppSupportURL={#AppUrl}
AppUpdatesURL={#AppUrl}
VersionInfoVersion={#AppVersionNumeric}
VersionInfoDescription={#AppDisplayName} installer
DefaultDirName={autopf}\QArchiver
DefaultGroupName=QArchiver
DisableProgramGroupPage=yes
PrivilegesRequired=admin
OutputDir={#OutputDir}
OutputBaseFilename=QArchiver-{#AppVersion}-windows-{#Arch}-setup
SetupIconFile=..\..\resources\app-icons\qarchiver.ico
UninstallDisplayIcon={app}\{#AppExeName}
UninstallDisplayName={#AppDisplayName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ShowLanguageDialog=yes
LanguageDetectionMethod=locale
CloseApplications=yes
RestartApplications=no
ArchitecturesAllowed={#ArchAllowed}
ArchitecturesInstallIn64BitMode={#ArchAllowed}

[Languages]
; Chinese first: when LanguageDetectionMethod cannot decide, this is the
; preselected language on Chinese systems.
; 中文放在首位：无法自动判断时中文系统默认选中中文。
Name: "chinesesimplified"; MessagesFile: "ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "shellmenu"; Description: "添加资源管理器右键菜单（打开 / 解压 / 添加到 ZIP / 压缩到…）"; GroupDescription: "集成选项："
Name: "associate"; Description: "关联压缩包格式（.zip/.7z/.rar/.tar/.gz 等，默认选中）"; GroupDescription: "集成选项："

[Files]
; The whole portable payload: exe, 7zip\, Qt runtime and plugins, helper scripts.
; 完整的便携包内容：exe、7zip\、Qt 运行库与插件、辅助脚本。
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\QArchiver"; Filename: "{app}\{#AppExeName}"
Name: "{group}\卸载 QArchiver"; Filename: "{uninstallexe}"
Name: "{autodesktop}\QArchiver"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,QArchiver}"; Flags: nowait postinstall skipifsilent
Filename: "{app}\{#AppExeName}"; Parameters: "--register-associations"; Flags: runhidden waituntilterminated runasoriginaluser; Tasks: associate; StatusMsg: "正在注册文件关联..."
Filename: "{sys}\regsvr32.exe"; Parameters: "/s ""{app}\{#ShellDll}"""; Flags: runhidden waituntilterminated; Tasks: shellmenu; StatusMsg: "正在注册资源管理器右键菜单..."

[UninstallRun]
Filename: "{app}\{#AppExeName}"; Parameters: "--unregister-associations"; Flags: runhidden waituntilterminated; RunOnceId: "QArchiverUnregisterAssociations"
Filename: "{sys}\regsvr32.exe"; Parameters: "/u /s ""{app}\{#ShellDll}"""; Flags: runhidden waituntilterminated; RunOnceId: "QArchiverUnregisterShell"

[UninstallDelete]
; Remove all shell-extension copies, including older versioned DLLs that were
; left behind because Explorer had them mapped during an upgrade.
; 删除全部右键扩展副本，包括升级时因 Explorer 占用而遗留的旧版本 DLL。
Type: files; Name: "{app}\QArchiverShell*.dll"

[Code]
const
  ShellClsid = '{5F3B9C2A-7E4D-4A1B-9C6D-2B8E1F4A7C30}';

function ArchiveExtensions: TArrayOfString;
begin
  SetArrayLength(Result, 17);
  Result[0] := '.zip';
  Result[1] := '.7z';
  Result[2] := '.rar';
  Result[3] := '.tar';
  Result[4] := '.gz';
  Result[5] := '.bz2';
  Result[6] := '.xz';
  Result[7] := '.tgz';
  Result[8] := '.txz';
  Result[9] := '.tbz2';
  Result[10] := '.tar.gz';
  Result[11] := '.tar.xz';
  Result[12] := '.tar.bz2';
  Result[13] := '.tar.zst';
  Result[14] := '.tzst';
  Result[15] := '.zst';
  Result[16] := '.iso';
end;

procedure RemoveStaleShellExtension(Root: Integer);
begin
  // A previous portable/per-user registration can shadow the machine-wide
  // DLL and keep an old, icon-less build loaded. Remove it before registering
  // the installer copy.
  // 旧的便携版/用户级注册会遮蔽新安装的 DLL，导致仍加载旧版本；注册前清理。
  RegDeleteKeyIncludingSubkeys(Root,
    'Software\Classes\CLSID\' + ShellClsid);
  RegDeleteKeyIncludingSubkeys(Root,
    'Software\Classes\*\shellex\ContextMenuHandlers\QArchiver');
  RegDeleteKeyIncludingSubkeys(Root,
    'Software\Classes\Directory\shellex\ContextMenuHandlers\QArchiver');
  RegDeleteKeyIncludingSubkeys(Root,
    'Software\Classes\Directory\Background\shellex\ContextMenuHandlers\QArchiver');
end;

procedure RemoveOldShellDlls;
var
  FindRec: TFindRec;
  Current, Candidate: String;
begin
  // Try to remove older QArchiverShell*.dll copies. A DLL still mapped by
  // Explorer cannot be deleted; that is safe because the new version is loaded
  // from its own versioned filename.
  // 尝试删除旧的 QArchiverShell*.dll；被 Explorer 占用的 DLL 无法删除，
  // 但新版本使用独立文件名，不受影响。
  Current := ExpandConstant('{app}\{#ShellDll}');
  if FindFirst(ExpandConstant('{app}\QArchiverShell*.dll'), FindRec) then
  begin
    repeat
      Candidate := ExpandConstant('{app}\') + FindRec.Name;
      if CompareText(Candidate, Current) <> 0 then
        DeleteFile(Candidate);
    until not FindNext(FindRec);
    FindClose(FindRec);
  end;
end;

procedure RegisterExtractVerb(BaseKey, Verb, Caption, Command: String);
begin
  RegWriteStringValue(HKLM, BaseKey + '\shell\' + Verb, '', Caption);
  RegWriteStringValue(HKLM, BaseKey + '\shell\' + Verb, 'MUIVerb', Caption);
  RegWriteStringValue(HKLM, BaseKey + '\shell\' + Verb, 'Icon',
                      ExpandConstant('{app}\{#AppExeName}') + ',0');
  RegWriteStringValue(HKLM, BaseKey + '\shell\' + Verb + '\command', '', Command);
end;

procedure RegisterShellMenu;
var
  Exts: TArrayOfString;
  I: Integer;
  BaseKey, Exe: String;
begin
  Exe := ExpandConstant('{app}\{#AppExeName}');

  // Archive files: open + one-click extract / 压缩包：打开 + 一键解压
  Exts := ArchiveExtensions;
  for I := 0 to GetArrayLength(Exts) - 1 do begin
    BaseKey := 'Software\Classes\SystemFileAssociations\' + Exts[I];
    RegisterExtractVerb(BaseKey, 'QArchiver.Open', '用 小Q解压 打开',
                        '"' + Exe + '" "%1"');
    RegisterExtractVerb(BaseKey, 'QArchiver.OneClick', '一键解压',
                        '"' + Exe + '" --oneclick "%1"');
  end;

  // File/folder compress commands come from QArchiverShell.dll (registered by
  // regsvr32 in [Run]); the extract verbs above stay static.
  // 文件/文件夹压缩命令由 QArchiverShell.dll 提供（[Run] 中 regsvr32 注册），
  // 上面的解压命令保持静态注册。
end;

procedure UnregisterShellMenu;
var
  Exts: TArrayOfString;
  I: Integer;
  BaseKey: String;
begin
  Exts := ArchiveExtensions;
  for I := 0 to GetArrayLength(Exts) - 1 do begin
    BaseKey := 'Software\Classes\SystemFileAssociations\' + Exts[I] + '\shell';
    RegDeleteKeyIncludingSubkeys(HKLM, BaseKey + '\QArchiver.Open');
    RegDeleteKeyIncludingSubkeys(HKLM, BaseKey + '\QArchiver.OneClick');
  end;

  RegDeleteKeyIncludingSubkeys(HKLM,
    'Software\Classes\*\shell\QArchiver.AddToZip');
  RegDeleteKeyIncludingSubkeys(HKLM,
    'Software\Classes\*\shell\QArchiver.CompressTo');
  RegDeleteKeyIncludingSubkeys(HKLM,
    'Software\Classes\Directory\shell\QArchiver.AddToZip');
  RegDeleteKeyIncludingSubkeys(HKLM,
    'Software\Classes\Directory\shell\QArchiver.CompressTo');
  RegDeleteKeyIncludingSubkeys(HKLM,
    'Software\Classes\Directory\Background\shell\QArchiver.AddToZip');
  RegDeleteKeyIncludingSubkeys(HKLM,
    'Software\Classes\Directory\Background\shell\QArchiver.CompressTo');
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then begin
    // Remove static compress verbs from QArchiver <= 2.2.0; the shell
    // extension replaces them.
    // 删除 2.2.0 及更早版本的静态压缩菜单；改由右键菜单扩展提供。
    RegDeleteKeyIncludingSubkeys(HKLM,
      'Software\Classes\*\shell\QArchiver.AddToZip');
    RegDeleteKeyIncludingSubkeys(HKLM,
      'Software\Classes\*\shell\QArchiver.CompressTo');
    RegDeleteKeyIncludingSubkeys(HKLM,
      'Software\Classes\Directory\shell\QArchiver.AddToZip');
    RegDeleteKeyIncludingSubkeys(HKLM,
      'Software\Classes\Directory\shell\QArchiver.CompressTo');
    RegDeleteKeyIncludingSubkeys(HKLM,
      'Software\Classes\Directory\Background\shell\QArchiver.AddToZip');
    RegDeleteKeyIncludingSubkeys(HKLM,
      'Software\Classes\Directory\Background\shell\QArchiver.CompressTo');
    RegDeleteKeyIncludingSubkeys(HKCU,
      'Software\Classes\*\shell\QArchiver.AddToZip');
    RegDeleteKeyIncludingSubkeys(HKCU,
      'Software\Classes\*\shell\QArchiver.CompressTo');
    RegDeleteKeyIncludingSubkeys(HKCU,
      'Software\Classes\Directory\shell\QArchiver.AddToZip');
    RegDeleteKeyIncludingSubkeys(HKCU,
      'Software\Classes\Directory\shell\QArchiver.CompressTo');
    RegDeleteKeyIncludingSubkeys(HKCU,
      'Software\Classes\Directory\Background\shell\QArchiver.AddToZip');
    RegDeleteKeyIncludingSubkeys(HKCU,
      'Software\Classes\Directory\Background\shell\QArchiver.CompressTo');
    RemoveStaleShellExtension(HKCU);
    if WizardIsTaskSelected('shellmenu') then
      RegisterShellMenu;
    RemoveOldShellDlls;
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then begin
    RemoveStaleShellExtension(HKLM);
    RemoveStaleShellExtension(HKCU);
    UnregisterShellMenu;
  end;
end;
