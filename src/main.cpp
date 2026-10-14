#include "codec.h"
#include "commands.h"
#include "parser.h"
#include <lcf/lmu/reader.h>
#include <lcf/ldb/reader.h>
#include <lcf/rpg/map.h>
#include <lcf/rpg/database.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <stdexcept>
#include <filesystem>

namespace fs = std::filesystem;
using namespace rm2k;

// ──────────────────────────────────────────────────────────────────────────────
// ヘルプ
// ──────────────────────────────────────────────────────────────────────────────

static void print_usage(const char* prog) {
    std::cerr <<
R"(rm2kscript — RPGツクール2000/2003 イベントスクリプトツール

使い方:
  )" << prog << R"( decode  <Map0001.lmu>           [-o output.rs2k]
  )" << prog << R"( encode  <script.rs2k> <Map.lmu>  [-o output.lmu]
  )" << prog << R"( decode-db <RPG_RT.ldb>           [-o output.rs2k]
  )" << prog << R"( encode-db <script.rs2k> <RPG_RT.ldb> [-o output.ldb]

コマンド:
  decode    LMUマップファイルを独自言語テキストに変換
  encode    テキストをLMUマップファイルに書き込む
  decode-db LDBデータベースのコモンイベントをテキストに変換
  encode-db テキストをLDBデータベースのコモンイベントに書き込む

オプション:
  -o <file>  出力先ファイル (省略時: stdout / 入力ファイルを上書き)
  -e <enc>   文字エンコーディング (デフォルト: auto)

独自言語仕様:
  @event N  @page M trigger=action layer=same
  {
      ShowMessage("こんにちは！");
      if (switch[1] == ON) {
          ControlVars(var[1] = 100);
          Teleport(map=2, x=5, y=3);
      } else {
          ShowMessage("フラグが立っていません");
      }
      Wait(frames=30);
      PlayBGM("battle", volume=80, tempo=100, balance=50);
  }
)" << std::endl;
}

// ──────────────────────────────────────────────────────────────────────────────
// ファイル読み込みユーティリティ
// ──────────────────────────────────────────────────────────────────────────────

static std::string read_text_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open: " + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void write_text_file(const std::string& path, const std::string& content) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot write: " + path);
    f << content;
}

// ──────────────────────────────────────────────────────────────────────────────
// サブコマンド
// ──────────────────────────────────────────────────────────────────────────────

static int cmd_decode(const std::string& lmu_path,
                      const std::string& out_path,
                      const std::string& encoding) {
    auto map = lcf::LMU_Reader::Load(lmu_path, encoding);
    if (!map) {
        std::cerr << "Error: failed to load " << lmu_path << std::endl;
        return 1;
    }
    Decoder dec;
    std::string text = dec.decode_map(*map, encoding);

    if (out_path.empty() || out_path == "-") {
        std::cout << text;
    } else {
        write_text_file(out_path, text);
        std::cerr << "Written to " << out_path << std::endl;
    }
    return 0;
}

static int cmd_encode(const std::string& script_path,
                      const std::string& lmu_path,
                      const std::string& out_path,
                      const std::string& encoding) {
    // 元のLMUを読み込む
    auto map = lcf::LMU_Reader::Load(lmu_path, encoding);
    if (!map) {
        std::cerr << "Error: failed to load " << lmu_path << std::endl;
        return 1;
    }
    std::string source = read_text_file(script_path);
    Encoder enc;
    try {
        enc.encode_to_map(source, *map, encoding);
    } catch (const ParseError& e) {
        std::cerr << "Parse error at line " << e.loc().line
                  << " col " << e.loc().col << ": " << e.what() << std::endl;
        return 1;
    } catch (const LexError& e) {
        std::cerr << "Lex error at line " << e.loc().line
                  << " col " << e.loc().col << ": " << e.what() << std::endl;
        return 1;
    }

    std::string dest = out_path.empty() ? lmu_path : out_path;
    // エンジンバージョン自動判定 (2000形式)
    bool ok = lcf::LMU_Reader::Save(dest, *map, lcf::EngineVersion::e2k, encoding);
    if (!ok) {
        std::cerr << "Error: failed to save " << dest << std::endl;
        return 1;
    }
    std::cerr << "Saved to " << dest << std::endl;
    return 0;
}

static int cmd_decode_db(const std::string& ldb_path,
                          const std::string& out_path,
                          const std::string& encoding) {
    auto db = lcf::LDB_Reader::Load(ldb_path, encoding);
    if (!db) {
        std::cerr << "Error: failed to load " << ldb_path << std::endl;
        return 1;
    }
    Decoder dec;
    std::string text = dec.decode_common_events(*db, encoding);

    if (out_path.empty() || out_path == "-") {
        std::cout << text;
    } else {
        write_text_file(out_path, text);
        std::cerr << "Written to " << out_path << std::endl;
    }
    return 0;
}

static int cmd_encode_db(const std::string& script_path,
                          const std::string& ldb_path,
                          const std::string& out_path,
                          const std::string& encoding) {
    auto db = lcf::LDB_Reader::Load(ldb_path, encoding);
    if (!db) {
        std::cerr << "Error: failed to load " << ldb_path << std::endl;
        return 1;
    }
    std::string source = read_text_file(script_path);
    Encoder enc;
    try {
        enc.encode_to_database(source, *db, encoding);
    } catch (const ParseError& e) {
        std::cerr << "Parse error at line " << e.loc().line
                  << " col " << e.loc().col << ": " << e.what() << std::endl;
        return 1;
    } catch (const LexError& e) {
        std::cerr << "Lex error at line " << e.loc().line
                  << " col " << e.loc().col << ": " << e.what() << std::endl;
        return 1;
    }

    std::string dest = out_path.empty() ? ldb_path : out_path;
    bool ok = lcf::LDB_Reader::Save(dest, *db, encoding);
    if (!ok) {
        std::cerr << "Error: failed to save " << dest << std::endl;
        return 1;
    }
    std::cerr << "Saved to " << dest << std::endl;
    return 0;
}

// ──────────────────────────────────────────────────────────────────────────────
// main
// ──────────────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    if (argc < 2) { print_usage(argv[0]); return 1; }

    std::string cmd    = argv[1];
    std::string out_path;
    std::string encoding = "932"; // デフォルト: Shift-JIS（日本語版ツクール2000/2003）

    // オプション解析
    std::vector<std::string> positional;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) {
            out_path = argv[++i];
        } else if (a == "-e" && i + 1 < argc) {
            encoding = argv[++i];
        } else {
            positional.push_back(a);
        }
    }

    try {
        if (cmd == "decode") {
            if (positional.empty()) { print_usage(argv[0]); return 1; }
            return cmd_decode(positional[0], out_path, encoding);
        } else if (cmd == "encode") {
            if (positional.size() < 2) { print_usage(argv[0]); return 1; }
            return cmd_encode(positional[0], positional[1], out_path, encoding);
        } else if (cmd == "decode-db") {
            if (positional.empty()) { print_usage(argv[0]); return 1; }
            return cmd_decode_db(positional[0], out_path, encoding);
        } else if (cmd == "encode-db") {
            if (positional.size() < 2) { print_usage(argv[0]); return 1; }
            return cmd_encode_db(positional[0], positional[1], out_path, encoding);
        } else {
            std::cerr << "Unknown command: " << cmd << std::endl;
            print_usage(argv[0]);
            return 1;
        }
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << std::endl;
        return 1;
    }
}
