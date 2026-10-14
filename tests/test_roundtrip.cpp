// tests/test_roundtrip.cpp
// decode→encode→decode のテキスト一致を CTest で確認する
// LMU ファイルは liblcf で最小構成を生成して使う

#include "codec.h"
#include "commands.h"
#include "parser.h"

#include <lcf/lmu/reader.h>
#include <lcf/rpg/map.h>
#include <lcf/rpg/database.h>
#include <lcf/rpg/eventcommand.h>

#include <cassert>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace rm2k;

// ─── ヘルパー ───────────────────────────────────────────────────────────────

static lcf::rpg::Map make_empty_map() {
    lcf::rpg::Map m;
    m.chipset_id = 1;
    m.width  = 20;
    m.height = 15;
    m.parallax_flag = false;
    return m;
}

// テキストを encode してから decode し、テキストが一致するか確認
static bool roundtrip(const std::string& script, const std::string& label) {
    auto map = make_empty_map();
    Encoder enc;
    try {
        enc.encode_to_map(script, map);
    } catch (const std::exception& e) {
        std::cerr << "[FAIL] " << label << " — encode error: " << e.what() << "\n";
        return false;
    }

    Decoder dec;
    std::string decoded = dec.decode_map(map);

    // decoded → encode → decode して安定性を確認
    auto map2 = make_empty_map();
    Encoder enc2;
    try {
        enc2.encode_to_map(decoded, map2);
    } catch (const std::exception& e) {
        std::cerr << "[FAIL] " << label << " — re-encode error: " << e.what() << "\n";
        return false;
    }

    Decoder dec2;
    std::string decoded2 = dec2.decode_map(map2);

    if (decoded != decoded2) {
        std::cerr << "[FAIL] " << label << " — text mismatch\n";
        // 最初の差分を表示
        std::istringstream s1(decoded), s2(decoded2);
        std::string l1, l2;
        int linenum = 0;
        while (std::getline(s1, l1) && std::getline(s2, l2)) {
            ++linenum;
            if (l1 != l2) {
                std::cerr << "  line " << linenum << ":\n"
                          << "  < " << l1 << "\n"
                          << "  > " << l2 << "\n";
                break;
            }
        }
        return false;
    }

    std::cerr << "[PASS] " << label << "\n";
    return true;
}

// ─── テストケース ────────────────────────────────────────────────────────────

static bool test_message() {
    return roundtrip(R"(
event(1, x=0, y=0) {
    page(1, trigger=action, layer=same) {
        message("こんにちは！\n2行目です。");
    }
}
)", "message multiline");
}

static bool test_switch_var() {
    return roundtrip(R"(
event(2, x=1, y=1) {
    page(1, trigger=action, layer=same) {
        switch[1] = ON;
        switch[2] = OFF;
        switch[1..5] = toggle;
        var[1] = 42;
        var[2] += 10;
        var[3] = var[1];
    }
}
)", "switch and var");
}

static bool test_if_else() {
    return roundtrip(R"(
event(3, x=2, y=2) {
    page(1, trigger=action, layer=same) {
        if (switch[1] == ON) {
            message("ONです");
        } else {
            message("OFFです");
        }
        if (var[1] >= 10) {
            var[1] -= 10;
        }
    }
}
)", "if-else");
}

static bool test_choice() {
    return roundtrip(R"(
event(4, x=3, y=3) {
    page(1, trigger=action, layer=same) {
        choice("はい", "いいえ", cancel=5) {
            when("はい", index=0) {
                switch[10] = ON;
            }
            when("いいえ", index=1) {
                switch[10] = OFF;
            }
        }
    }
}
)", "choice");
}

static bool test_teleport() {
    return roundtrip(R"(
event(5, x=4, y=4) {
    page(1, trigger=action, layer=same) {
        Teleport(map=2, x=10, y=5);
        MemorizeLocation(var_map=1, var_x=2, var_y=3);
    }
}
)", "teleport");
}

static bool test_bgm_se() {
    return roundtrip(R"(
event(6, x=5, y=5) {
    page(1, trigger=action, layer=same) {
        PlayBGM("battle", event=0, volume=100, tempo=100, balance=50);
        FadeOutBGM(ms=1000);
        MemorizeBGM();
        PlayMemorizedBGM();
        PlaySound("cursor", p0=60, p1=100, p2=50);
    }
}
)", "bgm and se");
}

