// 单用例探针：每次只跑一个场景，保证 Lua 状态是全新的。
// 用法: kpcase.exe <隔离数据目录> <场景名>
// 场景名见 main 里的表。
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
  std::string t;
  if (g_api->get_commit(g_session, &c)) { t = c.text ? c.text : ""; g_api->free_commit(&c); }
  return t;
}
static std::string Prop(const char* n) {
  char b[32768] = {};
  g_api->get_property(g_session, n, b, sizeof(b));
  return std::string(b);
}

struct Step { int code; const char* label; };

int main(int argc, char** argv) {
  if (argc != 3) return 2;
  SetDllDirectoryW(L"D:\\Rime\\weasel-0.17.4");
  auto dll = LoadLibraryW(L"D:\\Rime\\weasel-0.17.4\\rime.dll");
  if (!dll) return 3;
  g_api = reinterpret_cast<RimeApi*(*)()>(GetProcAddress(dll, "rime_get_api"))();
  RIME_STRUCT(RimeTraits, traits);
  traits.shared_data_dir = "D:\\RimeUser\\build";
  traits.prebuilt_data_dir = "D:\\RimeUser\\build";
  traits.user_data_dir = argv[1];
  traits.app_name = "rime.kpcase";
  traits.log_dir = argv[1];
  traits.min_log_level = 3;
  g_api->setup(&traits);
  g_api->initialize(&traits);
  g_session = g_api->create_session();
  if (!g_session || !g_api->select_schema(g_session, "rime_ice")) return 4;

  std::string name = argv[2];
  std::vector<Step> steps;
  bool ascii_mode = false, ascii_punct = false, full_shape = false;

  // 全量主键盘标点:无候选框时应直接上屏配置形态
  static const struct { const char* name; int code; } kPunct[] = {
    {"p_exclam", '!'}, {"p_quotedbl", '"'}, {"p_numbersign", '#'},
    {"p_dollar", '$'}, {"p_percent", '%'}, {"p_ampersand", '&'},
    {"p_parenleft", '('}, {"p_parenright", ')'}, {"p_asterisk", '*'},
    {"p_plus", '+'}, {"p_comma", ','}, {"p_minus", '-'},
    {"p_period", '.'}, {"p_slash", '/'}, {"p_colon", ':'},
    {"p_semicolon", ';'}, {"p_less", '<'}, {"p_equal", '='},
    {"p_greater", '>'}, {"p_question", '?'}, {"p_at", '@'},
    {"p_bracketleft", '['}, {"p_backslash", '\\'}, {"p_bracketright", ']'},
    {"p_asciicircum", '^'}, {"p_underscore", '_'}, {"p_grave", '`'},
    {"p_braceleft", '{'}, {"p_bar", '|'}, {"p_braceright", '}'},
    {"p_asciitilde", '~'},
  };

  if (name == "comma")            steps = {{',', "comma"}};
  else if (name == "period")      steps = {{'.', "period"}};
  else if (name == "kpcomma")     steps = {{0xffb4, "KP_4"}, {',', "comma"}};
  else if (name == "kpperiod")    steps = {{0xffb4, "KP_4"}, {'.', "period"}};
  else if (name == "kpdec45")     steps = {{0xffb4, "KP_4"}, {0xffae, "KP_Decimal"}, {0xffb5, "KP_5"}};
  else if (name == "kpmain45")    steps = {{0xffb4, "KP_4"}, {'.', "period"}, {0xffb5, "KP_5"}};
  else if (name == "kpthen_comma") steps = {{0xffb4, "KP_4"}, {0xffb4, "KP_4"}, {',', "comma"}};
  else if (name == "letters")     steps = {{'n', "n"}, {'i', "i"}, {',', "comma"}};
  else if (name == "letters_digit_punct") steps = {{'n', "n"}, {0xffb4, "KP_4"}, {'.', "period"}};
  // 新规则：中文模式下主键盘标点一律中文，数字后面也不例外。
  // 需要 ASCII 小数点时用小键盘 KP_Decimal。
  else if (name == "main_192")    steps = {{'1', "1"}, {'9', "9"}, {'2', "2"}, {'.', "period"}, {'1', "1"}};
  else if (name == "main_123c")   steps = {{'1', "1"}, {'2', "2"}, {'3', "3"}, {',', "comma"}, {'4', "4"}};
  else if (name == "main_colon")  steps = {{'1', "1"}, {'2', "2"}, {':', "colon"}, {'3', "3"}};
  else if (name == "kpdec_alone") steps = {{0xffae, "KP_Decimal"}};
  else if (name == "main_dec_ascii") steps = {{'1', "1"}, {'.', "period"}, {'4', "4"}};
  else if (name == "mixed_digit_punct") steps = {{'n', "n"}, {0xffb4, "KP_4"}, {'.', "period"}};
  else if (name == "mixed_digit_punct_commit") steps = {{'n', "n"}, {0xffb4, "KP_4"}, {'.', "period"}, {0xff0d, "Enter"}};
  else if (name == "ascii_en")  { steps = {{',', "comma"}}; ascii_mode = true; }
  else if (name == "asciipunct"){ steps = {{',', "comma"}}; ascii_punct = true; }
  else if (name == "fullshape") { steps = {{',', "comma"}, {'.', "period"}}; }
  else if (name.rfind("fs_", 0) == 0) {
    full_shape = true;
    for (const auto& p : kPunct) {
      if (name == std::string("fs_") + (p.name + 2)) { steps = {{p.code, name.c_str() + 3}}; break; }
    }
  }
  else if (name.rfind("p_", 0) == 0) {
    for (const auto& p : kPunct) {
      if (name == p.name) { steps = {{p.code, name.c_str() + 2}}; break; }
    }
  }
  else { std::printf("unknown case\n"); return 5; }

  g_api->clear_composition(g_session);
  g_api->set_option(g_session, "ascii_mode", ascii_mode);
  g_api->set_option(g_session, "ascii_punct", ascii_punct);
  if (full_shape || name == "fullshape") g_api->set_option(g_session, "full_shape", true);
  Commit();

  std::printf("[%s] ascii_mode=%d ascii_punct=%d\n", name.c_str(), ascii_mode?1:0, ascii_punct?1:0);
  for (const auto& s : steps) {
    bool eaten = g_api->process_key(g_session, s.code, 0) != 0;
    std::printf("  %-12s eaten=%d commit='%s' input='%s' suffix='%s'\n",
                s.label, eaten?1:0, Commit().c_str(), Input().c_str(),
                Prop("weasel_keypad_suffix").c_str());
  }
  g_api->destroy_session(g_session);
  g_api->finalize();
  return 0;
}

