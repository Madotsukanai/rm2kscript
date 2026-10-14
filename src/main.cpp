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
#include <memory>
#include <utility>

using namespace rm2k;

// LMU 保存時のエンジンバージョン (--engine 2000|2003 で変更。デフォルトは 2000)
static lcf::EngineVersion g_engine = lcf::EngineVersion::e2k;
// --prune: スクリプトに現れないイベント/ページをマップから削除する
static bool g_prune = false;

static bool parse_engine(const std::string& s, lcf::EngineVersion& out) {
    if (s == "2000" || s == "2k" || s == "e2k")   { out = lcf::EngineVersion::e2k;  return true; }
    if (s == "2003" || s == "2k3" || s == "e2k3") { out = lcf::EngineVersion::e2k3; return true; }
    return false;
}

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
                 (省略時は入力の隣/ゲームディレクトリの RPG_RT.ldb を自動使用)
  --engine <ver> マップ保存時のエンジン: 2000 (デフォルト) または 2003
  --prune        encode / encode-project: スクリプトに現れないイベント/ページをマップから削除
  -h, --help     このヘルプを表示

encode-project は、全ファイルのエンコードが成功した場合のみゲームに書き込みます。
上書きする .lmu / .ldb は事前に <name>.bak として退避されます(既にあれば上書きしません)。

独自言語仕様 (詳細は docs/language.md):
  event(1, x=3, y=4) {
      page(1, trigger=action, layer=same) {
          message("こんにちは！");
          if (switch[1] == ON) {
              var[1] = 100;
              Teleport(map=2, x=5, y=3);
          } else {
              message("フラグが立っていません");
          }
          Wait(frames=30);
          PlayBGM("battle", volume=80, tempo=100, balance=50);
      }
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
    f.flush();
    // ディスクフル等の書き込み失敗を成功として扱わない
    if (!f) throw std::runtime_error("write failed: " + path);
}

// ──────────────────────────────────────────────────────────────────────────────
// NameTable を LDB から構築
// ──────────────────────────────────────────────────────────────────────────────

