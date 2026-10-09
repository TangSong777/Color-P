// 小键盘 / 标点行为诊断探针
//
// 目的：用真实 librime 回答两个问题
//   1) 有候选框（输入过字母、处于混合输入）时，小键盘数字/字符/标点是否进入候选框
//   2) 无候选框（没输入过字母）时，它们是否直接上屏，并按中英状态取全角/半角
//
// 只做只读观察，不写用户词库。
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>
#include "rime_api.h"

static RimeApi* g_api = nullptr;
static RimeSessionId g_session = 0;

static std::string Input() {
  const char* p = g_api->get_input(g_session);
  return std::string(p ? p : "");
}
static std::string Commit() {
  RIME_STRUCT(RimeCommit, c);
  std::string text;
  if (g_api->get_commit(g_session, &c)) {
    text = c.text ? c.text : "";
    g_api->free_commit(&c);
  }
  return text;
}
static std::string Preedit() {
  RIME_STRUCT(RimeContext, c);
  std::string s;
  if (g_api->get_context(g_session, &c)) {
    if (c.composition.preedit && c.composition.length > 0) s = c.composition.preedit;
    g_api->free_context(&c);
  }
  return s;
}
static int CandCount() {
  RIME_STRUCT(RimeContext, c);
  int n = 0;
  if (g_api->get_context(g_session, &c)) {
    n = c.menu.num_candidates;
    g_api->free_context(&c);
  }
  return n;
}
static void Reset(bool ascii_mode) {
  g_api->clear_composition(g_session);
  g_api->set_option(g_session, "ascii_mode", ascii_mode);
  Commit();
}
static bool Key(int code) { return g_api->process_key(g_session, code, 0) != 0; }

static std::string Prop(const char* name) {
  char buffer[32768] = {};
  g_api->get_property(g_session, name, buffer, sizeof(buffer));
  return std::string(buffer);
}

struct Step { int code; const char* label; };

static void Run(const char* title, bool ascii_mode, const Step* steps) {
  Reset(ascii_mode);
  std::printf("\n[%s]  (ascii_mode=%d)\n", title, ascii_mode ? 1 : 0);
  for (const Step* s = steps; s->label; ++s) {
    bool eaten = Key(s->code);
    std::string commit = Commit();
    std::printf("  %-14s eaten=%d commit='%s' input='%s' suffix='%s' prefix='%s' cand=%d\n",
                s->label, eaten ? 1 : 0, commit.c_str(), Input().c_str(),
                Prop("weasel_keypad_suffix").c_str(),
                Prop("weasel_keypad_prefix").c_str(), CandCount());
  }
}

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  SetDllDirectoryW(L"D:\\Rime\\weasel-0.17.4");
  auto dll = LoadLibraryW(L"D:\\Rime\\weasel-0.17.4\\rime.dll");
  if (!dll) { std::printf("no rime.dll\n"); return 3; }
  g_api = reinterpret_cast<RimeApi*(*)()>(GetProcAddress(dll, "rime_get_api"))();
  RIME_STRUCT(RimeTraits, traits);
  traits.shared_data_dir = "D:\\RimeUser\\build";
  traits.prebuilt_data_dir = "D:\\RimeUser\\build";
  traits.user_data_dir = argv[1];
  traits.app_name = "rime.keypad_probe";
  traits.log_dir = argv[1];
  traits.min_log_level = 3;
  g_api->setup(&traits);
  g_api->initialize(&traits);
  g_session = g_api->create_session();
  if (!g_session || !g_api->select_schema(g_session, "rime_ice")) {
    std::printf("session/schema failed\n");
    return 4;
  }
  std::printf("========== 场景 A：无候选框（冷启动，未输入字母）==========\n");
  const Step a_digit[]  = {{0xffb4, "KP_4"}, {0, nullptr}};
  const Step a_comma[]  = {{',', "comma"}, {0, nullptr}};
  const Step a_period[] = {{'.', "period"}, {0, nullptr}};
  const Step a_ops[]    = {{0xffab, "KP_Add"}, {0, nullptr}};
  const Step a_dec[]    = {{0xffae, "KP_Decimal"}, {0, nullptr}};
  for (bool en : {false, true}) {
    Run("A1 小键盘数字", en, a_digit);
    Run("A2 逗号",       en, a_comma);
    Run("A3 句点",       en, a_period);
    Run("A4 小键盘加号", en, a_ops);
    Run("A5 小键盘小数点", en, a_dec);
  }

  std::printf("\n========== 场景 B：有候选框（已输入字母，混合输入）==========\n");
  const Step b_digit[]  = {{'n', "n"}, {0xffb4, "KP_4"}, {0, nullptr}};
  const Step b_comma[]  = {{'n', "n"}, {',', "comma"}, {0, nullptr}};
  const Step b_period[] = {{'n', "n"}, {'.', "period"}, {0, nullptr}};
  const Step b_ops[]    = {{'n', "n"}, {0xffab, "KP_Add"}, {0, nullptr}};
  const Step b_lit[]    = {{'n', "n"}, {0xffb4, "KP_4"}, {',', "comma"}, {0, nullptr}};
  for (bool en : {false, true}) {
    Run("B1 字母+小键盘数字", en, b_digit);
    Run("B2 字母+逗号",       en, b_comma);
    Run("B3 字母+句点",       en, b_period);
    Run("B4 字母+小键盘加号", en, b_ops);
    Run("B5 ni4 后逗号",      en, b_lit);
  }

  std::printf("\n========== 场景 C：数字后分隔符（小数语义）==========\n");
  const Step c_dec[] = {{0xffb4, "KP_4"}, {'.', "period"}, {0xffb5, "KP_5"}, {0, nullptr}};
  const Step c_asc[] = {{',', "comma"}, {'.', "period"}, {0, nullptr}};
  for (bool en : {false, true}) {
    Run("C1 4.5", en, c_dec);
    Run("C2 连续逗号句点", en, c_asc);
  }

  g_api->destroy_session(g_session);
  g_api->finalize();
  std::printf("\n========== 结束 ==========\n");
  return 0;
}
