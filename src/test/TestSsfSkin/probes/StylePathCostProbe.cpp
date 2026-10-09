// 微基准：WeaselUserDataPath() / WeaselSharedDataPath() 以及图标探测的开销。
//
// 目的：为一个具体问题给出数字 —— WeaselRenderer::_UpdateUIStyle 里每次键盘响应
// 都会调用 load_icon 四次，而该 lambda 内部每次都会重算两个路径（含注册表访问）
// 并做磁盘存在性检查。这里量出单次成本，据此判断该不该优化。
#include <windows.h>
#include <cstdio>
#include <string>
#include <filesystem>
#include <chrono>

namespace fs = std::filesystem;

// 与 WeaselUtility.cpp 中的实现保持一致
static fs::path WeaselUserDataPath() {
  WCHAR _path[MAX_PATH] = {0};
  const WCHAR KEY[] = L"Software\\Rime\\Weasel";
  HKEY hKey;
  LSTATUS ret = RegOpenKeyW(HKEY_CURRENT_USER, KEY, &hKey);
  if (ret == ERROR_SUCCESS) {
    DWORD len = sizeof(_path);
    DWORD type = 0;
    ret = RegQueryValueExW(hKey, L"RimeUserDir", NULL, &type, (LPBYTE)_path, &len);
    RegCloseKey(hKey);
    if (ret == ERROR_SUCCESS && type == REG_SZ && _path[0]) return fs::path(_path);
  }
  ExpandEnvironmentStringsW(L"%AppData%\\Rime", _path, _countof(_path));
  return fs::path(_path);
}

static fs::path WeaselSharedDataPath() {
  wchar_t _path[MAX_PATH] = {0};
  GetModuleFileNameW(NULL, _path, _countof(_path));
  return fs::path(_path).remove_filename().append("data");
}

template <class F>
static double BenchUs(const char* label, int iters, F&& f) {
  // 预热
  for (int i = 0; i < iters / 10; ++i) f();
  double best = 1e18;
  for (int rep = 0; rep < 3; ++rep) {
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < iters; ++i) f();
    auto t1 = std::chrono::steady_clock::now();
    double us = std::chrono::duration<double, std::micro>(t1 - t0).count() / iters;
    if (us < best) best = us;
  }
  std::printf("  %-46s %8.3f us/call\n", label, best);
  return best;
}

int main() {
  const int N = 20000;
  std::printf("=== 单次调用开销 ===\n");
  double user = BenchUs("WeaselUserDataPath()   (注册表)", N, [] { (void)WeaselUserDataPath(); });
  double shared = BenchUs("WeaselSharedDataPath() (GetModuleFileNameW)", N, [] { (void)WeaselSharedDataPath(); });

  const fs::path user_dir = WeaselUserDataPath();
  const fs::path shared_dir = WeaselSharedDataPath();
  const fs::path probe = user_dir / L"weasel.custom.yaml";
  double isfile = BenchUs("fs::is_regular_file(已存在文件)", N, [&] { (void)fs::is_regular_file(probe); });
  const fs::path missing = user_dir / L"__no_such_icon__.png";
  double ismissing = BenchUs("fs::is_regular_file(不存在的文件)", N, [&] { (void)fs::is_regular_file(missing); });

  std::printf("\n=== 现状：每次键盘响应调用 load_icon 4 次 ===\n");
  double per_call = user + shared + isfile;         // 至少算到第一个 is_regular_file
  std::printf("  单次 load_icon 至少        %8.3f us\n", per_call);
  std::printf("  4 次（未命中直接返回）     %8.3f us\n", per_call * 4);
  std::printf("  4 次（两个目录各查一次）   %8.3f us\n", (user + shared + isfile + isfile) * 4);

  std::printf("\n=== 优化后：路径只算一次，且未配置图标时直接返回 ===\n");
  std::printf("  路径提取到 lambda 外       %8.3f us  (2 次路径 + 4 次 is_regular_file)\n",
              user + shared + isfile * 2);
  std::printf("  未配置图标直接跳过         %8.3f us\n", 0.0);

  std::printf("\n=== 单次按键总延迟参考量级 ===\n");
  std::printf("  IPC 往返 + Rime 查词 + 绘制   约 1000 ~ 5000 us\n");
  return 0;
}