static bool test_screen_effects() {
    return roundtrip(R"(
event(7, x=6, y=6) {
    page(1, trigger=action, layer=same) {
        TintScreen(r=0, g=0, b=0, s=100, frames=20, wait=1);
        FlashScreen(r=255, g=255, b=255, power=31, frames=5, wait=0);
        ShakeScreen(power=5, speed=5, frames=10, wait=1);
        Wait(frames=30);
    }
}
)", "screen effects");
}

static bool test_call_event() {
    return roundtrip(R"(
event(8, x=7, y=7) {
    page(1, trigger=action, layer=same) {
        CallEvent(type=0, id=10, page=0);
        CallCommonEvent(id=5);
        EraseEvent();
    }
}
)", "call event");
}

static bool test_store_terrain() {
    return roundtrip(R"(
event(9, x=8, y=8) {
    page(1, trigger=action, layer=same) {
        StoreTerrainID(src=0, x=5, y=3, var=10);
        StoreEventID(src=1, x=1, y=2, var=11);
    }
}
)", "store terrain/event id");
}

static bool test_change_items_party() {
    return roundtrip(R"(
event(10, x=9, y=9) {
    page(1, trigger=action, layer=same) {
        ChangeItems(item=5, op=0, src=0, amount=3, p4=0);
        ChangePartyMembers(add, src=0, actor=2);
        ChangePartyMembers(remove, src=0, actor=3);
    }
}
)", "items and party");
}

static bool test_skills_equipment() {
    return roundtrip(R"(
event(11, x=10, y=10) {
    page(1, trigger=action, layer=same) {
        ChangeSkills(actor=1, op=learn, src=0, skill=5);
        ChangeSkills(party, op=forget, src=0, skill=10);
        ChangeEquipment(actor=1, op=0, src=0, item=20);
    }
}
)", "skills and equipment");
}

static bool test_loop() {
    return roundtrip(R"(
event(12, x=11, y=11) {
    page(1, trigger=action, layer=same) {
        Loop() {
            var[1] += 1;
            if (var[1] >= 10) {
                BreakLoop();
            }
        }
    }
}
)", "loop with break");
}

static bool test_page_conditions() {
    return roundtrip(R"(
event(13, x=12, y=12) {
    page(1, trigger=action, layer=same, sprite="Actor1", sprite_index=0, dir=down,
         move_type=stationary, move_speed=normal, move_freq=3, anim_type=non_continuous,
         cond_switch_a=1, cond_switch_b=2) {
        message("条件付きページ");
    }
    page(2, trigger=parallel, layer=below, cond_var=10, cond_var_op=ge, cond_var_val=5) {
        Wait(frames=1);
    }
}
)", "page conditions");
}

// ─── main ────────────────────────────────────────────────────────────────────

// ─── ゴールデンテスト (期待する LCF コマンド列との直接比較) ───
// 往復一致 (冪等性) だけでは「誤変換」を検出できないため、エンコード結果を直接検証する。

struct ExpCmd {
    int code;
    int indent;
    std::vector<int32_t> params;
    std::string str;
};

static int C(LcfCode c) { return static_cast<int>(c); }

static std::vector<lcf::rpg::EventCommand> encode_cmds(const std::string& body) {
    auto map = make_empty_map();
    Encoder enc;
    std::string script = "event(1, x=0, y=0) {\n    page(1, trigger=action) {\n"
                         + body + "\n    }\n}\n";
    enc.encode_to_map(script, map);
    return map.events.at(0).pages.at(0).event_commands;
}

