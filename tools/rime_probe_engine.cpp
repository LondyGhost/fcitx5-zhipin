// rime_probe_engine.cpp — 直接驱动 abc::EngineSession（librime 后端）验证行为。
//
// 用法: rime_probe_engine <rime_user_dir> [word...]
// 每个 word 是一串字母；随后自动按 space / space 验证「出词 → 上屏第一个」。
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "abc/engine.h"
#include "rime_engine.h"

namespace {

abc::KeyCommand letter(char c) {
  abc::KeyCommand cmd;
  cmd.code = abc::KeyCode::kCharacter;
  cmd.unicode = static_cast<char32_t>(static_cast<unsigned char>(c));
  return cmd;
}

abc::KeyCommand special(abc::KeyCode code) {
  abc::KeyCommand cmd;
  cmd.code = code;
  return cmd;
}

abc::KeyCommand punct(char c) {
  abc::KeyCommand cmd;
  cmd.code = abc::KeyCode::kPunctuation;
  cmd.unicode = static_cast<char32_t>(static_cast<unsigned char>(c));
  return cmd;
}

void dump(const abc::EngineResult& r, const char* tag) {
  std::printf("  %-12s preedit=\"%s\" cands=%zu sel=%d page=%u/%u\n", tag,
              r.preedit.c_str(), r.candidates.size(), r.highlight, r.page,
              r.pageCount);
  for (std::size_t i = 0; i < r.candidates.size(); ++i) {
    std::printf("                %zu. %s%s\n", i + 1, r.candidates[i].text.c_str(),
                i + 1 == static_cast<std::size_t>(r.highlight + 1) ? "  <<" : "");
  }
  for (const auto& e : r.effects) {
    if (e.kind == abc::Effect::Kind::kCommitText) {
      std::printf("                >>> COMMIT \"%s\"\n", e.text.c_str());
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <rime_user_dir> [word...]\n", argv[0]);
    return 2;
  }
  abc::RimeOptions opts;
  opts.sharedDataDir = "/usr/share/rime-data";
  opts.userDataDir = argv[1];
  opts.logDir = "/tmp";
  opts.schemaId = "abc";

  if (!abc::initializeRime(opts)) {
    std::fprintf(stderr, "initializeRime failed\n");
    return 1;
  }
  std::printf("rime ready\n");

  abc::EngineConfig config;
  config.pageSize = 9;
  auto session = abc::createRimeEngineSession(config);
  if (session == nullptr) {
    std::fprintf(stderr, "createRimeEngineSession failed\n");
    return 1;
  }

  for (int a = 2; a < argc; ++a) {
    if (std::strcmp(argv[a], "-q") == 0) continue;  // 快速开关，不是词
    // 参数格式：word[:步骤...]。word 直接是拼音字母；"-"=不打字母只按空格；
    // "_"=完全从零开始。步骤：S=空格 R=回车 E=Esc B=退格 p=下一页 b=上一页
    //       D<n>=数字 n；P<ch>=标点；小写字母串=追加输入
    std::string spec = argv[a];
    std::string word = spec;
    std::vector<std::string> steps;
    const std::size_t colon = spec.find(':');
    if (colon != std::string::npos) {
      word = spec.substr(0, colon);
      std::size_t pos = colon + 1;
      while (pos <= spec.size()) {
        const std::size_t next = spec.find(':', pos);
        const std::string part =
            spec.substr(pos, next == std::string::npos ? std::string::npos
                                                       : next - pos);
        if (!part.empty()) steps.push_back(part);
        if (next == std::string::npos) break;
        pos = next + 1;
      }
    }
    std::printf("\n########## %s ##########\n", spec.c_str());
    if (word != "_") {
      for (const char c : word) {
        dump(session->handle(letter(c)), (std::string("key ") + c).c_str());
      }
    }
    // word 为 "_" 时不自动按空格；步骤里自己写 S。
    if (word == "-") {
      dump(session->handle(special(abc::KeyCode::kSpace)), "space#1");
    }
    for (const std::string& step : steps) {
      abc::KeyCommand cmd;
      bool isPunct = false;
      char pc = 0;
      if (step == "S") {
        cmd.code = abc::KeyCode::kSpace;
      } else if (step == "R") {
        cmd.code = abc::KeyCode::kEnter;
      } else if (step == "E") {
        cmd.code = abc::KeyCode::kEscape;
      } else if (step == "B") {
        cmd.code = abc::KeyCode::kBackspace;
      } else if (step == "p") {
        cmd.code = abc::KeyCode::kPageDown;
      } else if (step == "b") {
        cmd.code = abc::KeyCode::kPageUp;
      } else if (step[0] == 'D' && step.size() >= 2) {
        cmd.code = abc::KeyCode::kDigit;
        cmd.digit = static_cast<uint8_t>(step[1] - '0');
      } else if (step[0] == 'P' && step.size() >= 2) {
        isPunct = true;
        pc = step[1];
      } else {
        // 步骤里的普通字母：追加进缓冲
        for (const char c : step) {
          dump(session->handle(letter(c)), (std::string("key ") + c).c_str());
        }
        continue;
      }
      if (isPunct) {
        dump(session->handle(punct(pc)), (std::string("punct ") + pc).c_str());
      } else {
        dump(session->handle(cmd), ("step " + step).c_str());
      }
    }
    if (steps.empty()) {
      dump(session->handle(special(abc::KeyCode::kSpace)), "space#2");
    }
  }

  // 用 -q 跳过内置固定用例（只看命令行给的序列时用）。
  bool quick = false;
  for (int a = 2; a < argc; ++a) {
    if (std::strcmp(argv[a], "-q") == 0) quick = true;
  }
  if (quick) {
    session.reset();
    return 0;
  }

  std::printf("\n########## 回车原样上屏 ##########\n");
  for (const char c : std::string("nihao")) {
    session->handle(letter(c));
  }
  dump(session->handle(special(abc::KeyCode::kEnter)), "Return");

  std::printf("\n########## 标点：中文态全角 ##########\n");
  dump(session->handle(punct(',')), "punct ,");
  dump(session->handle(punct('.')), "punct .");
  dump(session->handle(punct('"')), "punct \" #1");
  dump(session->handle(punct('"')), "punct \" #2");
  dump(session->handle(punct('?')), "punct ?");

  std::printf("\n########## 标点：拼音中途先提交再出标点 ##########\n");
  for (const char c : std::string("ni")) {
    session->handle(letter(c));
  }
  dump(session->handle(punct(',')), "ni + punct ,");

  std::printf("\n########## 分阶段选字后直接回车 ##########\n");
  for (const char c : std::string("ceshi")) {
    session->handle(letter(c));
  }
  session->handle(special(abc::KeyCode::kSpace));
  dump(session->handle(special(abc::KeyCode::kDigit)), "digit-nop");
  // 选“测”（假定它在第1个位置的话就退格重选；直接选5号测后回车看行为）
  dump(session->handle([&] {
         abc::KeyCommand cmd;
         cmd.code = abc::KeyCode::kDigit;
         cmd.digit = 5;
         return cmd;
       }()),
       "digit 5");
  dump(session->handle(special(abc::KeyCode::kEnter)), "Return after stage");

  session.reset();
  return 0;
}
