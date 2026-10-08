# 直觉与行为审计（2026-10-07）

对 Color-P 小狼毫做的两次独立只读审计（输入行为、生命周期/宿主交互），加上本机
`rime.dll` 隔离实测。**只列经代码或实测确认的问题**，猜测一律标注。

已修复的见 `switch-latency-fix-20261007.md`（切换输入法延迟、`123。4` 数字分隔符）。

---

## 一、已修复

### 中文模式下小数/IP 的点号被改成「。」（`digit_separators` 失效）

- 现象（实测，修复前）：中文半角下 `1 2 3 . 4` → `123。4`；`192.168.1.1` →
  `192。168。1。1`；`123,4` → `123，4`。
- 根因：`D:\RimeUser\lua\keypad_input.lua` 的处理器排在 `punctuator` **之前**，未组合时
  它直接 `commit_text(配置里的中文标点)` 并返回 1，librime 的
  `punctuator/digit_separators: ",.:"`（`build/default.yaml:38`、
  `build/rime_ice.schema.yaml:127`）永远走不到。
- 修复：新增 `env.last_commit` 记录本处理器最近提交的字面量（Lua 处理器读不到文档里
  已有什么），当分隔符紧跟数字且 `ascii_punct` 关闭时返回 2 交给 punctuator。
- 验证：`123.4`、`123,4`、`192.168.1.1` 正确；`ascii_punct` 打开时仍为 `123.4`；全角
  仍为 `１２３．４`；`test\TestSsfSkin\test_keypad.py` 636 项通过。