static bool golden(const std::string& body, const std::vector<ExpCmd>& exp,
                   const std::string& label) {
    std::vector<lcf::rpg::EventCommand> got;
    try {
        got = encode_cmds(body);
    } catch (const std::exception& e) {
        std::cerr << "[FAIL] " << label << " — encode error: " << e.what() << "\n";
        return false;
    }
    if (got.size() != exp.size()) {
        std::cerr << "[FAIL] " << label << " — command count: got " << got.size()
                  << ", expected " << exp.size() << "\n";
        return false;
    }
    for (size_t i = 0; i < exp.size(); ++i) {
        const auto& g = got[i];
        std::string gs(g.string.begin(), g.string.end());
        bool ok = g.code == exp[i].code && g.indent == exp[i].indent
                  && gs == exp[i].str && g.parameters.size() == exp[i].params.size();
        for (size_t k = 0; ok && k < exp[i].params.size(); ++k)
            ok = (g.parameters[k] == exp[i].params[k]);
        if (!ok) {
            std::cerr << "[FAIL] " << label << " — mismatch at command " << i
                      << " (code=" << g.code << " indent=" << g.indent << ")\n";
            return false;
        }
    }
    std::cerr << "[PASS] " << label << "\n";
    return true;
}

// 不正な入力は必ずエラーになる (黙って別物にならない) ことを確認
static bool must_fail_page(const std::string& body, const std::string& page_opts,
                           const std::string& label) {
    auto map = make_empty_map();
    Encoder enc;
    std::string script = "event(1, x=0, y=0) {\n    page(1" + page_opts + ") {\n"
                         + body + "\n    }\n}\n";
    try {
        enc.encode_to_map(script, map);
    } catch (const std::exception&) {
        std::cerr << "[PASS] " << label << "\n";
        return true;
    }
    std::cerr << "[FAIL] " << label << " — expected an error but encoding succeeded\n";
    return false;
}

static bool test_golden_switch_var() {
    bool a = golden("switch[3] = ON;",
        {{C(LcfCode::ControlSwitches), 0, {0, 3, 3, 1}, ""}}, "golden switch ON");
    bool b = golden("switch[5] = OFF;",
        {{C(LcfCode::ControlSwitches), 0, {0, 5, 5, 0}, ""}}, "golden switch OFF");
    bool c = golden("switch[2] = toggle;",
        {{C(LcfCode::ControlSwitches), 0, {0, 2, 2, 2}, ""}}, "golden switch toggle");
    bool d = golden("var[2] += 10;",
        {{C(LcfCode::ControlVars), 0, {0, 2, 2, 1, 0, 10}, ""}}, "golden var");
    // ControlVars キー形式も末尾0を省略した6パラメータで出力されること
    bool e = golden("ControlVars(var=3, type=0, id2=3, op_code=0, src=0, src_val=99);",
        {{C(LcfCode::ControlVars), 0, {0, 3, 3, 0, 0, 99}, ""}}, "golden ControlVars key-form trim zeros");
    return a && b && c && d && e;
}

static bool test_golden_if_else() {
    return golden(
        "if (switch[1] == ON) { switch[2] = OFF; } else { switch[2] = ON; }",
        {
            // switch[1]==ON → type=0, id=1, val=0(ON) → cond_pN なし → {0,1,0}
            {C(LcfCode::ConditionalBranch), 0, {0, 1, 0}, ""},
            {C(LcfCode::ControlSwitches),   1, {0, 2, 2, 0}, ""},
            {C(LcfCode::END),               1, {}, ""},
            {C(LcfCode::ElseBranch),        0, {}, ""},
            {C(LcfCode::ControlSwitches),   1, {0, 2, 2, 1}, ""},
            {C(LcfCode::END),               1, {}, ""},
            {C(LcfCode::EndBranch),         0, {}, ""},
        }, "golden if-else");
}

static bool test_golden_raw_command() {
    return golden("RawCommand(code=99999, str=\"x\", p0=1, p1=2);",
        {{99999, 0, {1, 2}, "x"}}, "golden RawCommand");
}

// ─── while ゴールデンテスト ──────────────────────────────────────────────────

