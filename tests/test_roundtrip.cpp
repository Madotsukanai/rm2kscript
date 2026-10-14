// tests/test_roundtrip.cpp
// decode→encode→decode のテキスト一致を CTest で確認する
// LMU ファイルは liblcf で最小構成を生成して使う

#include "codec.h"
#include "commands.h"
#include "parser.h"

#include <lcf/lmu/reader.h>
#include <lcf/rpg/map.h>
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

    if (failures == 0) {
        std::cerr << "\nAll tests passed.\n";
        return 0;
    } else {
        std::cerr << "\n" << failures << " test(s) FAILED.\n";
        return 1;
    }
}
