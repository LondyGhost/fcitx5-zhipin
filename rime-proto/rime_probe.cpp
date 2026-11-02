// rime_probe.cpp — 无头验证 Rime 方案：逐键喂入并打印 preedit / 候选 / 上屏结果。
//
// 用法：rime_probe <user_data_dir> <schema_id> <key>...
//   key 支持：单个字母、space、Return、BackSpace、Escape、digit 0-9、Shift
// 首次运行会部署（可能耗时几分钟），之后复用 build 缓存。
#include <rime_api.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace {

constexpr int kMaskShift = 1 << 0;

int keycodeOf(const std::string& k) {
  if (k.size() == 1) return static_cast<unsigned char>(k[0]);
  if (k == "space") return 0x20;
  if (k == "Return" || k == "enter") return 0xff0d;
  if (k == "BackSpace") return 0xff08;
  if (k == "Escape") return 0xff1b;
  if (k == "Tab") return 0xff09;
  if (k == "Shift") return 0xffe1;  // Shift_L
  if (k == "apostrophe") return 0x27;
  return -1;
}

void printState(RimeApi* api, RimeSessionId s, const std::string& key) {
  RIME_STRUCT(RimeContext, ctx);
  if (!api->get_context(s, &ctx)) {
    std::printf("  %-10s | <no context>\n", key.c_str());
    return;
  }
  const char* preedit =
      (ctx.composition.preedit != nullptr) ? ctx.composition.preedit : "";
  std::printf("  %-10s | preedit=\"%s\" cands=%d sel=%d\n", key.c_str(), preedit,
              ctx.menu.num_candidates, ctx.menu.highlighted_candidate_index);
  for (int i = 0; i < ctx.menu.num_candidates && i < 9; ++i) {
    std::printf("             |   %d. %s\n", i + 1, ctx.menu.candidates[i].text);
  }
  api->free_context(&ctx);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr,
                 "usage: %s <user_data_dir> <schema_id> <key>...\n", argv[0]);
    return 2;
  }
  const char* user_dir = argv[1];
  const char* schema = argv[2];

  RimeApi* api = rime_get_api();
  if (api == nullptr) {
    std::fprintf(stderr, "rime_get_api failed\n");
    return 1;
  }

  RIME_STRUCT(RimeTraits, traits);
  traits.shared_data_dir = "/usr/share/rime-data";
  traits.user_data_dir = user_dir;
  traits.log_dir = "/tmp";
  traits.distribution_name = "Rime";
  traits.distribution_code_name = "rime-abc-probe";
  traits.distribution_version = "0.1";
  traits.app_name = "rime.abc_probe";
  traits.min_log_level = 2;  // 只报 warning 以上

  api->setup(&traits);
  api->initialize(&traits);
  std::printf("deploying...\n");
  std::fflush(stdout);
  api->start_maintenance(True);
  api->join_maintenance_thread();
  std::printf("deploy done.\n");

  RimeSessionId session = api->create_session();
  if (session == 0) {
    std::fprintf(stderr, "create_session failed\n");
    return 1;
  }
  if (!api->select_schema(session, schema)) {
    std::fprintf(stderr, "select_schema(%s) failed\n", schema);
    return 1;
  }
  std::printf("schema=%s session ok\n", schema);

  for (int i = 3; i < argc; ++i) {
    const std::string key = argv[i];
    int mask = 0;
    std::string k = key;
    if (k.size() > 6 && k.compare(0, 6, "Shift+") == 0) {
      mask = kMaskShift;
      k = k.substr(6);
    }
    const int code = keycodeOf(k);
    if (code < 0) {
      std::printf("  %-10s | <unknown key>\n", key.c_str());
      continue;
    }
    const Bool ok = api->process_key(session, code, mask);
    if (!ok) {
      std::printf("  %-10s | (not accepted by rime)\n", key.c_str());
    }
    RIME_STRUCT(RimeCommit, commit);
    if (api->get_commit(session, &commit)) {
      std::printf("  %-10s | >>> COMMIT \"%s\"\n", key.c_str(),
                  commit.text ? commit.text : "");
      api->free_commit(&commit);
    }
    printState(api, session, key);
  }

  api->destroy_session(session);
  api->finalize();
  return 0;
}
