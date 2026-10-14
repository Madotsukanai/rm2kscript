#include "codec.h"
#include "commands.h"
#include "nametable.h"
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
#include <algorithm>
#include <vector>

using namespace rm2k;

// ──────────────────────────────────────────────────────────────────────────────
// ヘルプ
// ──────────────────────────────────────────────────────────────────────────────

static void print_usage(const char* prog) {
    std::cerr <<
R"(rm2kscript — RPGツクール2000/2003 イベントスクリプトツール

使い方:
  )" << prog << R"( decode  <Map0001.lmu>           [-o output.rs2k] [-n RPG_RT.ldb]
  )" << prog << R"( encode  <script.rs2k> <Map.lmu>  [-o output.lmu]  [-n RPG_RT.ldb]
  )" << prog << R"( decode-db <RPG_RT.ldb>           [-o output.rs2k] [-n RPG_RT.ldb]
  )" << prog << R"( encode-db <script.rs2k> <RPG_RT.ldb> [-o output.ldb] [-n RPG_RT.ldb]
  )" << prog << R"( decode-project <game_dir> -o <out_dir>  [-n RPG_RT.ldb]
  )" << prog << R"( encode-project <script_dir> <game_dir>  [-n RPG_RT.ldb]

コマンド:
  decode          LMUマップファイルを独自言語テキストに変換
  encode          テキストをLMUマップファイルに書き込む
  decode-db       LDBデータベースのコモンイベントをテキストに変換
  encode-db       テキストをLDBデータベースのコモンイベントに書き込む
  decode-project  ゲームディレクトリの全 .lmu と RPG_RT.ldb を一括デコード
  encode-project  スクリプトディレクトリの全 .rs2k をゲームディレクトリに一括エンコード

オプション:
  -o <file|dir>  出力先ファイルまたはディレクトリ
  -e <enc>       文字エンコーディング (デフォルト: 932 / Shift-JIS)
  -n <ldb>       RPG_RT.ldb からスイッチ/変数名テーブルを読み込み、名前で表記する

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
// NameTable を LDB から構築
// ──────────────────────────────────────────────────────────────────────────────

static NameTable build_name_table(const std::string& ldb_path, const std::string& encoding) {
    auto db = lcf::LDB_Reader::Load(ldb_path, encoding);
    if (!db) throw std::runtime_error("cannot load nametable ldb: " + ldb_path);
    NameTable nt;
    // 重複名チェック: 同じ名前を持つエントリが複数ある場合、名前→IDの逆引きは登録しない
    // (登録すると最初のIDが返り、ラウンドトリップが壊れる)
    std::unordered_map<std::string, int> sw_name_count, var_name_count;
    for (const auto& sw : db->switches) {
        std::string name(sw.name.begin(), sw.name.end());
        if (!name.empty()) sw_name_count[name]++;
    }
    for (const auto& vr : db->variables) {
        std::string name(vr.name.begin(), vr.name.end());
        if (!name.empty()) var_name_count[name]++;
    }
    for (const auto& sw : db->switches) {
        std::string name(sw.name.begin(), sw.name.end());
        if (name.empty()) continue;
        nt.sw_names[sw.ID] = name;
        if (sw_name_count[name] == 1)   // 一意の名前のみ逆引き登録
            nt.sw_ids[name] = sw.ID;
    }
    for (const auto& vr : db->variables) {
        std::string name(vr.name.begin(), vr.name.end());
        if (name.empty()) continue;
        nt.var_names[vr.ID] = name;
        if (var_name_count[name] == 1)  // 一意の名前のみ逆引き登録
            nt.var_ids[name] = vr.ID;
    }
    return nt;
}

// ──────────────────────────────────────────────────────────────────────────────
// サブコマンド
// ──────────────────────────────────────────────────────────────────────────────

static int cmd_decode(const std::string& lmu_path,
                      const std::string& out_path,
                      const std::string& encoding,
                      const NameTable* names) {
    auto map = lcf::LMU_Reader::Load(lmu_path, encoding);
    if (!map) {
        std::cerr << "Error: failed to load " << lmu_path << std::endl;
        return 1;
    }
    Decoder dec;
    dec.set_name_table(names);
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
                      const std::string& encoding,
                      const NameTable* names) {
    // 元のLMUを読み込む
    auto map = lcf::LMU_Reader::Load(lmu_path, encoding);
    if (!map) {
        std::cerr << "Error: failed to load " << lmu_path << std::endl;
        return 1;
    }
    std::string source = read_text_file(script_path);
    Encoder enc;
    enc.set_name_table(names);
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
                          const std::string& encoding,
                          const NameTable* names) {
    auto db = lcf::LDB_Reader::Load(ldb_path, encoding);
    if (!db) {
        std::cerr << "Error: failed to load " << ldb_path << std::endl;
        return 1;
    }
    Decoder dec;
    dec.set_name_table(names);
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
                          const std::string& encoding,
                          const NameTable* names) {
    auto db = lcf::LDB_Reader::Load(ldb_path, encoding);
    if (!db) {
        std::cerr << "Error: failed to load " << ldb_path << std::endl;
        return 1;
    }
    std::string source = read_text_file(script_path);
    Encoder enc;
    enc.set_name_table(names);
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
// decode-project / encode-project
// ──────────────────────────────────────────────────────────────────────────────

namespace fs = std::filesystem;

// ゲームディレクトリの全 .lmu + RPG_RT.ldb をデコードして out_dir に書き出す
// マップ: MapNNNN.lmu → out_dir/MapNNNN.rs2k
// DB   : RPG_RT.ldb  → out_dir/CommonEvents.rs2k
static int cmd_decode_project(const std::string& game_dir,
                               const std::string& out_dir,
                               const std::string& encoding,
                               const NameTable* names) {
    fs::path gdir(game_dir);
    if (!fs::is_directory(gdir)) {
        std::cerr << "Error: not a directory: " << game_dir << std::endl;
        return 1;
    }
    if (out_dir.empty()) {
        std::cerr << "Error: -o <out_dir> is required for decode-project" << std::endl;
        return 1;
    }
    fs::path odir(out_dir);
    fs::create_directories(odir);

    // .lmu ファイルを収集して番号順にソート
    std::vector<fs::path> lmu_files;
    for (const auto& entry : fs::directory_iterator(gdir)) {
        if (!entry.is_regular_file()) continue;
        auto fn = entry.path().filename().string();
        if (fn.size() >= 3 && fn.substr(0, 3) == "Map"
            && entry.path().extension() == ".lmu")
            lmu_files.push_back(entry.path());
    }
    std::sort(lmu_files.begin(), lmu_files.end());

    int errors = 0;
    Decoder dec;
    dec.set_name_table(names);

    for (const auto& lmu_path : lmu_files) {
        auto map = lcf::LMU_Reader::Load(lmu_path.string(), encoding);
        if (!map) {
            std::cerr << "Error: failed to load " << lmu_path << std::endl;
            ++errors; continue;
        }
        std::string text = dec.decode_map(*map, encoding);
        // 拡張子を .rs2k に変えて出力
        fs::path out = odir / lmu_path.stem().concat(".rs2k");
        write_text_file(out.string(), text);
        std::cerr << lmu_path.filename().string() << " -> " << out.filename().string() << "\n";
    }

    // RPG_RT.ldb
    fs::path ldb_path = gdir / "RPG_RT.ldb";
    if (fs::exists(ldb_path)) {
        auto db = lcf::LDB_Reader::Load(ldb_path.string(), encoding);
        if (!db) {
            std::cerr << "Error: failed to load " << ldb_path << std::endl;
            ++errors;
        } else {
            std::string text = dec.decode_common_events(*db, encoding);
            fs::path out = odir / "CommonEvents.rs2k";
            write_text_file(out.string(), text);
            std::cerr << "RPG_RT.ldb -> CommonEvents.rs2k\n";
        }
    }

    if (errors == 0)
        std::cerr << "decode-project: " << lmu_files.size() << " maps + LDB -> " << out_dir << "\n";
    else
        std::cerr << "decode-project: " << errors << " error(s)\n";
    return errors ? 1 : 0;
}

// スクリプトディレクトリの .rs2k をゲームディレクトリの .lmu / .ldb に書き戻す
// MapNNNN.rs2k → game_dir/MapNNNN.lmu  (上書き)
// CommonEvents.rs2k → game_dir/RPG_RT.ldb  (上書き)
static int cmd_encode_project(const std::string& script_dir,
                               const std::string& game_dir,
                               const std::string& encoding,
                               const NameTable* names) {
    fs::path sdir(script_dir);
    if (!fs::is_directory(sdir)) {
        std::cerr << "Error: not a directory: " << script_dir << std::endl;
        return 1;
    }
    fs::path gdir(game_dir);
    if (!fs::is_directory(gdir)) {
        std::cerr << "Error: not a directory: " << game_dir << std::endl;
        return 1;
    }

    // .rs2k ファイルを収集してソート
    std::vector<fs::path> rs2k_files;
    for (const auto& entry : fs::directory_iterator(sdir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".rs2k")
            rs2k_files.push_back(entry.path());
    }
    std::sort(rs2k_files.begin(), rs2k_files.end());

    int errors = 0;
    Encoder enc;
    enc.set_name_table(names);

    for (const auto& rs2k_path : rs2k_files) {
        std::string stem = rs2k_path.stem().string(); // e.g. "Map0001" or "CommonEvents"

        std::string source;
        try {
            source = read_text_file(rs2k_path.string());
        } catch (const std::exception& e) {
            std::cerr << "Error reading " << rs2k_path.filename() << ": " << e.what() << "\n";
            ++errors; continue;
        }

        if (stem == "CommonEvents") {
            // LDB へエンコード
            fs::path ldb_path = gdir / "RPG_RT.ldb";
            if (!fs::exists(ldb_path)) {
                std::cerr << "Error: " << ldb_path << " not found\n";
                ++errors; continue;
            }
            auto db = lcf::LDB_Reader::Load(ldb_path.string(), encoding);
            if (!db) {
                std::cerr << "Error: failed to load " << ldb_path << "\n";
                ++errors; continue;
            }
            try {
                enc.encode_to_database(source, *db, encoding);
            } catch (const std::exception& e) {
                std::cerr << "Error encoding " << rs2k_path.filename() << ": " << e.what() << "\n";
                ++errors; continue;
            }
            if (!lcf::LDB_Reader::Save(ldb_path.string(), *db, encoding)) {
                std::cerr << "Error: failed to save " << ldb_path << "\n";
                ++errors; continue;
            }
            std::cerr << rs2k_path.filename().string() << " -> RPG_RT.ldb\n";

        } else {
            // MapNNNN.rs2k → MapNNNN.lmu
            fs::path lmu_path = gdir / (stem + ".lmu");
            if (!fs::exists(lmu_path)) {
                std::cerr << "Error: " << lmu_path << " not found\n";
                ++errors; continue;
            }
            auto map = lcf::LMU_Reader::Load(lmu_path.string(), encoding);
            if (!map) {
                std::cerr << "Error: failed to load " << lmu_path << "\n";
                ++errors; continue;
            }
            try {
                enc.encode_to_map(source, *map, encoding);
            } catch (const std::exception& e) {
                std::cerr << "Error encoding " << rs2k_path.filename() << ": " << e.what() << "\n";
                ++errors; continue;
            }
            if (!lcf::LMU_Reader::Save(lmu_path.string(), *map, lcf::EngineVersion::e2k, encoding)) {
                std::cerr << "Error: failed to save " << lmu_path << "\n";
                ++errors; continue;
            }
            std::cerr << rs2k_path.filename().string() << " -> " << (stem + ".lmu") << "\n";
        }
    }

    if (errors == 0)
        std::cerr << "encode-project: " << rs2k_files.size() << " files -> " << game_dir << "\n";
    else
        std::cerr << "encode-project: " << errors << " error(s)\n";
    return errors ? 1 : 0;
}

// ──────────────────────────────────────────────────────────────────────────────
// main
// ──────────────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    if (argc < 2) { print_usage(argv[0]); return 1; }

    std::string cmd    = argv[1];
    std::string out_path;
    std::string encoding  = "932"; // デフォルト: Shift-JIS（日本語版ツクール2000/2003）
    std::string names_ldb;         // -n <ldb> で指定する NameTable 用 LDB パス

    // オプション解析
    std::vector<std::string> positional;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) {
            out_path = argv[++i];
        } else if (a == "-e" && i + 1 < argc) {
            encoding = argv[++i];
        } else if (a == "-n" && i + 1 < argc) {
            names_ldb = argv[++i];
        } else {
            positional.push_back(a);
        }
    }

    // -n が省略された場合、入力ファイルの隣にある RPG_RT.ldb を自動使用する
    if (names_ldb.empty()) {
        fs::path auto_ldb;
        if ((cmd == "decode" || cmd == "encode") && !positional.empty()) {
            // lmu ファイルと同じディレクトリの RPG_RT.ldb
            auto_ldb = fs::path(positional[0]).parent_path() / "RPG_RT.ldb";
        } else if (cmd == "decode-db" && !positional.empty()) {
            // ldb 自身
            auto_ldb = positional[0];
        } else if (cmd == "encode-db" && positional.size() >= 2) {
            // 第2引数が ldb
            auto_ldb = positional[1];
        } else if ((cmd == "decode-project" || cmd == "encode-project") && !positional.empty()) {
            // game_dir の RPG_RT.ldb
            auto_ldb = fs::path(positional[0]) / "RPG_RT.ldb";
        } else if (cmd == "encode-project" && positional.size() >= 2) {
            auto_ldb = fs::path(positional[1]) / "RPG_RT.ldb";
        }
        if (!auto_ldb.empty() && fs::exists(auto_ldb))
            names_ldb = auto_ldb.string();
    }

    // NameTable を構築
    NameTable nt;
    const NameTable* names = nullptr;
    if (!names_ldb.empty()) {
        try {
            nt = build_name_table(names_ldb, encoding);
            names = &nt;
        } catch (const std::exception& ex) {
            std::cerr << "Warning: failed to load name table: " << ex.what() << std::endl;
        }
    }

    try {
        if (cmd == "decode") {
            if (positional.empty()) { print_usage(argv[0]); return 1; }
            return cmd_decode(positional[0], out_path, encoding, names);
        } else if (cmd == "encode") {
            if (positional.size() < 2) { print_usage(argv[0]); return 1; }
            return cmd_encode(positional[0], positional[1], out_path, encoding, names);
        } else if (cmd == "decode-db") {
            if (positional.empty()) { print_usage(argv[0]); return 1; }
            return cmd_decode_db(positional[0], out_path, encoding, names);
        } else if (cmd == "encode-db") {
            if (positional.size() < 2) { print_usage(argv[0]); return 1; }
            return cmd_encode_db(positional[0], positional[1], out_path, encoding, names);
        } else if (cmd == "decode-project") {
            if (positional.empty()) { print_usage(argv[0]); return 1; }
            return cmd_decode_project(positional[0], out_path, encoding, names);
        } else if (cmd == "encode-project") {
            if (positional.size() < 2) { print_usage(argv[0]); return 1; }
            return cmd_encode_project(positional[0], positional[1], encoding, names);
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
