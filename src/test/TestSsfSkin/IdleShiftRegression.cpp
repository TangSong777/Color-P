// 裸 Shift 切换中英文状态机的回归测试。
//
// 这套判定原先埋在 WeaselServerApp.cpp 的底层键盘钩子里，没有测试保护。
// 它决定“按 Shift 到底切不切模式”，值得锁住。
//
// 关键行为（用户可感知）：
//   · 单独按一下 Shift 再松开      -> 切换
//   · Shift 按下期间按了别的键      -> 不切换（打大写之后松手不会误切）
//   · Shift 按下期间点击/滚轮       -> 不切换
//   · Ctrl+Shift / Alt+Shift 等     -> 不切换
//   · 按住时长不影响判定
#include <windows.h>
#include <cstdio>
#include <string>
#include <WeaselIdleShift.h>

using weasel_idle_shift::Apply;
using weasel_idle_shift::Classify;
using weasel_idle_shift::Event;
using weasel_idle_shift::State;

static int checks = 0;
static int failures = 0;

static void Check(bool ok, const char* name) {
  ++checks;
  if (!ok) {
    ++failures;
    std::printf("FAIL: %s\n", name);
  }
}

// 一次手势的驱动：把事件序列喂进状态机，返回是否最终排了一次切换。
class Driver {
 public:
  void Event_(weasel_idle_shift::Event e, bool arm_allowed = true,
              DWORD shift_vk = VK_LSHIFT) {
    const auto d = Classify(state_, e, arm_allowed, shift_vk);
    Apply(state_, d, arm_allowed, shift_vk);
    if (d.cancel) {
      timer_ = 0;
      state_.armed = false;
      state_.timer_pending = false;
    } else if (d.schedule) {
      ++scheduled_;
      timer_ = 1;  // 简化：定时器已排队
      state_.timer_pending = true;
    }
  }

  // 定时器触发（ApplyIdleShift）
  void FireTimer() {
    if (timer_) {
      timer_ = 0;
      state_.timer_pending = false;
      ++toggled_;
    }
  }

  void ShiftDown(DWORD vk = VK_LSHIFT, bool arm_allowed = true) {
    Event_(Event::kShiftDown, arm_allowed, vk);
  }
  void ShiftUp(DWORD vk = VK_LSHIFT, bool arm_allowed = true) {
    Event_(Event::kShiftUp, arm_allowed, vk);
  }
  void OtherKey() { Event_(Event::kNonShiftKey); }
  void Pointer() { Event_(Event::kPointerActivity); }

  int scheduled() const { return scheduled_; }
  int toggled() const { return toggled_; }
  bool armed() const { return state_.armed; }

 private:
  State state_;
  int scheduled_ = 0;
  int toggled_ = 0;
  int timer_ = 0;
};

int main() {
  // 1. 单独按一下 Shift 再松开 -> 切换
  {
    Driver d;
    d.ShiftDown();
    d.ShiftUp();
    Check(d.scheduled() == 1, "单独 Shift 按下并松开 -> 排一次切换");
    d.FireTimer();
    Check(d.toggled() == 1, "定时器触发 -> 实际切换一次");
  }

  // 2. Shift 按下期间按了别的键 -> 不切换（用户报告的“长按不切换”）
  {
    Driver d;
    d.ShiftDown();
    d.OtherKey();  // 例如 Shift+A 打大写
    d.ShiftUp();
    Check(d.scheduled() == 0, "Shift 期间按其它键 -> 松开不切换");
    d.FireTimer();
    Check(d.toggled() == 0, "且定时器不会带来切换");
  }

  // 3. 按住很久但没按别的键 -> 仍应切换（判定只看手势是否纯净，不看到时长）
  {
    Driver d;
    d.ShiftDown();
    // 模拟“按住很久”：期间没有任何其它事件
    d.ShiftUp();
    Check(d.scheduled() == 1, "长按单个 Shift 松开 -> 仍切换（无时长门槛）");
  }

  // 4. Shift 按下期间点击鼠标 -> 不切换
  {
    Driver d;
    d.ShiftDown();
    d.Pointer();
    d.ShiftUp();
    Check(d.scheduled() == 0, "Shift 期间点击鼠标 -> 不切换");
  }

  // 5. 前提不满足（Ctrl+Shift 等）-> 不切换
  {
    Driver d;
    d.ShiftDown(VK_LSHIFT, /*arm_allowed=*/false);
    d.ShiftUp();
    Check(d.scheduled() == 0, "前提不满足（Ctrl/Alt/Win 或非 Weasel）-> 不切换");
    Check(!d.armed(), "前提不满足时不处于待命");
  }

  // 6. 左右 Shift 混用：按下 LShift 抬起 RShift -> 不切换
  {
    Driver d;
    d.ShiftDown(VK_LSHIFT);
    d.ShiftUp(VK_RSHIFT);
    Check(d.scheduled() == 0, "按下 LShift 却抬起 RShift -> 不切换");
  }

  // 7. 连续两次干净手势 -> 切换两次
  {
    Driver d;
    d.ShiftDown();
    d.ShiftUp();
    d.FireTimer();
    d.ShiftDown();
    d.ShiftUp();
    d.FireTimer();
    Check(d.toggled() == 2, "两次干净手势 -> 切换两次");
  }

  // 8. 排队的切换被取消后不应再生效（原来点鼠标就能取消）
  {
    Driver d;
    d.ShiftDown();
    d.ShiftUp();
    Check(d.scheduled() == 1, "先排一次切换");
    d.Pointer();  // 期间点击 -> 取消
    d.FireTimer();
    Check(d.toggled() == 0, "排队后被鼠标点击取消 -> 不再切换");
  }

  // 9. 手势取消后，下一次干净手势仍能正常切换
  {
    Driver d;
    d.ShiftDown();
    d.OtherKey();
    d.ShiftUp();
    d.ShiftDown();
    d.ShiftUp();
    d.FireTimer();
    Check(d.toggled() == 1, "取消一次后，下一次干净手势仍可切换");
  }

  // 10. 按键自动重复（按住不放产生的重复 WM_KEYDOWN）不应重复待命或排队
  {
    Driver d;
    d.ShiftDown();
    d.ShiftDown();  // 自动重复
    d.ShiftDown();
    d.ShiftUp();
    Check(d.scheduled() == 1, "自动重复的 Shift 按下只排一次切换");
  }

  // 11. 抬起非待命的 Shift（例如没经过按下）不应切换
  {
    Driver d;
    d.ShiftUp();
    Check(d.scheduled() == 0, "未待命时抬起 Shift -> 不切换");
  }

  std::printf("Idle Shift: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
