// tests/test_game_roundtrip.cpp
// ゆめ2っきの全マップ・LDB に対してデコード→エンコード→デコード テキスト完全一致を検証
// Usage: test_game_roundtrip <game_dir> [-n RPG_RT.ldb]

#include "codec.h"
#include "commands.h"
#include "nametable.h"
#include "parser.h"

#include <lcf/lmu/reader.h>
#include <lcf/ldb/reader.h>
#include <lcf/rpg/map.h>
#include <lcf/rpg/database.h>
#include <lcf/rpg/event.h>
#include <lcf/rpg/eventpage.h>
#include <lcf/rpg/commonevent.h>
#include <lcf/rpg/eventcommand.h>

#include <cassert>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

namespace fs = std::filesystem;
using namespace rm2k;

static int failures = 0;

static void check(const std::string& text1, const std::string& text2,
                  const std::string& label) {
    if (text1 == text2) return;

    ++failures;
    std::cerr << "[FAIL] " << label << "\n";
    // 最初の差分行を表示
    std::istringstream s1(text1), s2(text2);
    std::string l1, l2;
    int ln = 0;
    while (true) {
        bool ok1 = (bool)std::getline(s1, l1);
        bool ok2 = (bool)std::getline(s2, l2);
        ++ln;
        if (!ok1 && !ok2) break;
        if (l1 != l2) {
            std::cerr << "  first diff at line " << ln << ":\n";
            if (ok1) std::cerr << "  < " << l1 << "\n"; else std::cerr << "  < (eof)\n";
            if (ok2) std::cerr << "  > " << l2 << "\n"; else std::cerr << "  > (eof)\n";
            break;
        }
    }
}

// ─── LCF コマンド列の直接比較 ───────────────────────────────────────────────
// テキスト比較(decode→encode→decode)では、デコードで落ちた情報や解釈の誤りが
// 検出できない。元の EventCommand 列と再エンコード後の列を直接比較する。

static std::string describe_cmd(const lcf::rpg::EventCommand& c) {
    std::ostringstream o;
    o << "code=" << c.code << " indent=" << c.indent << " str=\""
      << std::string(c.string.begin(), c.string.end()) << "\" params=[";
    for (size_t i = 0; i < c.parameters.size(); ++i) {
        if (i) o << ',';
        o << c.parameters[i];
    }
    o << "]";
    return o.str();
}

static bool same_cmd(const lcf::rpg::EventCommand& a, const lcf::rpg::EventCommand& b) {
    if (a.code != b.code || a.indent != b.indent) return false;
    if (std::string(a.string.begin(), a.string.end()) !=
        std::string(b.string.begin(), b.string.end())) return false;

    // ControlVars(10220) で src=0/1/2 の場合: EasyRPG は p[6](src_extra) を参照しないため
    // エンコード時に 7→6 パラメータに圧縮されても意味的に等価。
    // orig=7パラメータ, now=6パラメータ かつ p[4]<=2 かつ orig p[6]==0 のとき許容する。
    size_t a_sz = a.parameters.size();
    size_t b_sz = b.parameters.size();
    auto ap = [&](size_t i) -> int32_t { return i < a_sz ? a.parameters[i] : 0; };
    auto bp = [&](size_t i) -> int32_t { return i < b_sz ? b.parameters[i] : 0; };

    bool cv_trim_ok = false;
    if (a.code == 10220) {
        // 7→6 圧縮: orig=7, now=6, src<=2, orig p[6]=0
        if (a_sz == 7 && b_sz == 6 && ap(4) <= 2 && ap(6) == 0) cv_trim_ok = true;
        // 6→7 の逆方向は起きないはずだが念のため対称に
        if (b_sz == 7 && a_sz == 6 && bp(4) <= 2 && bp(6) == 0) cv_trim_ok = true;
    }
    if (!cv_trim_ok && a_sz != b_sz) return false;

    // ControlSwitches(10210)/ControlVars(10220) の単独指定(type=0)では id2 は未使用値
    // (エディタの残骸が残る)なので、意味のある差分としては扱わない
    const bool skip_id2 = (a.code == 10210 || a.code == 10220)
                          && a_sz > 2 && ap(0) == 0;
    size_t cmp_sz = std::max(a_sz, b_sz);
    if (cv_trim_ok) cmp_sz = std::min(a_sz, b_sz); // 共通部分のみ比較
    for (size_t i = 0; i < cmp_sz; ++i) {
        if (skip_id2 && i == 2) continue;
        if (ap(i) != bp(i)) return false;
    }
    return true;
}