// (NameTable は main() で LDB を 1 回だけロードして構築する: バリデーション用 DB と共用)

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
                      const NameTable* names,
                      const lcf::rpg::Database* db = nullptr) {
    // 元のLMUを読み込む
    auto map = lcf::LMU_Reader::Load(lmu_path, encoding);
    if (!map) {
        std::cerr << "Error: failed to load " << lmu_path << std::endl;
        return 1;
    }
    std::string source = read_text_file(script_path);
    Encoder enc;
    enc.set_name_table(names);
    enc.set_prune(g_prune);
    if (db) enc.set_database(db);
    try {
        enc.encode_to_map(source, *map, encoding);
    } catch (const CompileError& e) {
        std::cerr << "Compile error at line " << e.loc().line
                  << " col " << e.loc().col << ": " << e.what() << std::endl;
        return 1;
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
    bool ok = lcf::LMU_Reader::Save(dest, *map, g_engine, encoding);
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
                          const NameTable* names,
                          const lcf::rpg::Database* db_for_validation = nullptr) {
    auto db = lcf::LDB_Reader::Load(ldb_path, encoding);
    if (!db) {
        std::cerr << "Error: failed to load " << ldb_path << std::endl;
        return 1;
    }
    std::string source = read_text_file(script_path);
    Encoder enc;
    enc.set_name_table(names);
    enc.set_database(db_for_validation ? db_for_validation : db.get());
    try {
        enc.encode_to_database(source, *db, encoding);
    } catch (const CompileError& e) {
        std::cerr << "Compile error at line " << e.loc().line
                  << " col " << e.loc().col << ": " << e.what() << std::endl;
        return 1;
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

    // バリデーション用に DB を事前ロード
    std::unique_ptr<lcf::rpg::Database> val_db;
    fs::path ldb_path = gdir / "RPG_RT.ldb";
    if (fs::exists(ldb_path)) {
        val_db = lcf::LDB_Reader::Load(ldb_path.string(), encoding);
    }

    int errors = 0;
    Encoder enc;
    enc.set_name_table(names);
    enc.set_prune(g_prune);
    if (val_db) enc.set_database(val_db.get());

    // 書き込みは一時ファイル (<dest>.rs2k_new) に対して行い、全ファイル成功後にまとめて差し替える
    std::vector<std::pair<fs::path, fs::path>> pending; // (tmp, dest)

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
            } catch (const LexError& e) {
                std::cerr << "Lex error in " << rs2k_path.filename().string()
                          << " at line " << e.loc().line << " col " << e.loc().col
                          << ": " << e.what() << "\n";
                ++errors; continue;
            } catch (const ParseError& e) {
                std::cerr << "Parse error in " << rs2k_path.filename().string()
                          << " at line " << e.loc().line << " col " << e.loc().col
                          << ": " << e.what() << "\n";
                ++errors; continue;
            } catch (const CompileError& e) {
                std::cerr << "Compile error in " << rs2k_path.filename().string()
                          << " at line " << e.loc().line << " col " << e.loc().col
                          << ": " << e.what() << "\n";
                ++errors; continue;
            } catch (const std::exception& e) {
                std::cerr << "Error encoding " << rs2k_path.filename() << ": " << e.what() << "\n";
                ++errors; continue;
            }
            fs::path tmp = ldb_path;
            tmp += ".rs2k_new";
            if (!lcf::LDB_Reader::Save(tmp.string(), *db, encoding)) {
                std::cerr << "Error: failed to save " << tmp << "\n";
                std::error_code ec; fs::remove(tmp, ec);
                ++errors; continue;
            }
            pending.emplace_back(tmp, ldb_path);
            std::cerr << rs2k_path.filename().string() << " -> RPG_RT.ldb (staged)\n";

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
            } catch (const LexError& e) {
                std::cerr << "Lex error in " << rs2k_path.filename().string()
                          << " at line " << e.loc().line << " col " << e.loc().col
                          << ": " << e.what() << "\n";
                ++errors; continue;
            } catch (const ParseError& e) {
                std::cerr << "Parse error in " << rs2k_path.filename().string()
                          << " at line " << e.loc().line << " col " << e.loc().col
                          << ": " << e.what() << "\n";
                ++errors; continue;
            } catch (const CompileError& e) {
                std::cerr << "Compile error in " << rs2k_path.filename().string()
                          << " at line " << e.loc().line << " col " << e.loc().col
                          << ": " << e.what() << "\n";
                ++errors; continue;
            } catch (const std::exception& e) {
                std::cerr << "Error encoding " << rs2k_path.filename() << ": " << e.what() << "\n";
                ++errors; continue;
            }
            fs::path tmp = lmu_path;
            tmp += ".rs2k_new";
            if (!lcf::LMU_Reader::Save(tmp.string(), *map, g_engine, encoding)) {
                std::cerr << "Error: failed to save " << tmp << "\n";
                std::error_code ec; fs::remove(tmp, ec);
                ++errors; continue;
            }
            pending.emplace_back(tmp, lmu_path);
            std::cerr << rs2k_path.filename().string() << " -> " << (stem + ".lmu") << " (staged)\n";
        }
    }

    std::error_code ec;
    if (errors != 0) {
        for (const auto& pr : pending) fs::remove(pr.first, ec);
        std::cerr << "encode-project: " << errors << " error(s); no files were modified\n";
        return 1;
    }

    // 全て成功: まず全ファイルを .bak に退避する (この段階で失敗しても元ファイルは未変更)
    for (const auto& pr : pending) {
        fs::path bak = pr.second;
        bak += ".bak";
        ec.clear();
        if (!fs::exists(bak))
            fs::copy_file(pr.second, bak, ec);
        if (ec) {
            std::cerr << "Error: cannot back up " << pr.second << ": " << ec.message() << "\n"
                      << "encode-project: no files were modified\n";
            for (const auto& q : pending) { std::error_code ec2; fs::remove(q.first, ec2); }
            return 1;
        }
    }
    // 退避が済んでから差し替える。失敗時は残りの一時ファイルを片付け、状況を報告する
    size_t replaced = 0;
    for (const auto& pr : pending) {
        ec.clear();
        fs::rename(pr.first, pr.second, ec);
        if (ec) {
            std::cerr << "Error: cannot replace " << pr.second << ": " << ec.message() << "\n"
                      << "encode-project: " << replaced << " of " << pending.size()
                      << " files were already replaced (originals are saved as *.bak)\n";
            for (const auto& q : pending) { std::error_code ec2; fs::remove(q.first, ec2); }
            return 1;
        }
        ++replaced;
    }
    std::cerr << "encode-project: " << pending.size() << " files -> " << game_dir << "\n";
    return 0;
}

