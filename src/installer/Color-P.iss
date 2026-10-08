; ============================================================================
;  Color-P · 小狼毫定制版 单文件安装器
;
;  设计：不重新实现小狼毫的 TSF 注册逻辑，而是内嵌官方安装器静默运行，
;        由它完成文本服务注册 / 卸载项 / 评测数据；随后把本项目编译的三个
;        文件覆盖上去，最后部署皮肤与 Rime 配置。
;
;  与官方安装布局的差异（有意为之）：
;    · 输入法本体 -> {app}            标准程序目录，需要管理员
;    · 皮肤       -> {commonappdata}  AppContainer 宿主（Windows Search 等）
;                                     读不到 Program Files，皮肤必须放这里
;    · Rime 配置  -> RimeUserDir      用户数据，不进程序目录
;
;  三个核心文件用 dontcopy + ExtractTemporaryFile 在 ssPostInstall 才落盘，
;  避免官方安装器的“已存在则跳过”逻辑把我们的版本挤掉。
; ============================================================================

#define MyAppName "Color-P 小狼毫定制版"
#define MyAppVersion "0.17.4"
#define MyAppPublisher "TangSong777"
#define MyAppURL "https://github.com/TangSong777/Color-P"
#define UpstreamVersion "0.17.4"
#define SkinDirName "ColorPWeasel"

