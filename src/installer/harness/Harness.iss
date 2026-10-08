; ============================================================================
;  安装器核心逻辑验证装置（Harness）
;
;  为什么需要它：主安装器要写 Program Files、注册 TSF、改动系统目录，都需要
;  管理员权限；而真正容易写错、错了又难发现的是下面这些纯逻辑：
;
;    · weasel.custom.yaml 的按行改写（要保留 UTF-8 中文注释不被破坏）
;    · 缺字段 / 仅 LF / 无尾换行 等边界输入
;    · ExtractTemporaryFile 能否取到 dontcopy 标记的文件
;
;  本装置把主安装器的算法**逐字复制**过来，在 %TEMP% 下自建目录里跑，
;  不需要管理员权限，也不碰任何系统状态。算法若改动，两边必须一起改。
; ============================================================================

; 测试根目录。用编译期 #define 而不是 {app}：{app} 在 InitializeWizard 阶段
; 还不能展开（会在运行时报 “attempted to expand the app constant before it
; was initialized”）。可在编译时用 /DHarnessRoot=... 覆盖。
#ifndef HarnessRoot
  #define HarnessRoot "C:\ColorP-harness"
#endif

[Setup]
AppName=Color-P Installer Harness
AppVersion=1.0
DefaultDirName={#HarnessRoot}\work
DefaultGroupName=Color-P Harness
DisableProgramGroupPage=yes
DisableDirPage=yes
DisableFinishedPage=yes
DisableReadyPage=yes
DisableStartupPrompt=yes
CreateAppDir=yes
PrivilegesRequired=lowest
OutputDir=output
OutputBaseFilename=Color-P-Harness
Compression=none
SolidCompression=no
Uninstallable=no

[Files]
; 与主安装器一致：待改写的配置用 dontcopy。
; 第一个夹具特意不叫 weasel.custom.yaml：它与复制目标同名时会被目标清理
; 动作删掉，导致 CopyFile 失败（实测踩到的坑）。
Source: "fixtures\standard.yaml";  DestDir: "{tmp}"; Flags: dontcopy
Source: "fixtures\empty.yaml";     DestDir: "{tmp}"; Flags: dontcopy
Source: "fixtures\lf-noeol.yaml";  DestDir: "{tmp}"; Flags: dontcopy

[Code]
var
  RimeUserDir: string;
  SkinRoot: string;
  TestLog: string;
  PassCount: Integer;
  FailCount: Integer;

procedure Log(const s: string);
begin
  TestLog := TestLog + s + #13#10;
end;

// ---------------------------------------------------------------- 断言
// 一律走字节层：中文注释的编码完整性只有按字节看才靠得住。
function FileBytes(const Path: string; var d: AnsiString): Boolean;
begin
  Result := False;
  if not FileExists(Path) then Exit;
  Result := LoadStringFromFile(Path, d);
end;

// 注意第 2 个参数的类型：必须是 AnsiString（字节串）。
// 若声明成 string，Inno 会把中文字面量按系统 ANSI 代码页转成字节，
// 而配置文件是 UTF-8，两边字节不同会导致 Pos 永远找不到 —— 测试会假通过。
function HasBytes(const Path: string; const Needle: AnsiString): Boolean;
var
  d: AnsiString;
begin
  Result := False;
  if not FileBytes(Path, d) then Exit;
  Result := Pos(Needle, d) > 0;
end;

// 构造一段「必须原样保留」的期望字节：从 SourcePath 里定位 AsciiMarker，
// 再从 MarkerEndOffset 处取到行尾。
//
// 为什么用偏移而不是把中文写进源码：Inno 会把 string 字面量里的中文按系统
// ANSI 代码页（本机 GBK）转字节，而配置是 UTF-8，两边不同 -> 用中文字面量做
// 字节匹配必然失败。偏移是纯 ASCII，不受代码页影响。这个坑是实测出来的。
function ExpectedSlice(const SourcePath, AsciiMarker: string;
                       MarkerEndOffset: Integer; var Sliced: AnsiString): Boolean;
var
  d: AnsiString;
  p, e: Integer;
begin
  Sliced := '';
  Result := False;
  if not FileBytes(SourcePath, d) then Exit;
  p := Pos(AsciiMarker, d);
  if p = 0 then Exit;
  p := p + MarkerEndOffset;
  e := p;
  while (e <= Length(d)) and (d[e] <> #10) and (d[e] <> #13) do
    e := e + 1;
  Sliced := Copy(d, p, e - p);
  Result := Length(Sliced) > 0;
end;

procedure Check(const Name: string; Ok: Boolean);
begin
  if Ok then
  begin
    PassCount := PassCount + 1;
    Log('[PASS] ' + Name);
  end
  else
  begin
    FailCount := FailCount + 1;
    Log('[FAIL] ' + Name);
  end;
end;

// ============================================================================
//  以下两个函数与主安装器 Color-P.iss 中的实现保持一致（逐字复制）
//  必须在调用者之前定义：Inno 的 Pascal 不支持前置声明。
// ============================================================================
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
// ============================================================================

// 跑一个夹具：提取 -> 复制成 weasel.custom.yaml -> 跑被测算法 -> 断言
//
// 两个实测出的坑，别踩回去：
//   1) ExtractTemporaryFile 每次只提取一个文件，必须逐个调用；
//   2) 夹具若与复制目标同名（都叫 weasel.custom.yaml），提取到 {tmp} 后会被
//      目标清理动作 DeleteFile 删掉，导致 CopyFile 失败。所以夹具要放到
//      子目录 fixtures\ 里，路径分离。
procedure RunFixture(const Fixture, CaseName, ExpectPath: string);
var
  src, dst: string;
begin
  src := ExpandConstant('{tmp}\' + Fixture);
  dst := ExpandConstant('{tmp}\weasel.custom.yaml');

  if not FileExists(src) then
  begin
    try
      ExtractTemporaryFile(Fixture);
    except
      Check(CaseName + ' / 提取夹具 ' + Fixture, False);
      Exit;
    end;
  end;

  if FileExists(dst) then DeleteFile(dst);
  if not CopyFile(src, dst, False) then
  begin
    Check(CaseName + ' / fixture 复制', False);
    Exit;
  end;

  WriteCustomSkinPath();

  Check(CaseName + ' / 生成文件', FileExists(RimeUserDir + '\weasel.custom.yaml'));

  if ExpectPath <> '' then
    Check(CaseName + ' / 路径已写入',
         HasBytes(RimeUserDir + '\weasel.custom.yaml', ExpectPath));
end;

procedure InitializeWizard();
begin
  TestLog := '';
  PassCount := 0;
  FailCount := 0;
  RimeUserDir := ExpandConstant('{#HarnessRoot}\rimeuser');
  SkinRoot := 'C:\ProgramData\ColorPWeasel\Color-P';
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  expectPath, outPath, outFile: string;
  comment: AnsiString;
  ok: Boolean;
begin
  if CurStep <> ssPostInstall then Exit;

  // ---- 用例 0：dontcopy 文件能否被 ExtractTemporaryFile 取出 ----
  // 只断言机制本身，不再顺手提取 —— 提取出来的文件由 RunFixture 负责，
  // 之前在这里提取导致与复制目标同名互删，是个实测踩到的坑。
  ok := True;
  try
    ExtractTemporaryFile('empty.yaml');
  except
    ok := False;
  end;
  Check('ExtractTemporaryFile 取出 dontcopy 文件',
        ok and FileExists(ExpandConstant('{tmp}\empty.yaml')));

  // ---- 用例 1：标准输入 ----
  expectPath := 'style/ssf_skin: ''C:/ProgramData/ColorPWeasel/Color-P''';
  RunFixture('standard.yaml', '标准输入', expectPath);

  outFile := RimeUserDir + '\weasel.custom.yaml';

  // 中文注释必须逐字节保留。期望值从夹具取：定位 ASCII 标记 '# '（2 字节）
  // 之后跳过前面的固定前缀，取到行尾。
  // fixture 第 2 行是「# 配色与布局说明（这行中文用于验证 UTF-8 注释不被破坏）」。
  // 标记 '# ' 占 2 字节，「配色与布局」这 5 个汉字在 UTF-8 下占 5*3=15 字节，
  // 共跳过 17 字节，取剩余部分作为必须逐字节保留的证据。
  ok := ExpectedSlice(ExpandConstant('{tmp}\standard.yaml'), '# ', 17, comment);
  Check('能从输入中取到中文注释段', ok);
  if ok then
    Check('中文注释在输出中逐字节保留',
         HasBytes(outFile, comment));

  // 其余非目标内容也要保留
  Check('未涉及的配置项保留（style/font_face）',
       HasBytes(outFile, 'style/font_face: Arial'));
  Check('未涉及的配置项保留（global_ascii）',
       HasBytes(outFile, 'global_ascii: true'));
  Check('旧的皮肤路径已被替换掉',
       not HasBytes(outFile, 'old/relative/path'));
  Check('皮肤路径用正斜杠（规避正则转义）',
       HasBytes(outFile, 'C:/ProgramData'));

  // ---- 用例 2：缺 ssf_skin 字段 ----
  RimeUserDir := ExpandConstant('{#HarnessRoot}\rimeuser2');
  RunFixture('empty.yaml', '缺 ssf_skin 字段（不应崩）', '');
  Check('缺字段时内容原样写出',
       HasBytes(RimeUserDir + '\weasel.custom.yaml', 'patch:'));

  // ---- 用例 3：仅 LF、无尾换行 ----
  RimeUserDir := ExpandConstant('{#HarnessRoot}\rimeuser3');
  RunFixture('lf-noeol.yaml', 'LF 行尾且无尾换行', expectPath);
  Check('LF 输入后其余内容保留',
       HasBytes(RimeUserDir + '\weasel.custom.yaml', 'other: 1'));

  // ---- 汇总 ----
  Log('');
  Log('合计: ' + IntToStr(PassCount) + ' 通过, ' + IntToStr(FailCount) + ' 失败');

  outPath := ExpandConstant('{#HarnessRoot}\result.txt');
  ForceDirectories(ExtractFileDir(outPath));
  // True = UTF-8 带 BOM。用 False（ANSI）会让中文按系统代码页写出，
  // 读的时候极易误判成「编码被破坏了」，而这个装置恰恰在验证编码完整性。
  SaveStringToFile(outPath, TestLog, True);
end;