// 一致なら true。不一致なら最初の差分を表示して false。
static bool compare_cmds(const std::vector<lcf::rpg::EventCommand>& orig,
                         const std::vector<lcf::rpg::EventCommand>& now,
                         const std::string& label) {
    size_t n = std::min(orig.size(), now.size());
    for (size_t i = 0; i < n; ++i) {
        if (!same_cmd(orig[i], now[i])) {
            std::cerr << "[FAIL-CMD] " << label << " idx=" << i << "\n"
                      << "  orig: " << describe_cmd(orig[i]) << "\n"
                      << "  now : " << describe_cmd(now[i]) << "\n";
            return false;
        }
    }
    if (orig.size() != now.size()) {
        std::cerr << "[FAIL-CMD] " << label << " size orig=" << orig.size()
                  << " now=" << now.size() << "\n";
        if (orig.size() > now.size())
            std::cerr << "  orig extra: " << describe_cmd(orig[n]) << "\n";
        else
            std::cerr << "  now extra : " << describe_cmd(now[n]) << "\n";
        return false;
    }
    return true;
}

static void compare_map_commands(const lcf::rpg::Map& orig, const lcf::rpg::Map& now,
                                 const std::string& path) {
    int bad_pages = 0;
    for (const auto& oe : orig.events) {
        const lcf::rpg::Event* ne = nullptr;
        for (const auto& e : now.events)
            if (e.ID == oe.ID) { ne = &e; break; }
        if (!ne) {
            std::cerr << "[FAIL-CMD] " << path << " event=" << oe.ID << " missing after re-encode\n";
            ++bad_pages;
            continue;
        }
        for (const auto& op : oe.pages) {
            const lcf::rpg::EventPage* np = nullptr;
            for (const auto& p : ne->pages)
                if (p.ID == op.ID) { np = &p; break; }
            if (!np) {
                std::cerr << "[FAIL-CMD] " << path << " event=" << oe.ID
                          << " page=" << op.ID << " missing after re-encode\n";
                ++bad_pages;
                continue;
            }
            std::string label = path + " event=" + std::to_string(oe.ID)
                              + " page=" + std::to_string(op.ID);
            if (!compare_cmds(op.event_commands, np->event_commands, label)) {
                // 1ファイルあたり最初の3ページまで詳細を表示
                if (++bad_pages >= 3) {
                    ++failures;
                    return;
                }
            }
        }
    }
    if (bad_pages > 0) ++failures;
}

static void compare_db_commands(const lcf::rpg::Database& orig, const lcf::rpg::Database& now,
                                const std::string& path) {
    int bad = 0;
    for (const auto& oc : orig.commonevents) {
        const lcf::rpg::CommonEvent* nc = nullptr;
        for (const auto& c : now.commonevents)
            if (c.ID == oc.ID) { nc = &c; break; }
        if (!nc) {
            std::cerr << "[FAIL-CMD] " << path << " common_event=" << oc.ID << " missing after re-encode\n";
            ++bad;
            continue;
        }
        std::string label = path + " common_event=" + std::to_string(oc.ID);
        if (!compare_cmds(oc.event_commands, nc->event_commands, label)) {
            if (++bad >= 10) break; // 表示は10件まで
        }
    }
    if (bad > 0) ++failures;
}

