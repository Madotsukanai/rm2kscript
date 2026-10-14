// tests/test_game_roundtrip.cpp
// ゆめ2っきの全マップ・LDB に対してデコード→エンコード→デコード テキスト完全一致を検証
// Usage: test_game_roundtrip <game_dir>

#include "codec.h"
#include "commands.h"
#include "parser.h"

#include <lcf/lmu/reader.h>
#include <lcf/ldb/reader.h>
#include <lcf/rpg/map.h>
#include <lcf/rpg/database.h>

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

static void test_lmu(const std::string& path) {
    static const std::string enc = "932";
    auto map = lcf::LMU_Reader::Load(path, enc);
    if (!map) {
        std::cerr << "[SKIP] failed to load: " << path << "\n";
        return;
    }

    Decoder dec;
    std::string t1 = dec.decode_map(*map, enc);

    // encode back
    Encoder encdr;
    try {
        encdr.encode_to_map(t1, *map, enc);
    } catch (const std::exception& e) {
        ++failures;
        std::cerr << "[FAIL] " << path << " — encode error: " << e.what() << "\n";
        return;
    }

    // decode again
    Decoder dec2;
    std::string t2 = dec2.decode_map(*map, enc);

    check(t1, t2, path);
}

static void test_ldb(const std::string& path) {
    static const std::string enc = "932";
    auto db = lcf::LDB_Reader::Load(path, enc);
    if (!db) {
        std::cerr << "[SKIP] failed to load: " << path << "\n";
        return;
    }

    Decoder dec;
    std::string t1 = dec.decode_common_events(*db, enc);

    Encoder encdr;
    try {
        encdr.encode_to_database(t1, *db, enc);
    } catch (const std::exception& e) {
        ++failures;
        std::cerr << "[FAIL] " << path << " — encode error: " << e.what() << "\n";
        return;
    }

    Decoder dec2;
    std::string t2 = dec2.decode_common_events(*db, enc);

    check(t1, t2, path);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: test_game_roundtrip <game_dir>\n";
        return 1;
    }
    fs::path dir(argv[1]);
    if (!fs::is_directory(dir)) {
        std::cerr << "Not a directory: " << argv[1] << "\n";
        return 1;
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
        test_lmu(p.string());
    }

    // LDB
    fs::path ldb = dir / "RPG_RT.ldb";
    if (fs::exists(ldb)) {
        test_ldb(ldb.string());
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
