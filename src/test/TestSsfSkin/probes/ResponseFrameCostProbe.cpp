// 微基准：一次完整键盘响应帧的序列化 + 解析开销。
//
// 目的：为「消除每次响应 229~412 us 的图标路径/磁盘探测」提供一个真实的分母。
// 完整端到端还包含 IPC 往返、librime 查词、DirectWrite 绘制，那些本探针测不到；
// 但这一块（构造响应 + 解析响应）是服务端与客户端各自的核心 CPU 工作，
// 量出来就能给出占比的量级判断。
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>
#include <chrono>
#include <cstdint>

namespace {

// 按 WeaselIPCData.h / cereal 的等价布局手工构造与解析，
// 只求规模与成本量级，不求与真实序列化逐字节一致。
struct Cand {
  std::wstring text;
  std::wstring comment;
  std::wstring label;
};

struct Frame {
  std::wstring preedit;
  std::wstring aux;
  std::vector<Cand> candies;
  int highlighted = 0;
  int page_size = 5;
  int page_no = 0;
  bool composing = true;
  bool ascii_mode = false;
  int sel_start = 0;
  int sel_end = 0;
};

template <class F>
double BenchUs(const char* label, int iters, F&& f) {
  for (int i = 0; i < iters / 10; ++i) f();
  double best = 1e18;
  for (int rep = 0; rep < 3; ++rep) {
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < iters; ++i) f();
    auto t1 = std::chrono::steady_clock::now();
    double us = std::chrono::duration<double, std::micro>(t1 - t0).count() / iters;
    if (us < best) best = us;
  }
  std::printf("  %-44s %9.2f us\n", label, best);
  return best;
}

Frame MakeFrame(int cand_count) {
  Frame f;
  f.preedit = L"woshi";
  f.aux = L"";
  f.candies.resize(cand_count);
  for (int i = 0; i < cand_count; ++i) {
    f.candies[i].text = L"候选词" + std::to_wstring(i);
    f.candies[i].comment = L"释义" + std::to_wstring(i);
    f.candies[i].label = std::to_wstring(i + 1) + L".";
  }
  f.highlighted = 0;
  return f;
}

// 模拟「压平成 IPC 缓冲区」：把帧里的字符串按字节复制进一块缓冲
std::vector<char> Serialize(const Frame& f) {
  std::vector<char> out;
  out.reserve(4096);
  auto put = [&out](const void* p, size_t n) {
    const char* c = static_cast<const char*>(p);
    out.insert(out.end(), c, c + n);
  };
  put(f.preedit.data(), f.preedit.size() * sizeof(wchar_t));
  put(f.aux.data(), f.aux.size() * sizeof(wchar_t));
  const uint32_t n = static_cast<uint32_t>(f.candies.size());
  put(&n, sizeof(n));
  for (const auto& c : f.candies) {
    for (const auto* s : {&c.text, &c.comment, &c.label}) {
      const uint32_t len = static_cast<uint32_t>(s->size());
      put(&len, sizeof(len));
      put(s->data(), s->size() * sizeof(wchar_t));
    }
  }
  put(&f.highlighted, sizeof(f.highlighted));
  put(&f.page_size, sizeof(f.page_size));
  return out;
}

// 模拟「从 IPC 缓冲区解析回帧」：逐个读出并构造 std::wstring
Frame Parse(const std::vector<char>& buf) {
  Frame f;
  size_t off = 0;
  auto get = [&buf, &off](void* dst, size_t n) {
    if (off + n > buf.size()) return false;
    memcpy(dst, buf.data() + off, n);
    off += n;
    return true;
  };
  auto get_str = [&](std::wstring& s) {
    uint32_t len = 0;
    if (!get(&len, sizeof(len))) return false;
    if (off + static_cast<size_t>(len) * sizeof(wchar_t) > buf.size()) return false;
    s.assign(reinterpret_cast<const wchar_t*>(buf.data() + off), len);
    off += static_cast<size_t>(len) * sizeof(wchar_t);
    return true;
  };
  if (!get_str(f.preedit)) return f;
  if (!get_str(f.aux)) return f;
  uint32_t n = 0;
  if (!get(&n, sizeof(n))) return f;
  f.candies.resize(n);
  for (auto& c : f.candies) {
    if (!get_str(c.text) || !get_str(c.comment) || !get_str(c.label)) return f;
  }
  get(&f.highlighted, sizeof(f.highlighted));
  get(&f.page_size, sizeof(f.page_size));
  return f;
}

}  // namespace

int main() {
  const int N = 30000;
  std::printf("=== 一次响应帧的 CPU 成本 ===\n");
  for (int cands : {5, 10, 50, 130}) {
    Frame f = MakeFrame(cands);
    std::vector<char> buf = Serialize(f);
    char label[96];
    std::snprintf(label, sizeof(label), "序列化 %d 候选 (%zu 字节)", cands, buf.size());
    double ser = BenchUs(label, N, [&] { (void)Serialize(f); });
    std::snprintf(label, sizeof(label), "解析   %d 候选", cands);
    double par = BenchUs(label, N, [&] { (void)Parse(buf); });
    std::printf("      -> 合计 %.2f us\n", ser + par);
  }

  std::printf("\n=== 本次优化消除的量（实测）===\n");
  std::printf("  图标路径/磁盘探测 4 次        229 ~ 412 us\n");

  std::printf("\n=== 结论用的参考区间 ===\n");
  std::printf("  单次按键端到端（未实测，仅量级参考）  1000 ~ 5000 us\n");
  return 0;
}
