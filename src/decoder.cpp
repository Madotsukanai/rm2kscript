#include "codec.h"
#include "commands.h"
#include <lcf/rpg/event.h>
#include <lcf/rpg/eventpage.h>
#include <lcf/rpg/commonevent.h>
#include <sstream>
#include <string>
#include <algorithm>

namespace rm2k {

// ──────────────────────────────────────────────────────────────────────────────
// ConditionalBranch のデコード
// ──────────────────────────────────────────────────────────────────────────────

// param[0]: condition type
//   0 = switch, 1 = var compare, 2 = timer, 3 = actor, 4 = character,
//   5 = vehicle, 6 = item, 7 = equipment, 8 = event_state, 9 = gold_compare

// extra_cond_params: type の「既知パラメータ数（type含む）」以降を cond_pN として追加
static std::string extra_cond_params(const lcf::rpg::EventCommand& c, size_t known_total) {
    std::string r;
    for (size_t i = known_total; i < c.parameters.size(); ++i) {
        int32_t v = c.parameters[i];
        r += ", cond_p" + std::to_string(i) + "=" + std::to_string(v);
    }
    return r;
}

static std::string decode_cond(const lcf::rpg::EventCommand& c) {
    auto p = [&](size_t i) { return i < c.parameters.size() ? c.parameters[i] : 0; };
    int32_t type = p(0);
    switch (type) {
        case 0: { // switch [type,id,val,p3,p4,p5]
            int32_t id  = p(1);
            int32_t val = p(2); // 0=ON, 1=OFF
            std::string r = "switch[" + std::to_string(id) + "] == " + (val ? "OFF" : "ON");
            r += extra_cond_params(c, 3);
            return r;
        }
        case 1: { // var compare [type,id,src,val,op,p5]
            int32_t id  = p(1);
            int32_t src = p(2); // 0=const, 1=var
            int32_t val = p(3);
            static const char* ops[] = {"==",">=","<=",">","<","!="};
            int32_t op  = p(4);
            std::string rhs = src ? "var[" + std::to_string(val) + "]" : std::to_string(val);
            std::string r = "var[" + std::to_string(id) + "] " + ops[op < 6 ? op : 0] + " " + rhs;
            r += extra_cond_params(c, 5);
            return r;
        }
        case 2: { // timer [type,val,op,p3,p4,p5]
            int32_t val = p(1);
            int32_t op  = p(2); // 0=>=, 1=<=
            std::string r = "timer " + std::string(op ? "<=" : ">=") + " " + std::to_string(val);
            r += extra_cond_params(c, 3);
            return r;
        }
        case 3: { // actor condition [type,actor,cond,val,p4,p5]
            int32_t actor = p(1);
            int32_t cond  = p(2);
            int32_t val   = p(3);
            std::string r = "actor_cond(actor=" + std::to_string(actor)
                 + ", cond=" + std::to_string(cond) + ", val=" + std::to_string(val) + ")";
            r += extra_cond_params(c, 4);
            return r;
        }
        case 4: { // character facing [type,who,dir,p3,p4,p5]
            int32_t who = p(1);
            int32_t dir = p(2);
            static const char* dirs[] = {"up","right","down","left"};
            std::string who_s = (who == 10001) ? "this" : std::to_string(who);
            std::string r = "char_facing(who=" + who_s + ", dir=" + dirs[dir < 4 ? dir : 0] + ")";
            r += extra_cond_params(c, 3);
            return r;
        }
        case 5: { // vehicle [type,v,p2,p3,p4,p5]
            int32_t v = p(1);
            static const char* vs[] = {"ship","skiff","airship","any"};
            std::string r = "on_vehicle(type=" + std::string(vs[v < 4 ? v : 0]) + ")";
            r += extra_cond_params(c, 2);
            return r;
        }
        case 6: { // item [type,item_id,p2,p3,p4,p5]
            int32_t item = p(1);
            std::string r = "has_item(id=" + std::to_string(item) + ")";
            r += extra_cond_params(c, 2);
            return r;
        }
        case 7: { // equipment [type,actor,item,p3,p4,p5]
            int32_t actor = p(1);
            int32_t item  = p(2);
            std::string r = "has_equip(actor=" + std::to_string(actor) + ", item=" + std::to_string(item) + ")";
            r += extra_cond_params(c, 3);
            return r;
        }
        case 8: { // event state [type,event,val,p3,p4,p5]
            std::string r = "event_state(event=" + std::to_string(p(1)) + ", val=" + std::to_string(p(2)) + ")";
            r += extra_cond_params(c, 3);
            return r;
        }
        case 9: { // gold compare [type,val,op,p3,p4,p5]
            int32_t val = p(1);
            int32_t op  = p(2);
            std::string r = "gold_cmp(gold=" + std::to_string(val) + ", op=" + std::string(op?"le":"ge") + ")";
            r += extra_cond_params(c, 3);
            return r;
        }
        default:
            return "cond(" + std::to_string(type) + ")";
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// ユーティリティ
// ──────────────────────────────────────────────────────────────────────────────

static std::string quote_str(const std::string& s) {
    std::string r = "\"";
    for (char c : s) {
        if      (c == '"')  r += "\\\"";
        else if (c == '\\') r += "\\\\";
        else if (c == '\n') r += "\\n";
        else                r += c;
    }
    r += '"';
    return r;
}

static std::string indent_str(int n) {
    return std::string(static_cast<size_t>(std::max(0, n * 4)), ' ');
}

static std::string escape_for_comment(const std::string& s) {
    std::string r;
    for (char c : s) r += (c == '\n' ? ' ' : c);
    return r;
}

static inline std::string lcf_str(const lcf::DBString& ds) {
    return std::string(ds.begin(), ds.end());
}

// ──────────────────────────────────────────────────────────────────────────────
// decode_event_commands: ストリームをインデント付きで文字列化（新形式）
// ──────────────────────────────────────────────────────────────────────────────

// ShowMessage + ShowMessage_2 を統合して message("...") に
// ShowChoice + ShowChoiceOption + ShowChoiceEnd を choice(...){when(){}} に
// ControlSwitches → switch[N] = OP;
// ControlVars (src<=1) → var[N] op= val;

static std::string decode_control_switches(const lcf::rpg::EventCommand& c, int ind) {
    auto p = [&](size_t i) -> int32_t { return i < c.parameters.size() ? c.parameters[i] : 0; };
    int32_t type = p(0);
    int32_t id1  = p(1);
    int32_t id2  = p(2);
    int32_t val  = p(3);
    const char* op = (val == 0) ? "OFF" : (val == 1) ? "ON" : "toggle";
    std::string s = indent_str(ind);
    if (type == 0)
        s += "switch[" + std::to_string(id1) + "] = " + op + ";";
    else if (type == 1)
        s += "switch[" + std::to_string(id1) + ".." + std::to_string(id2) + "] = " + op + ";";
    else
        s += "switch[var[" + std::to_string(id1) + "]] = " + op + ";";
    return s;
}

static std::string decode_control_vars(const lcf::rpg::EventCommand& c, int ind) {
    auto p = [&](size_t i) -> int32_t { return i < c.parameters.size() ? c.parameters[i] : 0; };
    int32_t type = p(0);
    int32_t id1  = p(1);
    int32_t id2  = p(2);
    int32_t op   = p(3);
    int32_t src  = p(4);
    int32_t val  = p(5);
    static const char* ops[] = {"=","+=","-=","*=","/=","%="};
    std::string op_s = (op >= 0 && op < 6) ? ops[op] : "=";

    std::string lhs;
    if (type == 0)
        lhs = "var[" + std::to_string(id1) + "]";
    else
        lhs = "var[" + std::to_string(id1) + ".." + std::to_string(id2) + "]";

    if (src <= 1 && c.parameters.size() <= 6) {
        // シンプル形式: var[N] op= val; (p[6] なし)
        std::string rhs = (src == 0) ? std::to_string(val) : "var[" + std::to_string(val) + "]";
        return indent_str(ind) + lhs + " " + op_s + " " + rhs + ";";
    }
    // キー形式: ControlVars(var=N, type=T, id2=M, op_code=op, src=src, src_val=val [,src_extra=p6])
    // src<=1 でも p[6] がある場合や src>=2 の場合はこちら
    std::string r = indent_str(ind) + "ControlVars(var=" + std::to_string(id1)
         + ", type=" + std::to_string(type)
         + ", id2=" + std::to_string(id2)
         + ", op_code=" + std::to_string(op)
         + ", src=" + std::to_string(src)
         + ", src_val=" + std::to_string(val);
    if (c.parameters.size() > 6)
        r += ", src_extra=" + std::to_string(p(6));
    r += ");";
    return r;
}

// decode_cmds_impl: i を参照で受け取り、stop_code に出会ったら止まる
// stop_code1/2 が LcfCode::END の場合は EOF まで
static void decode_cmds_impl(
    const std::vector<lcf::rpg::EventCommand>& cmds,
    size_t& i, size_t n, int base_indent,
    std::ostringstream& out,
    LcfCode stop1 = LcfCode::END,
    LcfCode stop2 = LcfCode::END)
{
    while (i < n) {
        const auto& cmd = cmds[i];
        auto code = static_cast<LcfCode>(cmd.code);
        int vis = cmd.indent + base_indent;

        // ── 停止条件 ─────────────────────────────────────────────────────
        // END (code=10) はブランチ内末尾にも挿入される。
        // indent=0 の END だけが本当の終端。indent>0 の END はスキップ。
        if (code == LcfCode::END) {
            if (cmd.indent == 0) break; // 本当の終端
            ++i; continue;             // ブランチ内 END はスキップ
        }
        if (stop1 != LcfCode::END && code == stop1) break;
        if (stop2 != LcfCode::END && code == stop2) break;

        // ── ShowMessage + ShowMessage_2 を統合 ───────────────────────────
        if (code == LcfCode::ShowMessage) {
            std::string text = lcf_str(cmd.string);
            ++i;
            while (i < n && static_cast<LcfCode>(cmds[i].code) == LcfCode::ShowMessage_2) {
                text += '\n';
                text += lcf_str(cmds[i].string);
                ++i;
            }
            out << indent_str(vis) << "message(" << quote_str(text) << ");\n";
            continue;
        }

        // ── ShowChoice + ShowChoiceOption + ShowChoiceEnd を統合 ─────────
        if (code == LcfCode::ShowChoice) {
            std::string sv = lcf_str(cmd.string);
            int32_t cancel = cmd.parameters.size() > 0 ? cmd.parameters[0] : 0;

            std::vector<std::string> opts;
            {
                std::istringstream ss(sv);
                std::string tok;
                while (std::getline(ss, tok, '/')) opts.push_back(tok);
            }

            out << indent_str(vis) << "choice(";
            for (size_t o = 0; o < opts.size(); ++o) {
                if (o) out << ", ";
                out << quote_str(opts[o]);
            }
            if (cancel != 0) out << ", cancel=" << cancel;
            out << ") {\n";

            ++i;
            while (i < n && static_cast<LcfCode>(cmds[i].code) != LcfCode::ShowChoiceEnd) {
                if (static_cast<LcfCode>(cmds[i].code) == LcfCode::ShowChoiceOption) {
                    std::string label = lcf_str(cmds[i].string);
                    int wv = cmds[i].indent + base_indent;
                    // params[0] がオプションインデックス（存在する場合のみ出力）
                    out << indent_str(wv) << "when(" << quote_str(label);
                    if (!cmds[i].parameters.empty())
                        out << ", index=" << cmds[i].parameters[0];
                    out << ") {\n";
                    ++i;
                    // when 本体を再帰的に処理 (ShowChoiceOption / ShowChoiceEnd で停止)
                    decode_cmds_impl(cmds, i, n, base_indent, out,
                                     LcfCode::ShowChoiceOption, LcfCode::ShowChoiceEnd);
                    out << indent_str(wv) << "}\n";
                } else {
                    ++i;
                }
            }
            if (i < n) ++i; // ShowChoiceEnd を消費
            out << indent_str(vis) << "}\n";
            continue;
        }

        // ── ControlSwitches → switch[N] = OP; ───────────────────────────
        if (code == LcfCode::ControlSwitches) {
            out << decode_control_switches(cmd, vis) << "\n";
            ++i; continue;
        }

        // ── ControlVars → var[N] op= val; ───────────────────────────────
        if (code == LcfCode::ControlVars) {
            out << decode_control_vars(cmd, vis) << "\n";
            ++i; continue;
        }

        // ── ConditionalBranch ────────────────────────────────────────────
        if (code == LcfCode::ConditionalBranch) {
            out << indent_str(vis) << "if (" << decode_cond(cmd) << ") {\n";
            ++i; continue;
        }
        if (code == LcfCode::ElseBranch || code == LcfCode::ElseBranch_B) {
            // ElseBranch の cmd.indent は ConditionalBranch と同じ値なので
            // vis = cmd.indent + base_indent をそのまま使う（if と同レベル）
            out << indent_str(vis) << "} else {\n";
            ++i; continue;
        }
        if (code == LcfCode::EndBranch || code == LcfCode::EndBranch_B) {
            // EndBranch の cmd.indent は ConditionalBranch と同じ値
            // vis をそのまま使うことで、if { ... } の閉じ } と同レベルに揃う
            out << indent_str(vis) << "}\n";
            ++i; continue;
        }

        // ── Loop ─────────────────────────────────────────────────────────
        if (code == LcfCode::Loop) {
            out << indent_str(vis) << "Loop() {\n";
            ++i; continue;
        }
        if (code == LcfCode::EndLoop) {
            // EndLoop の cmd.indent は Loop と同じ値
            out << indent_str(vis) << "}  // end loop\n";
            ++i; continue;
        }

        // ── ShowChoiceOption/End が単独で来た場合 ────────────────────────
        if (code == LcfCode::ShowChoiceOption) {
            out << indent_str(vis) << "// choice_option: " << lcf_str(cmd.string) << "\n";
            ++i; continue;
        }
        if (code == LcfCode::ShowChoiceEnd) {
            ++i; continue;
        }

        // ── Comment / Comment_2 ──────────────────────────────────────────
        // comment("text") 形式でデコード → エンコード時に Comment + Comment_2 を復元
        if (code == LcfCode::Comment) {
            // 連続する Comment_2 を結合して1つの comment() にする
            std::string text = lcf_str(cmd.string);
            ++i;
            while (i < n && static_cast<LcfCode>(cmds[i].code) == LcfCode::Comment_2) {
                text += '\n';
                text += lcf_str(cmds[i].string);
                ++i;
            }
            out << indent_str(vis) << "comment(" << quote_str(text) << ");\n";
            continue;
        }
        if (code == LcfCode::Comment_2) {
            // 孤立した Comment_2 (通常はComment後にくるが念のため)
            out << indent_str(vis) << "comment(" << quote_str(lcf_str(cmd.string)) << ");\n";
            ++i; continue;
        }

        // ── 汎用: レジストリ検索 ──────────────────────────────────────────
        {
            const CmdDef* def = CommandRegistry::instance().find_by_code(code);
            if (def && def->decode) {
                out << indent_str(vis) << def->decode(cmd) << ";\n";
            } else {
                // 未知コマンド
                std::string r = "// [unknown cmd=" + std::to_string(cmd.code) + " str=\""
                    + escape_for_comment(lcf_str(cmd.string)) + "\" params=[";
                for (size_t j = 0; j < cmd.parameters.size(); ++j) {
                    if (j) r += ',';
                    r += std::to_string(cmd.parameters[j]);
                }
                r += "]]";
                out << indent_str(vis) << r << "\n";
            }
        }
        ++i;
    }
}

std::string Decoder::decode_event_commands(
    const std::vector<lcf::rpg::EventCommand>& cmds,
    int base_indent)
{
    register_all_commands();
    std::ostringstream out;
    size_t i = 0;
    decode_cmds_impl(cmds, i, cmds.size(), base_indent, out);
    return out.str();
}

// decode_one_cmd は ChoiceOption/End 等が内部で呼ばれた場合の fallback として残す
std::string Decoder::decode_one_cmd(const lcf::rpg::EventCommand& cmd,
                                    int /*visual_indent*/) {
    register_all_commands();
    auto code = static_cast<LcfCode>(cmd.code);

    if (code == LcfCode::ConditionalBranch)
        return "if (" + decode_cond(cmd) + ") {";
    if (code == LcfCode::ElseBranch || code == LcfCode::ElseBranch_B)
        return "} else {";
    if (code == LcfCode::EndBranch || code == LcfCode::EndBranch_B)
        return "}";

    if (code == LcfCode::Loop)    return "Loop() {";
    if (code == LcfCode::EndLoop) return "}  // end loop";
    if (code == LcfCode::BreakLoop) return "BreakLoop();";

    if (code == LcfCode::ControlSwitches) {
        // decode_event_commands で処理済みのはずだが fallback
        auto p = [&](size_t i) -> int32_t { return i < cmd.parameters.size() ? cmd.parameters[i] : 0; };
        int32_t id1 = p(1);
        int32_t val = p(3);
        const char* op = (val == 0) ? "OFF" : (val == 1) ? "ON" : "toggle";
        return "switch[" + std::to_string(id1) + "] = " + op + ";";
    }
    if (code == LcfCode::ControlVars) {
        auto p = [&](size_t i) -> int32_t { return i < cmd.parameters.size() ? cmd.parameters[i] : 0; };
        int32_t id1 = p(1);
        int32_t op  = p(3);
        int32_t src = p(4);
        int32_t val = p(5);
        static const char* ops[] = {"=","+=","-=","*=","/=","%="};
        std::string op_s = (op < 6) ? ops[op] : "=";
        if (src <= 1) {
            std::string rhs = (src == 0) ? std::to_string(val) : "var[" + std::to_string(val) + "]";
            return "var[" + std::to_string(id1) + "] " + op_s + " " + rhs + ";";
        }
        return "ControlVars(var=" + std::to_string(id1) + ", op_code=" + std::to_string(op)
             + ", src=" + std::to_string(src) + ", src_val=" + std::to_string(val) + ");";
    }

    if (code == LcfCode::ShowMessage) {
        return "message(" + quote_str(lcf_str(cmd.string)) + ");";
    }
    if (code == LcfCode::ShowMessage_2) {
        return "// msg_continue: " + quote_str(lcf_str(cmd.string));
    }
    if (code == LcfCode::ShowChoiceOption) {
        return "// choice_option: " + lcf_str(cmd.string);
    }
    if (code == LcfCode::ShowChoiceEnd) return "// choice_end";
    if (code == LcfCode::END) return "";

    if (code == LcfCode::Comment || code == LcfCode::Comment_2) {
        return "// " + lcf_str(cmd.string);
    }

    const CmdDef* def = CommandRegistry::instance().find_by_code(code);
    if (def && def->decode) {
        return def->decode(cmd) + ";";
    }

    // 未知コマンド
    std::string r = "// [unknown cmd=" + std::to_string(cmd.code) + " str=\""
        + escape_for_comment(lcf_str(cmd.string)) + "\" params=[";
    for (size_t j = 0; j < cmd.parameters.size(); ++j) {
        if (j) r += ',';
        r += std::to_string(cmd.parameters[j]);
    }
    r += "]]";
    return r;
}

// ──────────────────────────────────────────────────────────────────────────────
// decode_map: 新形式 event(N, x=.., y=..) { page(N, ...) { ... } }
// ──────────────────────────────────────────────────────────────────────────────

std::string Decoder::decode_map(const lcf::rpg::Map& map,
                                 const std::string& /*encoding*/) {
    register_all_commands();
    std::ostringstream out;

    static const char* triggers[]   = {"action","touched","collision","auto_start","parallel"};
    static const char* layers[]     = {"below","same","above"};
    static const char* move_types[] = {"stationary","random","vertical","horizontal","toward","away","custom"};
    static const char* move_speeds[]= {"","eighth","quarter","half","normal","double","fourfold"};
    static const char* anim_types[] = {"non_continuous","continuous","fixed_non_continuous",
                                        "fixed_continuous","fixed_graphic","spin","step_frame_fix"};
    static const char* dirs[]       = {"up","right","down","left"};
    static const char* cmp_ops[]    = {"eq","ge","le","gt","lt","ne"};

    for (const auto& ev : map.events) {
        std::string ev_name = lcf_str(ev.name);

        out << "event(" << ev.ID
            << ", x=" << ev.x
            << ", y=" << ev.y
            << ") {";
        if (!ev_name.empty()) out << "  // " << ev_name;
        out << "\n";

        for (const auto& page : ev.pages) {
            out << "    page(" << page.ID;

            int tr = page.trigger;
            if (tr >= 0 && tr < 5) out << ", trigger=" << triggers[tr];
            int ly = page.layer;
            if (ly >= 0 && ly < 3) out << ", layer=" << layers[ly];

            if (!page.character_name.empty()) {
                out << ", sprite=" << quote_str(lcf_str(page.character_name));
                out << ", sprite_index=" << page.character_index;
            }
            int cd = page.character_direction;
            if (cd >= 0 && cd < 4) out << ", dir=" << dirs[cd];

            int mt = page.move_type;
            if (mt >= 0 && mt < 7) out << ", move_type=" << move_types[mt];
            int ms = page.move_speed;
            if (ms >= 1 && ms <= 6) out << ", move_speed=" << move_speeds[ms];
            out << ", move_freq=" << page.move_frequency;

            int at = page.animation_type;
            if (at >= 0 && at < 7) out << ", anim_type=" << anim_types[at];

            if (page.translucent)       out << ", translucent=1";
            if (page.overlap_forbidden) out << ", overlap_forbidden=1";

            const auto& cond = page.condition;
            if (cond.flags.switch_a)
                out << ", cond_switch_a=" << cond.switch_a_id;
            if (cond.flags.switch_b)
                out << ", cond_switch_b=" << cond.switch_b_id;
            if (cond.flags.variable) {
                int op = cond.compare_operator;
                out << ", cond_var=" << cond.variable_id
                    << ", cond_var_op=" << (op >= 0 && op < 6 ? cmp_ops[op] : "ge")
                    << ", cond_var_val=" << cond.variable_value;
            }
            if (cond.flags.item)
                out << ", cond_item=" << cond.item_id;
            if (cond.flags.actor)
                out << ", cond_actor=" << cond.actor_id;
            if (cond.flags.timer)
                out << ", cond_timer=" << cond.timer_sec;
            if (cond.flags.timer2)
                out << ", cond_timer2=" << cond.timer2_sec;

            // ページ末尾 END の有無をメタに記録 (ゆめ2っきは末尾ENDなし)
            bool page_has_end = !page.event_commands.empty() &&
                                 page.event_commands.back().code == 10;
            if (page_has_end) out << ", has_end=1";
            out << ") {\n";
            // ページ本体のコマンド列 (base_indent=2 → 8スペース相当)
            std::string body = decode_event_commands(page.event_commands, 2);
            out << body;
            out << "    }\n";
        }
        out << "}\n\n";
    }
    return out.str();
}

// ──────────────────────────────────────────────────────────────────────────────
// decode_common_events
// ──────────────────────────────────────────────────────────────────────────────

std::string Decoder::decode_common_events(const lcf::rpg::Database& db,
                                           const std::string& /*encoding*/) {
    register_all_commands();
    std::ostringstream out;
    for (const auto& ce : db.commonevents) {
        std::string ce_name = lcf_str(ce.name);
        out << "event(" << ce.ID << ") {";
        if (!ce_name.empty()) out << "  // " << ce_name;
        out << "\n";

        out << "    page(1";
        static const char* trigs[] = {"none","auto_start","parallel"};
        int tr = ce.trigger;
        if (tr >= 0 && tr < 3) out << ", trigger=" << trigs[tr];
        if (ce.switch_flag)
            out << ", cond_switch=" << ce.switch_id;
        out << ") {\n";
        out << decode_event_commands(ce.event_commands, 2);
        out << "    }\n";
        out << "}\n\n";
    }
    return out.str();
}

} // namespace rm2k
