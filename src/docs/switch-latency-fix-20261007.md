# 切换输入法后首次输入延迟修复（2026-10-07）

## 问题

Win+Space 切到「英语(美国) 美式键盘」、隔较久再切回小狼毫，立刻打字时候选框要
0.5–2 秒才出现。连续切换则几乎无感。

## 根因

切走中文时走的是 WeaselTSF 的语言配置停用路径：

```
OnActivated(FALSE)
  -> _DeleteCandidateList()
  -> CCandidateList::Destroy()
  -> _DisposeUIWindow()  -> UI::Destroy(true)
```

`Destroy(true)` 会 `delete pimpl_`、`pDWR.reset()`，也就是把整个 `WeaselPanel` 丢掉：
GDI+ 会话、已解码的 12 张 Color-P PNG（`SsfImageStore`）、DirectWrite 字体格式
（`ssf_formats_`）全部释放。切回来第一次打字才在按键路径上同步重建。

注意 `CCandidateList::EndUI()` 用的是**默认** `UI::Destroy(false)`，本来就只销窗口、
留资源；但停用路径用的是 `Destroy(true)`，两者不一致。

## 实测数据

`test\TestSsfSkin\PanelRecreateBench.cpp`（独立基准，不进 weasel.sln）：

| 路径 | 切换后首个可见帧 |
|---|---|
| 旧：`Destroy(true)` + 重建 | 219.6 ms |
| 新：保留解码结果，只重建窗口 | 28.6 ms |

其中旧路径 184.9 ms 花在 `UI::Create`（即 `WeaselPanel` 构造：GDI+ 启动、
`_SsfReloadSkin()` 解码 PNG、`_InitFontRes()` 建 DirectWrite 资源），只有约 19 ms
是首帧布局绘制。28.6 ms 基本上就是纯窗口重建。

这 185 ms 是**页面全在内存**时的开销。隔几小时才切回来时，这些页已被系统换出，
重读+重解码放大到用户听到的 0.5–2 秒——所以真实收益大于 185 ms。

## 修改

1. `WeaselTSF/CandidateList.cpp` — `_DisposeUIWindow()` 改用默认 `_ui->Destroy()`，
   只销原生弹窗，保留 UIImpl 内已解码的一切。彻底释放仍在
   `_DisposeUIWindowAll() -> Destroy(true)`（服务退出时）。
2. `WeaselTSF/CandidateList.cpp` — `CCandidateList::Show(TRUE)` 增加重建：
   `Show()` 原先假定窗口还在，若只销窗口，恢复时窗口不会回来（第一版修复就是这样
   失败的，被回归测试抓到）。现在窗口不存在时先 `_MakeUIWindow()` 再显示。
3. `WeaselTSF/CandidateList.cpp` — `CCandidateList::Destroy()` 幂等化：窗口已经不在时
   直接返回。该方法在普通焦点变化序列里**一次组合会被调用两次**（`_AbortComposition`
   一次、下一次组合开始时一次），原逻辑每次都重新置位 `_nativeWindowDestroyed`，导致
   每轮合成"建了又毁"。置位只在真正销掉窗口时才做。
4. `include/WeaselUI.h` + `WeaselUI/WeaselUI.cpp` — 新增 `UI::panel_hwnd()`，
   返回原生弹窗句柄（无则 NULL），供上面判断"窗口没了但资源还在"。
5. `test\TestSsfSkin\PanelRecreateBench.cpp` + `build-panel-bench.ps1` — 新增回归，
   复现切换往返并断言窗口重建、重新可见、重复销窗是 no-op、候选数据与皮肤样式存活。

## 验证

- `panel-bench.exe`：11 项断言全过（窗口重建约 25–30 ms）。
- `ssf_tests.exe`：237 项，0 失败。
- `host-regression.exe`：通过。
- `test_keypad.py`：636 项，0 失败（Lua 改动后重跑）。
- x64 / x86 Release 双架构构建通过。

## 部署状态

- `D:\Rime\weasel-0.17.4\weaselx64.dll` 已更新为 `437817621B18BCC7…`
- `D:\Rime\weasel-0.17.4\weasel.dll` 已更新为 `AC9DE2F70C35A1A5…`
- 旧文件备份在 `D:\Rime\weasel-0.17.4-backup-20261007-132910\`
- **未完成**：`C:\Windows\System32\weasel.dll` 与 `C:\Windows\SysWOW64\weasel.dll`
  仍是旧版，需要管理员权限；且这两个文件被 explorer.exe / SearchHost 映射，
  必须重启后替换（或先注销）。当前会话无管理员权限，未擅自提权。
- 未替换前，**运行中的宿主仍在使用旧 DLL**，修复不会生效。

## 回归注意

`CCandidateList::Show(TRUE)` 与 `_DisposeUIWindow()` 现在是一对：只改其中一个会让
候选框在切换后不再出现。改动这两处后必须重跑 `panel-bench.exe`。
