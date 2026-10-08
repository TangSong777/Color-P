// Isolated librime probe for the punctuation behaviour a user actually gets.
// Uses the installed rime.dll against D:\RimeUser\build as shared data and a
// throwaway user directory, so the user's own dictionary is never written.
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
  traits.app_name = "rime.punct_probe";
  traits.log_dir = argv[1];
  traits.min_log_level = 3;
  api->setup(&traits);
  api->initialize(&traits);
  auto session = api->create_session();
  if (!session || !api->select_schema(session, "rime_ice")) return 4;

  auto commit = [&]() {
    RIME_STRUCT(RimeCommit, c);
    std::string text;
    if (api->get_commit(session, &c)) {
      text = c.text ? c.text : "";
      api->free_commit(&c);
    }
    return text;
  };
  auto press = [&](const char* keys) {
    std::string out;
    for (const char* p = keys; *p; ++p) {
      api->process_key(session, *p, 0);
      out += commit();
    }
    return out;
  };
  auto reset = [&]() {
    api->clear_composition(session);
    api->set_option(session, "ascii_punct", 0);
    api->set_option(session, "full_shape", 0);
    commit();
  };

  std::printf("--- Chinese, half shape, ascii_punct OFF ---\n");
  reset();
  std::printf("keys 1 2 3 . 4  ->  \"%s\"\n", press("123.4").c_str());
  reset();
  std::printf("keys 1 2 3 , 4  ->  \"%s\"\n", press("123,4").c_str());
  reset();
  std::printf("keys 1 9 2 . 1 6 8 . 1 . 1  ->  \"%s\"\n",
              press("192.168.1.1").c_str());

  std::printf("--- Chinese, half shape, ascii_punct ON (Ctrl+Shift+3) ---\n");
  reset();
  api->set_option(session, "ascii_punct", 1);
  std::printf("keys 1 2 3 . 4  ->  \"%s\"\n", press("123.4").c_str());

  std::printf("--- Chinese, FULL shape, ascii_punct OFF ---\n");
  reset();
  api->set_option(session, "full_shape", 1);
  std::printf("keys 1 2 3 . 4  ->  \"%s\"\n", press("123.4").c_str());
  reset();
  api->set_option(session, "full_shape", 1);
  std::printf("key  .   ->  \"%s\"\n", press(".").c_str());
  reset();
  api->set_option(session, "full_shape", 1);
  std::printf("key  ^   ->  \"%s\"   (config says ……)\n", press("^").c_str());
  reset();
  api->set_option(session, "full_shape", 1);
  std::printf("key  [   ->  \"%s\"   (config says 「 first)\n", press("[").c_str());
  reset();
  api->set_option(session, "full_shape", 1);
  std::printf("key  $   ->  \"%s\"   (config says ￥ first)\n", press("$").c_str());

  std::printf("--- paging keys with a menu open ---\n");
  reset();
  api->process_key(session, 'n', 0); commit();
  api->process_key(session, 'i', 0); commit();
  std::printf("menu open? has_menu assumed. '-' on page 1 -> eaten=%d commit=\"%s\"\n",
              api->process_key(session, '-', 0), commit().c_str());
  std::printf("'=' on page 1 -> eaten=%d commit=\"%s\"\n",
              api->process_key(session, '=', 0), commit().c_str());

  std::printf("--- Tab while composing ---\n");
  reset();
  for (const char* p = "nihao"; *p; ++p) { api->process_key(session, *p, 0); commit(); }
  std::printf("Tab -> eaten=%d\n", api->process_key(session, 0xff09, 0));
  reset();
  std::printf("Tab with no composition -> eaten=%d\n",
              api->process_key(session, 0xff09, 0));

  std::printf("--- bare Shift ---\n");
  reset();
  for (const char* p = "ni"; *p; ++p) { api->process_key(session, *p, 0); commit(); }
  api->process_key(session, 0xffe1, 0);
  std::printf("Shift down then up -> commit=\"%s\" ascii_mode=%d\n",
              commit().c_str(), api->get_option(session, "ascii_mode") ? 1 : 0);

  api->destroy_session(session);
  api->finalize();
  return 0;
}