[Setup]
AppId={{8F3A7C21-4D6B-4E59-9A2F-7C1B5E8D3A04}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\Rime\weasel-{#UpstreamVersion}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
DisableDirPage=no
LicenseFile=payload\LICENSE.txt
OutputDir=output
OutputBaseFilename=Color-P-Setup-{#MyAppVersion}
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
SetupLogging=yes
Uninstallable=yes
UninstallDisplayName={#MyAppName}
UninstallDisplayIcon={app}\WeaselServer.exe
MinVersion=10.0

[Languages]
Name: "chinese"; MessagesFile: "Languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "skin"; Description: "部署 Color-P 皮肤与 Rime 配置"; GroupDescription: "外观与配置："; Flags: checkedonce
Name: "rimeice"; Description: "安装雾凇拼音（rime-ice）方案数据，约 48 MB"; GroupDescription: "外观与配置："; Flags: checkedonce
Name: "updater"; Description: "安装自动更新组件（WinSparkle）"; GroupDescription: "可选组件："; Flags: unchecked

[Files]
; IMPORTANT — 顺序有性能后果，别随意调整。
; 本安装器开了 SolidCompression；Inno 文档明确要求：用 ExtractTemporaryFile 取的
; 文件必须列在 [Files] 最前面，否则每次提取都要把前面所有文件先解到内存（本载荷
; 四十多 MB，会明显卡顿）。所以四个 dontcopy 文件放在最顶，大件压后。
;
; ---- 本项目编译的三个核心文件：不直接落盘，安装后期再覆盖 --------------
Source: "payload\ime\WeaselServer.exe"; DestDir: "{tmp}"; Flags: dontcopy
Source: "payload\ime\weaselx64.dll";    DestDir: "{tmp}"; Flags: dontcopy
Source: "payload\ime\weasel.dll";       DestDir: "{tmp}"; Flags: dontcopy

; ---- 需要改写皮肤路径的配置文件：先取到临时目录，改完再放 ------------
Source: "..\..\rime-config\weasel.custom.yaml"; DestDir: "{tmp}"; Flags: dontcopy

; ---- 交给官方安装器的部分：内嵌，安装时由代码调用 --------------------
Source: "payload\upstream-installer.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall

; ---- 自动更新组件（可选）--------------------------------------------
Source: "payload\updater\WinSparkle.dll"; DestDir: "{app}"; Flags: ignoreversion; Tasks: updater

; ---- 皮肤资源（ProgramData，AppContainer 宿主可读）------------------
Source: "payload\skin\*"; DestDir: "{commonappdata}\{#SkinDirName}\Color-P"; Flags: ignoreversion recursesubdirs; Tasks: skin

; ---- rime-ice 方案数据 ----------------------------------------------
Source: "payload\rime-data\*"; DestDir: "{code:GetRimeUserDir}"; Flags: ignoreversion recursesubdirs createallsubdirs; Tasks: rimeice

; ---- 其余 Rime 配置 -------------------------------------------------
Source: "..\..\rime-config\default.custom.yaml";  DestDir: "{code:GetRimeUserDir}"; Flags: ignoreversion; Tasks: skin
Source: "..\..\rime-config\rime_ice.custom.yaml"; DestDir: "{code:GetRimeUserDir}"; Flags: ignoreversion; Tasks: skin
Source: "..\..\rime-config\lua\keypad_input.lua"; DestDir: "{code:GetRimeUserDir}\lua"; Flags: ignoreversion; Tasks: skin

[Icons]
Name: "{group}\重新部署"; Filename: "{app}\WeaselDeployer.exe"
Name: "{group}\打开用户文件夹"; Filename: "{app}\WeaselDeployer.exe"; Parameters: "/userdir"
Name: "{group}\卸载 {#MyAppName}"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\WeaselDeployer.exe"; Parameters: "/deploy"; \
  Description: "立即重新部署 Rime（首次会编译词库，约需一分钟）"; \
  Flags: postinstall nowait skipifsilent
Filename: "{#MyAppURL}"; Description: "打开项目主页"; \
  Flags: postinstall shellexec nowait skipifsilent unchecked

[UninstallRun]
; 调小狼毫自带卸载器注销 TSF 文本服务，必须在删文件之前
Filename: "{app}\WeaselSetup.exe"; Parameters: "/u"; Flags: runhidden waituntilterminated; RunOnceId: "UnregisterWeaselTsf"

[Code]
var
  RimeUserDir: string;
  SkinRoot: string;

function GetRimeUserDir(Param: string): string;
begin
  Result := RimeUserDir;
end;

function Normalize(p: string): string;
begin
  Result := p;
  StringChangeEx(Result, '/', '\', True);
end;

// 小狼毫把用户目录记在这里；没记录就用默认 %AppData%\Rime
function DetectRimeUserDir(): string;
var
  v: string;
begin
  if RegQueryStringValue(HKEY_CURRENT_USER, 'Software\Rime\Weasel', 'RimeUserDir', v) and (v <> '') then
    Result := Normalize(v)
  else
    Result := ExpandConstant('{userappdata}\Rime');
end;

// 已安装的小狼毫根目录（官方写在 WOW6432Node 下）
function WeaselInstalledRoot(): string;
var
  v: string;
begin
  if RegQueryStringValue(HKEY_LOCAL_MACHINE, 'SOFTWARE\WOW6432Node\Rime\Weasel', 'WeaselRoot', v) and (v <> '') then
    Result := v
  else if RegQueryStringValue(HKEY_LOCAL_MACHINE, 'SOFTWARE\Rime\Weasel', 'WeaselRoot', v) and (v <> '') then
    Result := v
  else
    Result := '';
end;

function InitializeSetup(): Boolean;
var
  existing, ourDir: string;
  answer: Integer;
begin
  // 必须先取用户目录：下面的 [Files] 用 {code:GetRimeUserDir}，
  // 而该函数只是读出 RimeUserDir 变量，不初始化就会拿到空路径。
  RimeUserDir := DetectRimeUserDir();
  SkinRoot := ExpandConstant('{commonappdata}\{#SkinDirName}\Color-P');

  existing := WeaselInstalledRoot();
  ourDir := ExpandConstant('{autopf}\Rime\weasel-{#UpstreamVersion}');
  if (existing <> '') and (CompareText(existing, ourDir) <> 0) then
  begin
    answer := MsgBox('检测到系统里已安装小狼毫：' + #13#10 +
                     '    ' + existing + #13#10 + #13#10 +
                     '继续安装会在系统里注册出第二个输入法，语言列表会出现重复项。' + #13#10 +
                     '建议先卸载原有版本再运行本安装器。' + #13#10 + #13#10 +
                     '仍要继续吗？', mbConfirmation, MB_YESNO);
    Result := (answer = IDYES);
  end
  else
    Result := True;
end;

procedure KillWeaselProcesses();
var
  rc: Integer;
begin
  Exec(ExpandConstant('{cmd}'), '/c taskkill /F /IM WeaselServer.exe /T >nul 2>&1', '',
       SW_HIDE, ewWaitUntilTerminated, rc);
  Exec(ExpandConstant('{cmd}'), '/c taskkill /F /IM WeaselDeployer.exe /T >nul 2>&1', '',
       SW_HIDE, ewWaitUntilTerminated, rc);
  Sleep(800);
end;

// 把 weasel.custom.yaml 里的 style/ssf_skin 换成实际皮肤目录。
//
// 这里刻意按字节处理：配置文件是 UTF-8，里面还有中文注释。
// 若用 LoadStringFromFile 当成 AnsiString 读、再 Unicode 写回，编码会被破坏。
// 所以把它读进 AnsiString，只替换「整行都是 ASCII」的那一行，其余字节原样保留。
// 皮肤加载器接受正斜杠，于是路径里不会出现需要转义的反斜杠。
function SkinPathForYaml(): string;
var
  p: string;
begin
  p := SkinRoot;
  StringChangeEx(p, '\', '/', True);
  Result := p;
end;

procedure WriteCustomSkinPath();
var
  src, dst: string;
  data, newLine, key, quoted: AnsiString;
  nlPos, i: Integer;
begin
  key := 'style/ssf_skin:';
  src := ExpandConstant('{tmp}\weasel.custom.yaml');
  if not FileExists(src) then
    Exit;
  dst := RimeUserDir + '\weasel.custom.yaml';
  if not LoadStringFromFile(src, data) then
    Exit;

  nlPos := Pos(key, data);
  if nlPos = 0 then
  begin
    ForceDirectories(RimeUserDir);
    SaveStringToFile(dst, data, False);
    Exit;
  end;

  quoted := '''' + SkinPathForYaml() + '''';
  newLine := key + ' ' + quoted;

  // 先删掉该行的旧值（一直删到行尾）
  i := nlPos;
  while (i <= Length(data)) and (data[i] <> #10) and (data[i] <> #13) do
    Delete(data, i, 1);
  // 再把新值插到行首标记之后
  Insert(#13#10 + newLine, data, nlPos + Length(key));

  ForceDirectories(RimeUserDir);
  SaveStringToFile(dst, data, False);
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  rc: Integer;
  base, app, failed: string;
begin
  if CurStep = ssInstall then
  begin
    // 官方安装器可能跳过已存在的文件，先清理目标，确保它真的写入
    KillWeaselProcesses();
    app := ExpandConstant('{app}');
    DeleteFile(app + '\WeaselServer.exe');
    DeleteFile(app + '\weaselx64.dll');
    DeleteFile(app + '\weasel.dll');
  end
  else if CurStep = ssPostInstall then
  begin
    app := ExpandConstant('{app}');

    // ---- 1. 官方安装器完成 TSF 注册（/S 静默）----
    base := ExpandConstant('{tmp}\upstream-installer.exe');
    if FileExists(base) then
    begin
      if not Exec(base, '/S', '', SW_HIDE, ewWaitUntilTerminated, rc) then
        MsgBox('内嵌的小狼毫安装器未能运行，输入法可能没有完成注册。' + #13#10 +
               '可稍后手动运行：' + base, mbError, MB_OK);
      Sleep(1500);
      if not FileExists(app + '\WeaselSetup.exe') then
        MsgBox('官方安装器似乎没有把文件释放到：' + #13#10 + app + #13#10 + #13#10 +
               '安装无法继续，请把上面的路径与安装日志一并反馈。', mbCriticalError, MB_OK);
    end
    else
      MsgBox('找不到内嵌的官方安装器，安装不完整。', mbCriticalError, MB_OK);

    // ---- 2. 覆盖成本项目编译的三个文件 ----
    // 必须校验拷贝结果：若文件仍被占用，CopyFile 会静默失败，用户拿到的
    // 就是一个没被替换过的原版小狼毫，而且不会有任何提示。
    KillWeaselProcesses();
    ExtractTemporaryFile('WeaselServer.exe');
    ExtractTemporaryFile('weaselx64.dll');
    ExtractTemporaryFile('weasel.dll');

    failed := '';
    if not CopyFile(ExpandConstant('{tmp}\WeaselServer.exe'), app + '\WeaselServer.exe', False) then
      failed := failed + #13#10 + '    ' + app + '\WeaselServer.exe';
    if not CopyFile(ExpandConstant('{tmp}\weaselx64.dll'), app + '\weaselx64.dll', False) then
      failed := failed + #13#10 + '    ' + app + '\weaselx64.dll';
    if not CopyFile(ExpandConstant('{tmp}\weasel.dll'), app + '\weasel.dll', False) then
      failed := failed + #13#10 + '    ' + app + '\weasel.dll';

    if failed <> '' then
      MsgBox('以下文件没能替换成 Color-P 定制版：' + failed + #13#10 + #13#10 +
             '通常是这些文件正被占用（宿主进程仍加载着它们）。' + #13#10 +
             '安装出来的会是原版小狼毫，不是定制版。' + #13#10 + #13#10 +
             '请重启后重新运行本安装器。', mbCriticalError, MB_OK);

    // ---- 3. 改写皮肤路径并落盘配置 ----
    if WizardIsTaskSelected('skin') then
    begin
      ExtractTemporaryFile('weasel.custom.yaml');
      WriteCustomSkinPath();
    end;
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
    KillWeaselProcesses();
end;

procedure InitializeWizard();
begin
  RimeUserDir := DetectRimeUserDir();
  SkinRoot := ExpandConstant('{commonappdata}\{#SkinDirName}\Color-P');
end;
