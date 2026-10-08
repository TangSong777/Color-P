// Runs against the installed librime DLL but an isolated user directory.
// No UI automation or writes to the user's dictionary.
#include <windows.h>
#include <cstdio>
#include <string>
#include "rime_api.h"

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  SetDllDirectoryW(L"D:\\Rime\\weasel-0.17.4");
  auto dll = LoadLibraryW(L"D:\\Rime\\weasel-0.17.4\\rime.dll");
  if (!dll) return 3;
  auto api = reinterpret_cast<RimeApi*(*)()>(GetProcAddress(dll, "rime_get_api"))();
  RIME_STRUCT(RimeTraits, traits);
  traits.shared_data_dir = "D:\\RimeUser\\build";
  traits.prebuilt_data_dir = "D:\\RimeUser\\build";
  traits.user_data_dir = argv[1];
  traits.app_name = "rime.native_regression";
  traits.log_dir = argv[1]; traits.min_log_level = 2;
  api->setup(&traits); api->initialize(&traits);
  auto session = api->create_session();
  if (!session || !api->select_schema(session, "rime_ice")) return 4;
  int checks = 0, failures = 0;
  auto check = [&](bool ok, const char* name) {
    ++checks; if (!ok) { ++failures; std::printf("FAIL: %s\n", name); }
  };
  auto property = [&](const char* name) {
    char buffer[32768] = {};
    api->get_property(session, name, buffer, sizeof(buffer));
    return std::string(buffer);
  };
  auto input = [&]() { const char* p = api->get_input(session); return std::string(p ? p : ""); };
  auto key = [&](int code, int mask = 0) { return api->process_key(session, code, mask); };
  auto type = [&](const char* text) { for (; *text; ++text) key(*text); };
  auto commit = [&]() {
    RIME_STRUCT(RimeCommit, c); std::string text;
    if (api->get_commit(session, &c)) { text = c.text ? c.text : ""; api->free_commit(&c); }
    return text;
  };
  auto reset = [&]() { api->clear_composition(session); api->set_option(session, "ascii_mode", false); commit(); };
  auto page = [&]() {
    RIME_STRUCT(RimeContext, c); int value = -1;
    if (api->get_context(session, &c)) { value = c.menu.page_no; api->free_context(&c); }
    return value;
  };
  reset(); type("ni"); key(0xffb4); type("hao");
  check(input() == "ni" && property("weasel_keypad_suffix") == "4hao", "native processor order / mixed suffix");
  key(0xff0d); check(commit() == "ni4hao", "Enter commits complete raw mixed input");
  reset(); type("ni"); key(0xffb4); type("hao");
  key(0xffe1); key(0xffe1, 1 << 30);
  check(commit() == "ni4hao", "Shift must not drop deferred suffix");
  check(api->get_option(session,"ascii_mode"), "Shift switches to English");
  reset(); type("ni"); key(0xffb4); key('A', 2);
  check(property("weasel_keypad_suffix") == "4A", "Caps Lock letter stays after keypad digit");
  key(0xff08, 2); check(property("weasel_keypad_suffix") == "4", "Caps Lock Backspace edits suffix");
  reset(); type("ni"); key(0xffb4); type("hao"); key('1');
  check(commit().empty(), "first selection stays composed");
  check(property("weasel_keypad_prefix").find('4') != std::string::npos && input() == "hao", "selection advances across literal");
  key(0xff50); check(commit().empty() && input() == "ni", "Home reopens confirmed prefix");
  key(0xff0d); check(commit() == "ni4hao", "raw Enter after reopening");
  reset(); type("ni"); key(0xffb4); type("hao"); key('1'); key('1');
  auto selected = commit();
  check(!selected.empty() && selected.find('4') != std::string::npos && selected.find("hao") == std::string::npos, "final native selection and learning emit one complete commit");
  check(input().empty(), "final selection clears context");
  reset(); type("n"); key(0xffb4); key(0xff50); key(0xffff);
  check(commit().empty() && input() == "4", "deleting last pinyin never auto-commits digit");
  key(0xffff); key(0xff0d); check(commit().empty(), "delete exposed digit then Enter");
  reset(); type("ni"); key(0xffb4); key('='); check(page() == 1, "top-row equal pages mixed candidates");
  key('-'); check(page() == 0, "top-row minus pages backwards");
  for (int symbol : {0xffaa,0xffab,0xffad,0xffaf,0xffae}) {
    reset(); type("ni"); key(symbol); type("hao");
    check(input() == "ni", "keypad operator does not alter pinyin lookup");
    key(0xff1b); check(input().empty() && commit().empty() && property("weasel_keypad_suffix").empty(), "Escape cancels all segments");
  }
  reset(); key(0xffb4); check(commit() == "4" && input().empty(), "standalone keypad does not create composition");
  reset(); type("ni"); key(0xffb4); type("hao"); api->clear_composition(session);
  check(property("weasel_keypad_suffix").empty() && property("weasel_keypad_prefix").empty(), "focus cancellation removes virtual state");
  for (char symbol : std::string("/\\*+-=,.;:!?[]{}()@#$%^&_`~\"'")) {
    reset(); api->set_option(session,"ascii_punct",true); key(0xffb4); check(commit() == "4", "keypad digit immediately commits");
    key(symbol);
    check(commit() == std::string(1, symbol) && input().empty(), "symbol after digit never creates candidate composition");
    key('n'); check(input() == "n", "letter starts composition after standalone symbol");
  }
  reset(); type("ni"); key(0xffb4); key('/');
  check(input() == "ni" && property("weasel_keypad_suffix") == "4/" && commit().empty(), "symbol stays deferred when letters already compose");
  api->set_option(session,"ascii_punct",false);
  for (auto item : {std::pair<int,const char*>(',',"，"), {'.',"。"}, {'!',"！"}, {'?',"？"}, {'[',"【"}, {']',"】"}}) {
    reset(); key(0xffb4); commit(); key(item.first);
    check(commit()==item.second && input().empty(), "Chinese punctuation after digit commits without menu");
  }
  reset(); type("ni"); key(','); type("hao");
  check(property("weasel_keypad_suffix")=="，hao", "Chinese punctuation stays in mixed preedit");
  key(0xff51); key(0xff51); key(0xff51); key(0xff08);
  check(input()=="nihao" && property("weasel_keypad_suffix").empty(), "Backspace removes entire UTF8 punctuation and rejoins spelling");
  reset(); type("ni"); key(','); type("hao"); key(0xff0d);
  check(commit()=="ni，hao", "Enter preserves letters and entered Chinese punctuation");
  reset(); type("ni"); key(','); type("hao"); key('1'); key('1');
  check(commit().find("，")!=std::string::npos, "selection preserves Chinese punctuation");
  reset(); key('"'); check(commit()=="“", "opening quote");
  key('"'); check(commit()=="”", "closing quote");
  reset(); key(0xffae); check(commit()==".", "keypad decimal stays ASCII in Chinese mode");
  api->set_option(session,"ascii_mode",true);
  check(!key(','), "English punctuation passes through to host");
  api->destroy_session(session); api->finalize();
  std::printf("Native Rime: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
