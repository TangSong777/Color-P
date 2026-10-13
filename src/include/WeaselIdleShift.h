#pragma once
// 裸 Shift 切换中英文的状态机（可单测）。
//
// 为什么要单独抽出来：这套判定原先写在 WeaselServerApp.cpp 的底层键盘钩子里，
// 与 SetTimer / CallNextHookEx 等 API 混在一起，没法单独验证。而它决定的是
// “按 Shift 到底切不切模式”这种用户每天都会碰到的行为，值得有回归保护。
//
// 抽出来之后钩子只负责把事件喂进来并执行返回的三个动作，判定本身是纯函数。
//
// 判定规则（“裸 Shift”）：
//   · 仅当没有 Ctrl/Alt/Win 修饰时才会进入待命；
//   · Shift 按下后到抬起之间，出现任何其它按键、鼠标点击或滚轮，本次手势作废；
//   · 只有“单独按下又单独抬起”才会切换 —— 这样 Shift+字母 打大写之后松开
//     Shift 不会误切模式。
//   注意：判定依据是“手势是否纯净”，不是按住时长；按住多久都不影响。
namespace weasel_idle_shift {

enum class Event {
  kNonShiftKey,      // 任何非 Shift 键（按下或抬起都算）
  kShiftDown,        // Shift 按下
  kShiftUp,          // Shift 抬起
  kPointerActivity,  // 鼠标点击或滚轮
};

struct State {
  bool armed = false;          // 本次 Shift 手势仍有效
  DWORD shift_key = 0;         // 待命时记下的 Shift 虚拟键码
  bool timer_pending = false;  // 已排队一次切换
};

struct Decision {
  bool cancel = false;  // 丢弃待命状态并取消已排队的切换
  bool arm = false;     // 尝试进入待命（需同时满足 arm_allowed）
  bool schedule = false;  // 排队一次切换
};

// can_toggle / arm_allowed 的求值留在调用方：它们要访问 handler 状态，
// 而且必须各自只求值一次（arm 的前提检查开销不小，不应被重复执行）。
inline Decision Classify(State& state, Event event, bool arm_allowed,
                         DWORD shift_vk = 0) {
  Decision d;
  switch (event) {
    case Event::kNonShiftKey:
    case Event::kPointerActivity:
      d.cancel = true;
      break;
    case Event::kShiftDown:
      // 只记“这次想待命”，实际是否生效由 arm_allowed 决定。
      // 原实现在前提不满足时同样会把 armed 置为 false、并记下 key，
      // 这里保持同样的状态形状，避免影响后续事件的判定。
      d.arm = true;
      break;
    case Event::kShiftUp:
      d.schedule = state.armed && shift_vk == state.shift_key &&
                   !state.timer_pending;
      break;
  }
  return d;
}

// 把 Decision 应用到状态。调用方随后按同一 Decision 执行副作用
// （KillTimer / SetTimer），保证状态与副作用一一对应。
inline void Apply(State& state, const Decision& d, bool arm_allowed,
                  DWORD shift_vk = 0) {
  if (d.cancel) {
    state.armed = false;
    state.timer_pending = false;
    return;
  }
  if (d.arm) {
    state.armed = arm_allowed;
    state.shift_key = shift_vk;
    return;
  }
  if (d.schedule) {
    state.timer_pending = true;
    state.armed = false;  // 原实现在排队后立即清除待命
    return;
  }
  // 抬起但未排队：原实现无条件清除待命
  state.armed = false;
}

}  // namespace weasel_idle_shift