static bool test_golden_while() {
    // while (var[1] >= 1) { var[1] -= 1; }
    // 展開: Loop { if(var[1]>=1){} else{BreakLoop} body END EndLoop
    return golden(
        "while (var[1] >= 1) { var[1] -= 1; }",
        {
            {C(LcfCode::Loop),              0, {}, ""},
            // ConditionalBranch: var[1] >= 1 → type=1(var), id=1, src=0(const), val=1, op=1(>=)
            // cond_pN なし → 自然サイズ {1, 1, 0, 1, 1}
            {C(LcfCode::ConditionalBranch), 1, {1, 1, 0, 1, 1}, ""},
            {C(LcfCode::END),               2, {}, ""},
            {C(LcfCode::ElseBranch),        1, {}, ""},
            {C(LcfCode::BreakLoop),         2, {}, ""},
            {C(LcfCode::END),               2, {}, ""},
            {C(LcfCode::EndBranch),         1, {}, ""},
            // body: var[1] -= 1 → 末尾0省略後: {0, 1, 1, 2, 0, 1}
            {C(LcfCode::ControlVars),       1, {0, 1, 1, 2, 0, 1}, ""},
            {C(LcfCode::END),               1, {}, ""},
            {C(LcfCode::EndLoop),           0, {}, ""},
        }, "golden while");
}

// ─── for ゴールデンテスト ────────────────────────────────────────────────────

static bool test_golden_for() {
    // for (var[2] = 1; var[2] <= 5; var[2] += 1) { Wait(frames=1); }
    return golden(
        "for (var[2] = 1; var[2] <= 5; var[2] += 1) { Wait(frames=1); }",
        {
            // init: var[2] = 1 → 末尾0省略後: {0, 2, 2, 0, 0, 1}
            {C(LcfCode::ControlVars),       0, {0, 2, 2, 0, 0, 1}, ""},
            {C(LcfCode::Loop),              0, {}, ""},
            // ConditionalBranch: var[2] <= 5 → type=1, id=2, src=0, val=5, op=2(<=) → cond_pN なし → {1,2,0,5,2}
            {C(LcfCode::ConditionalBranch), 1, {1, 2, 0, 5, 2}, ""},
            {C(LcfCode::END),               2, {}, ""},
            {C(LcfCode::ElseBranch),        1, {}, ""},
            {C(LcfCode::BreakLoop),         2, {}, ""},
            {C(LcfCode::END),               2, {}, ""},
            {C(LcfCode::EndBranch),         1, {}, ""},
            {C(LcfCode::Wait),              1, {1}, ""},
            // step: var[2] += 1 → 末尾0省略後: {0, 2, 2, 1, 0, 1}
            {C(LcfCode::ControlVars),       1, {0, 2, 2, 1, 0, 1}, ""},
            {C(LcfCode::END),               1, {}, ""},
            {C(LcfCode::EndLoop),           0, {}, ""},
        }, "golden for");
}

// ─── while / for ラウンドトリップ ───────────────────────────────────────────

static bool test_while_roundtrip() {
    return roundtrip(R"(
event(20, x=0, y=0) {
    page(1, trigger=action, layer=same) {
        while (var[1] >= 1) {
            var[1] -= 1;
        }
    }
}
)", "while roundtrip");
}

static bool test_for_roundtrip() {
    return roundtrip(R"(
event(21, x=0, y=0) {
    page(1, trigger=action, layer=same) {
        for (var[2] = 1; var[2] <= 5; var[2] += 1) {
            Wait(frames=1);
        }
    }
}
)", "for roundtrip");
}

// ─── DB (コモンイベント) ラウンドトリップ ───────────────────────────────────

static bool roundtrip_db(const std::string& script, const std::string& label) {
    lcf::rpg::Database db;
    Encoder enc;
    try {
        enc.encode_to_database(script, db);
    } catch (const std::exception& e) {
        std::cerr << "[FAIL] " << label << " — encode error: " << e.what() << "\n";
        return false;
    }

    Decoder dec;
    std::string decoded = dec.decode_common_events(db);

    lcf::rpg::Database db2;
    Encoder enc2;
    try {
        enc2.encode_to_database(decoded, db2);
    } catch (const std::exception& e) {
        std::cerr << "[FAIL] " << label << " — re-encode error: " << e.what() << "\n";
        return false;
    }

    Decoder dec2;
    std::string decoded2 = dec2.decode_common_events(db2);

    if (decoded != decoded2) {
        std::cerr << "[FAIL] " << label << " — text mismatch\n";
        std::istringstream s1(decoded), s2(decoded2);
        std::string l1, l2;
        int linenum = 0;
        while (std::getline(s1, l1) && std::getline(s2, l2)) {
            ++linenum;
            if (l1 != l2) {
                std::cerr << "  line " << linenum << ":\n"
                          << "  < " << l1 << "\n"
                          << "  > " << l2 << "\n";
                break;
            }
        }
        return false;
    }

    std::cerr << "[PASS] " << label << "\n";
    return true;
}