// ──────────────────────────────────────────────────────────────────────────────
// main
// ──────────────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    if (argc < 2) { print_usage(argv[0]); return 1; }

    std::string cmd    = argv[1];
    if (cmd == "-h" || cmd == "--help") { print_usage(argv[0]); return 0; }
    std::string out_path;
    std::string encoding  = "932"; // デフォルト: Shift-JIS（日本語版ツクール2000/2003）
    std::string names_ldb;         // -n <ldb> で指定する NameTable 用 LDB パス

    // オプション解析
    std::vector<std::string> positional;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-h" || a == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (a == "--prune") {
            g_prune = true;
        } else if (a == "-o" || a == "-e" || a == "-n" || a == "--engine") {
            if (i + 1 >= argc) {
                std::cerr << "Error: option " << a << " requires a value" << std::endl;
                return 1;
            }
            std::string v = argv[++i];
            if (a == "-o") out_path = v;
            else if (a == "-e") encoding = v;
            else if (a == "-n") names_ldb = v;
            else if (!parse_engine(v, g_engine)) {
                std::cerr << "Error: unknown engine '" << v << "' (use 2000 or 2003)" << std::endl;
                return 1;
            }
        } else {
            positional.push_back(a);
        }
    }

    // -n を明示指定したか (自動検出と区別: 明示指定のロード失敗はエラーにする)
    const bool names_explicit = !names_ldb.empty();

    // -n が省略された場合、入力ファイルの隣にある RPG_RT.ldb を自動使用する
    if (names_ldb.empty()) {
        fs::path auto_ldb;
        if (cmd == "decode" && !positional.empty()) {
            // decode: positional[0] = LMU
            auto_ldb = fs::path(positional[0]).parent_path() / "RPG_RT.ldb";
        } else if (cmd == "encode" && positional.size() >= 2) {
            // encode: positional[0] = script, positional[1] = LMU
            auto_ldb = fs::path(positional[1]).parent_path() / "RPG_RT.ldb";
        } else if (cmd == "decode-db" && !positional.empty()) {
            // ldb 自身
            auto_ldb = positional[0];
        } else if (cmd == "encode-db" && positional.size() >= 2) {
            // 第2引数が ldb
            auto_ldb = positional[1];
        } else if (cmd == "decode-project" && !positional.empty()) {
            // decode-project: positional[0] = game_dir
            auto_ldb = fs::path(positional[0]) / "RPG_RT.ldb";
        } else if (cmd == "encode-project" && positional.size() >= 2) {
            // encode-project: positional[0] = script_dir, positional[1] = game_dir
            auto_ldb = fs::path(positional[1]) / "RPG_RT.ldb";
        }
        if (!auto_ldb.empty() && fs::exists(auto_ldb))
            names_ldb = auto_ldb.string();
    }

    // NameTable と DB を構築
    NameTable nt;
    const NameTable* names = nullptr;
    std::unique_ptr<lcf::rpg::Database> val_db;
    if (!names_ldb.empty()) {
        try {
            // 1 回だけロードして、名前テーブルとバリデーション用 DB の両方に使う
            val_db = lcf::LDB_Reader::Load(names_ldb, encoding);
            if (!val_db) throw std::runtime_error("cannot load nametable ldb: " + names_ldb);
            nt = NameTable::from_database(*val_db);
            names = &nt;
        } catch (const std::exception& ex) {
            if (names_explicit) {
                std::cerr << "Error: failed to load name table (-n " << names_ldb << "): "
                          << ex.what() << std::endl;
                return 1;
            }
            val_db.reset();
            std::cerr << "Warning: failed to load name table: " << ex.what() << std::endl;
        }
    }
    const lcf::rpg::Database* val_db_ptr = val_db.get();

    try {
        if (cmd == "decode") {
            if (positional.empty()) { print_usage(argv[0]); return 1; }
            return cmd_decode(positional[0], out_path, encoding, names);
        } else if (cmd == "encode") {
            if (positional.size() < 2) { print_usage(argv[0]); return 1; }
            return cmd_encode(positional[0], positional[1], out_path, encoding, names, val_db_ptr);
        } else if (cmd == "decode-db") {
            if (positional.empty()) { print_usage(argv[0]); return 1; }
            return cmd_decode_db(positional[0], out_path, encoding, names);
        } else if (cmd == "encode-db") {
            if (positional.size() < 2) { print_usage(argv[0]); return 1; }
            return cmd_encode_db(positional[0], positional[1], out_path, encoding, names, val_db_ptr);
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
