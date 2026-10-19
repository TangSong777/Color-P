// 字典覆盖度功能探针：用真实 librime 验证某个字能否被输入出来。
//
// 为什么需要它：Rime 的 .table.bin 是 Marisa 压缩结构，明文搜索不可靠，
// 无法用"文件里有没有这个字"判断。唯一可靠的验证是走一遍真实的输入链路：
// 选定方案 -> 模拟键入拼音 -> 遍历候选 -> 看目标字在不在。
//
// 用法：DictCoverageProbe.exe <user_data_dir>
//   该目录需已包含编译好的 build\*.bin（由 WeaselDeployer 部署生成）。
#include <rime_api.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct Case {
  const char* pinyin;
  const char* expect;
  const char* note;
};

// 目标字应只来自「大字表」cn_dicts/41448；鑫 是 8105 里就有的对照项。
const Case kCases[] = {
    {"da", "\xE9\xBE\x98", "龘"},
    {"bing", "\xE9\x9D\x90", "靐"},
    {"cu", "\xE9\xBA\xA4", "麤"},
    {"xian", "\xE9\xB1\xBB", "鱻"},
    {"biao", "\xE7\x8C\x8B", "猋"},
    {"xin", "\xE9\x91\xAB", "鑫(对照)"},
};

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const std::string& what) {
  ++g_checks;
  if (!ok) {
    ++g_failures;
    std::printf("FAIL  %s\n", what.c_str());
  }
}

// 在一个新会话里键入 pinyin，返回目标字是否出现在候选列表里。
bool FeedsToChar(RimeApi* api, const char* pinyin, const char* expect) {
  const RimeSessionId sid = api->create_session();
  if (!sid) {
    std::printf("  ! create_session failed\n");
    return false;
  }
  api->select_schema(sid, "rime_ice");
  api->simulate_key_sequence(sid, pinyin);

  bool found = false;
  RimeCandidateListIterator it;
  std::memset(&it, 0, sizeof(it));
  if (api->candidate_list_begin(sid, &it)) {
    while (api->candidate_list_next(&it)) {
      if (it.candidate.text && std::strcmp(it.candidate.text, expect) == 0) {
        found = true;
        break;
      }
    }
    api->candidate_list_end(&it);
  }
  api->destroy_session(sid);
  return found;
}

}  // namespace

// 用法：DictCoverageProbe.exe <user_data_dir> [shared_data_dir] [--deploy]
//                                              [--query <keys> [--limit N]]
//   user_data_dir   含编译好的 build\*.bin（由 WeaselDeployer 部署生成）
//   shared_data_dir 安装目录下的 data\（提供 opencc 等共享资源；可省略）
//   --deploy        先在该目录重新部署词库再测（用于对照实验）
//   --query <keys>  只打印这串输入得到的候选，用于人工对比（不改判定逻辑）
//   --limit N       配合 --query，最多打印 N 条（默认 20）
int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf("usage: %s <user_data_dir> [shared_data_dir] [--deploy] "
                "[--query <keys>] [--limit N]\n", argv[0]);
    return 2;
  }
  const char* dir = argv[1];
  const char* shared = dir;
  bool deploy = false;
  const char* query = nullptr;
  int limit = 20;
  for (int i = 2; i < argc; ++i) {
    if (std::strcmp(argv[i], "--deploy") == 0) { deploy = true; continue; }
    if (std::strcmp(argv[i], "--query") == 0 && i + 1 < argc) { query = argv[++i]; continue; }
    if (std::strcmp(argv[i], "--limit") == 0 && i + 1 < argc) { limit = std::atoi(argv[++i]); continue; }
    shared = argv[i];
  }

  RimeApi* api = rime_get_api();
  if (!api) {
    std::printf("rime_get_api() failed\n");
    return 2;
  }

  RIME_STRUCT(RimeTraits, traits);
  traits.shared_data_dir = shared;
  traits.user_data_dir = dir;
  traits.app_name = "rime.dict_probe";
  traits.distribution_name = "DictProbe";
  traits.distribution_code_name = "dict_probe";
  traits.distribution_version = "0.0.1";
  if (api->setup) api->setup(&traits);
  api->initialize(&traits);
  // 用已编译好的 build\*.bin，不重新部署：maintenance 传 false 即只做校验。
  // --deploy 时才真正重建（对照实验用）。
  if (api->start_maintenance) api->start_maintenance(deploy ? True : False);
  if (api->join_maintenance_thread) api->join_maintenance_thread();

  std::printf("user_data_dir = %s\n", dir);
  {
    char schema[64] = {0};
    const RimeSessionId probe = api->create_session();
    api->select_schema(probe, "rime_ice");
    const Bool have = api->get_current_schema(probe, schema, sizeof(schema));
    std::printf("current schema = %s\n", have ? schema : "(null)");
    api->destroy_session(probe);
  }
  if (query) {
    // --query 模式：只把候选打印出来，供人工对比（例如确认英文单词是否还在）。
    const RimeSessionId sid = api->create_session();
    api->select_schema(sid, "rime_ice");
    api->simulate_key_sequence(sid, query);
    std::printf("\nquery \"%s\" candidates:\n", query);
    int n = 0;
    RimeCandidateListIterator it;
    std::memset(&it, 0, sizeof(it));
    if (api->candidate_list_begin(sid, &it)) {
      while (n < limit && api->candidate_list_next(&it)) {
        std::printf("  %2d. %s%s%s\n", ++n,
                    it.candidate.text ? it.candidate.text : "(null)",
                    it.candidate.comment ? "  " : "",
                    it.candidate.comment ? it.candidate.comment : "");
      }
      api->candidate_list_end(&it);
    }
    if (n == 0) std::printf("  (no candidates)\n");
    api->destroy_session(sid);
    api->finalize();
    std::printf("\nquery total: %d\n", n);
    return 0;
  }

  std::printf("\n%-8s %-10s %s\n", "pinyin", "char", "result");
  for (const auto& c : kCases) {
    const bool ok = FeedsToChar(api, c.pinyin, c.expect);
    std::printf("%-8s %-10s %s\n", c.pinyin, c.note, ok ? "FOUND" : "MISSING");
    Check(ok, std::string("candidate for \"") + c.pinyin + "\" contains " + c.note);
  }

  api->finalize();
  std::printf("\nDict coverage: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