static void test_lmu(const std::string& path, const NameTable* names = nullptr) {
    static const std::string enc = "932";
    auto map = lcf::LMU_Reader::Load(path, enc);
    if (!map) {
        std::cerr << "[SKIP] failed to load: " << path << "\n";
        return;
    }

    Decoder dec;
    dec.set_name_table(names);
    std::string t1 = dec.decode_map(*map, enc);
    const lcf::rpg::Map orig_map = *map; // コマンド列比較用の元データ

    // encode back
    Encoder encdr;
    encdr.set_name_table(names);
    try {
        encdr.encode_to_map(t1, *map, enc);
    } catch (const std::exception& e) {
        ++failures;
        std::cerr << "[FAIL] " << path << " — encode error: " << e.what() << "\n";
        return;
    }

    // decode again
    Decoder dec2;
    dec2.set_name_table(names);
    std::string t2 = dec2.decode_map(*map, enc);

    check(t1, t2, path);
    compare_map_commands(orig_map, *map, path);
}

static void test_ldb(const std::string& path, const NameTable* names = nullptr) {
    static const std::string enc = "932";
    auto db = lcf::LDB_Reader::Load(path, enc);
    if (!db) {
        std::cerr << "[SKIP] failed to load: " << path << "\n";
        return;
    }

    Decoder dec;
    dec.set_name_table(names);
    std::string t1 = dec.decode_common_events(*db, enc);
    const lcf::rpg::Database orig_db = *db; // コマンド列比較用の元データ

    Encoder encdr;
    encdr.set_name_table(names);
    try {
        encdr.encode_to_database(t1, *db, enc);
    } catch (const std::exception& e) {
        ++failures;
        std::cerr << "[FAIL] " << path << " — encode error: " << e.what() << "\n";
        return;
    }

    Decoder dec2;
    dec2.set_name_table(names);
    std::string t2 = dec2.decode_common_events(*db, enc);

    check(t1, t2, path);
    compare_db_commands(orig_db, *db, path);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: test_game_roundtrip <game_dir> [-n RPG_RT.ldb]\n";
        return 1;
    }
    fs::path dir(argv[1]);
    if (!fs::is_directory(dir)) {
        std::cerr << "Not a directory: " << argv[1] << "\n";
        return 1;
    }

    // -n オプションで NameTable を読み込む
    std::string names_ldb_path;
    for (int i = 2; i < argc; ++i) {
        if (std::string(argv[i]) == "-n" && i + 1 < argc)
            names_ldb_path = argv[++i];
    }

    NameTable nt;
    const NameTable* names_ptr = nullptr;
    if (!names_ldb_path.empty()) {
        auto db = lcf::LDB_Reader::Load(names_ldb_path, "932");
        if (db) {
            nt = NameTable::from_database(*db);
            names_ptr = &nt;
            std::cerr << "Name table loaded from " << names_ldb_path << "\n";
        }
    }

    register_all_commands();

    // マップファイルを番号順に収集
    std::vector<fs::path> lmu_files;
    for (auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file()) {
            auto fn = entry.path().filename().string();
            if (fn.size() >= 3 &&
                fn.substr(0, 3) == "Map" &&
                entry.path().extension() == ".lmu")
                lmu_files.push_back(entry.path());
        }
    }
    std::sort(lmu_files.begin(), lmu_files.end());

    int total = 0;
    for (const auto& p : lmu_files) {
        ++total;
        test_lmu(p.string(), names_ptr);
    }

    // LDB
    fs::path ldb = dir / "RPG_RT.ldb";
    if (fs::exists(ldb)) {
        test_ldb(ldb.string(), names_ptr);
        ++total;
    }

    if (failures == 0) {
        std::cerr << "All " << total << " files passed.\n";
        return 0;
    } else {
        std::cerr << failures << " FAILED out of " << total << " files.\n";
        return 1;
    }
}