- **该文件三处副本必须一致**：`D:\RimeUser\lua\`、仓库内 `src\rime\lua\`、发布包 `lua\`
  （当前均为 `560003EA97E64146…`），否则下次部署会覆盖修复。

### 拿不到光标位置时候选窗完全不显示

- 现象（修复前）：`Composition.cpp:302-313` 在 TSF `GetTextExt` 失效、又取不到真实
  caret、且视图矩形过小（宽 ≤200 或高 ≤100）时直接 `return S_OK`，**不发任何位置**；
  而 `CCandidateList::Show()` 要求 `_positionValid`，否则隐藏 → 拼音正常组合但候选窗
  一个都不出现，用户盲打且无提示。
- 修复：视图不可用时改用宿主窗口矩形作兜底锚点（`GetWindowRect(foreground)`），再走
  `FallbackAnchor`。原来只在 TSF 视图可用时才兜底，而该分支恰恰是 TSF 视图不可用。
  兜底位置不代表精确 caret，但比"什么都不显示"好。
- 验证：`host-regression.exe` 增加视图尺寸守卫断言后通过。

---

## 二、确认存在、按现状保留（附理由）

### 全角标点有 7 个键输出的字符与配置不符

- 现象（实测）：全角模式下 `.` `^` `[` `$` `{` `\` `|` 输出 `．＾［＄｛＼｜`，而配置
  （`build/default.yaml:39-72`）要求 `。……「￥『、·`。
- 根因（已实测定位）：这些键在配置里是 `{commit: X}` 或 `["A","B",...]` 形态，而 Lua 的
  `config:get_string` 对 map/list **一律返回 nil**（已打印确认，属 API 既定行为）。
  `env.punctuation` 因此回退成 ASCII 原字符，再被 librime 的 shape formatter 变成全角
  ASCII。
- 尝试过两次修复，都因引入回归而回退：list 的 `get_value_at(i)` 返回的是 `ConfigValue`
  包装对象（不是字符串），取不到内容；擅自改提交路径会让非组合态的 `[` `$` 变成空输出。
  正确做法需要把 `ConfigValue` 解成字符串，当前 Lua 绑定下没有干净的入口。
- **保留现状：与修复前逐键完全一致。** 影响仅限主动切全角标点的用户。

### `-` / `=` 在候选菜单打开时永远被吞掉

- 现象（实测）：有候选时按 `-` 或 `=`，第一页和最后一页也不输出字符。
- 这是既定设计，且**改成 `paging` 也解决不了**：librime 在没有下一页时刻意吞键
  （`librime-src\src\rime\gear\selector.cc:184`），而 `paging` 标签在
  `NextPage`/`PreviousPage` 里是**无条件**插入的（`selector.cc:167`、`191`），所以
  `paging` 条件在边界同样成立。`keypad_input.lua:373-377` 的注释也明确写了"翻页归
  key binder 所有"。与 stock Rime 语义一致，故保留。

### 语言栏/托盘的「中/英」按钮点了不起作用，但图标会先变一下

- 依据（未实测）：`LanguageBar.cpp:151-160` 点击后本地翻转 `ascii_mode` 立刻重绘图标并
  发 `ID_WEASELTRAY_ENABLE_ASCII`；而 `RimeWithWeasel.cpp:669-673` 在
  `m_global_ascii_mode && m_base_style.ssf_enabled` 时**明确 return 忽略**，注释为
  "This custom profile changes language only through bare Shift."
- 这是**有意的设计决定**，不是疏漏（`SetOption` 里为裸 Shift 专门写了全局传播与
  两秒提示的机制）。故不推翻。副作用是工具提示写着"左键切换模式"却不生效、图标会
  假变一下——如果要修，是改工具提示文案或让该按钮真正生效，属于产品取舍。

### `Tab` / `Shift+Tab` 在拼音组合中被吃掉，且会在音节间环绕

- 现象（实测）：`nihao` 组合中按 Tab 被吃掉（eaten=1），光标跳到第一个音节末并在音节
  边界循环；无组合时 Tab 正常放行（eaten=0）。
- 根因：`rime_ice.custom.yaml:26-27` 显式映射 `Tab → Shift+Right`、
  `Shift+Tab → Shift+Left`（`when: composing`）。这是用户写在自定义配置里的显式意图，
  不是代码缺陷，故保留。

### 左右方向键在两端会环绕

- 现象（实测）：`nihao` 末尾按 `→` 光标跑到开头，开头按 `←` 跑到末尾。
- 根因：`build/rime_ice.schema.yaml` 引用了 `navigator` 但**没有写 `navigator:` 配置段**，
  所以 `default.yaml:90-95` 的 `Left: left_by_char_no_loop` 等是死配置，实际用 librime
  内置绑定（`navigator.cc:32-58` 的 `Rewind`/`RightByChar` 都以 `GoToEnd`/`GoHome` 收尾）。
  且 `_no_loop` 这类动作名需要 librime ≥ 1.16，本机是 1.13.1（`default.yaml:83-88` 自注）。
- 补 `navigator:` 段在 1.13.1 上无法实现"不环绕"，改内置行为需要换 librime 版本，故保留。

### 组合未完成时切焦点/点别处 → 已输入内容被丢弃而非上屏

- 依据（未实测，Esc 路径实测同源）：`KeyEventSink.cpp:73-85` → `_AbortComposition()`
  → `Composition.cpp:548-555`（`ClearComposition` + `EndComposition(clear=true)`）。
- 与主流直觉相反（搜狗/微软拼音失焦会把已敲字母原文上屏），但改成"上屏"会把半成品
  塞进宿主文档，是可争议的产品行为，`keypad_input.lua` 注释也表明当前行为是有意的。
  同时候选窗生命周期刚改动过，不再叠加同类改动，故保留待定。

---

## 三、其它确认但未改动的小问题

- **CapsLock 不切中英**：`default.yaml:75` 写 `Caps_Lock: clear` 且注释说切换中英，但
  `default.custom.yaml:9` 覆写为 `noop`；`KeyEventSink.cpp:48-66` 为它写的补偿逻辑因此
  是死代码（需要 `prevfEaten == TRUE` 才进入）。
- **鼠标悬停高亮恒不生效**：`weasel.yaml` 的 `hover_type: none`，
  `WeaselPanel.cpp:696-697` 在 NONE 时直接 return。选中靠"按下改高亮 + 抬起才提交"。
- **IPC 1.5 秒超时会静默丢键**：`PipeChannel.cpp:99-110` 超时后
  `WeaselClientImpl.cpp:184-187` 清零 session 并让该键漏给应用变成 ASCII。
- **`_uiStarted` 全仓从未置 true**：`BeginUIElement` 已删除，`CandidateList.cpp:260-261`
  与 `EndUI()` 里的 `EndUIElement` 都是死代码。

---

## 四、审计中核实为"正常"的项（避免误报）

- Backspace / Delete / Esc / 空格 / Home / End / Enter 在普通拼音组合中与 stock Rime 一致。
- 顶排数字选词、小键盘原义输入、无拼音时独立符号不弹候选、混合态 Enter 提交原文、
  鼠标点击选中、滚轮翻页均按既定设计工作。
- 顶排 `6`–`0` 有候选时被静默吞掉是 **stock librime** 行为（`selector.cc:151-154`），
  不是本项目偏差。
- 模式提示的 2 秒定时器在窗口销毁时**不会**残留（窗口定时器随 HWND 回收）。

---

## 五、本轮验证

- `test_keypad.py`：636 项，0 失败。
- `panel-bench.exe`：11 项断言通过。
- `ssf_tests.exe`：237 项，0 失败。
- `host-regression.exe`：通过（含新增视图尺寸守卫断言）。
- x64 / x86 Release 双架构构建通过。