static bool test_db_roundtrip() {
    return roundtrip_db(R"(
event(1, name="init") {
    page(1, trigger=auto_start) {
        switch[1] = ON;
        var[1] = 100;
        message("common event");
    }
}
event(2, name="heal") {
    page(1, trigger=parallel, cond_switch=1) {
        ChangeHP(actor=1, delta=50, die_allowed=0);
    }
}
)", "db roundtrip");
}

// ─── int32_t 境界値テスト ────────────────────────────────────────────────────

static bool test_int32_bounds() {
    bool ok = true;
    auto try_encode = [](const std::string& body, bool expect_fail, const std::string& label) -> bool {
        auto map = make_empty_map();
        Encoder enc;
        std::string script = "event(1, x=0, y=0) {\n    page(1, trigger=action) {\n"
                             + body + "\n    }\n}\n";
        bool threw = false;
        try { enc.encode_to_map(script, map); }
        catch (const std::exception&) { threw = true; }
        if (threw == expect_fail) {
            std::cerr << "[PASS] " << label << "\n";
            return true;
        }
        std::cerr << "[FAIL] " << label << (expect_fail ? " — should have thrown\n" : " — unexpected error\n");
        return false;
    };
    ok &= try_encode("var[1] = 2147483647;",  false, "int32 max (2147483647)");
    ok &= try_encode("var[1] = -2147483648;", false, "int32 min (-2147483648)");
    ok &= try_encode("var[1] = 2147483648;",  true,  "int32 overflow (2147483648)");
    return ok;
}

// ─── for ステップ変数混在エラーテスト ───────────────────────────────────────

static bool test_for_step_errors() {
    bool ok = true;
    // step が別の数値変数 → エラー
    {
        auto map = make_empty_map();
        Encoder enc;
        std::string script = "event(1, x=0, y=0) {\n    page(1, trigger=action) {\n"
                             "for (var[1] = 0; var[1] < 5; var[2] += 1) {}\n    }\n}\n";
        bool threw = false;
        try { enc.encode_to_map(script, map); } catch (const std::exception&) { threw = true; }
        if (threw) std::cerr << "[PASS] for step different var correctly rejected\n";
        else { std::cerr << "[FAIL] for step different var: should have thrown\n"; ok = false; }
    }
    return ok;
}

static bool test_errors() {
    bool ok = true;
    ok &= must_fail_page("NoSuchCommand();", "", "error: unknown command");
    ok &= must_fail_page("if (bogus) { Wait(frames=1); }", "", "error: unknown condition");
    ok &= must_fail_page("Wait(frames=1);", ", trigger=actoin", "error: invalid trigger");
    ok &= must_fail_page("Wait(frames=1);", ", no_such_option=1", "error: unknown page option");
    ok &= must_fail_page("switch[\"名前\"] = ON;", "", "error: name without name table");
    ok &= must_fail_page("message(\"unterminated);", "", "error: unterminated string");
    return ok;
}

int main() {
    register_all_commands();

    int failures = 0;
    auto run = [&](bool(*fn)()) { if (!fn()) ++failures; };

    run(test_message);
    run(test_switch_var);
    run(test_if_else);
    run(test_choice);
    run(test_teleport);
    run(test_bgm_se);
    run(test_screen_effects);
    run(test_call_event);
    run(test_store_terrain);
    run(test_change_items_party);
    run(test_skills_equipment);
    run(test_loop);
    run(test_page_conditions);
    run(test_golden_switch_var);
    run(test_golden_if_else);
    run(test_golden_raw_command);
    run(test_golden_while);
    run(test_golden_for);
    run(test_while_roundtrip);
    run(test_for_roundtrip);
    run(test_db_roundtrip);
    run(test_int32_bounds);
    run(test_for_step_errors);
    run(test_errors);

    if (failures == 0) {
        std::cerr << "\nAll tests passed.\n";
        return 0;
    } else {
        std::cerr << "\n" << failures << " test(s) FAILED.\n";
        return 1;
    }
}
