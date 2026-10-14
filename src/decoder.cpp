#include "codec.h"
#include "commands.h"
#include "textutil.h"
#include <lcf/rpg/event.h>
#include <lcf/rpg/eventpage.h>
#include <lcf/rpg/commonevent.h>
#include <sstream>
#include <string>
#include <algorithm>

namespace rm2k {

// ──────────────────────────────────────────────────────────────────────────────
// ユーティリティ
// ──────────────────────────────────────────────────────────────────────────────

static inline std::string quote_str(const std::string& s) { return quote_string(s); }

static std::string indent_str(int n) {
    return std::string(static_cast<size_t>(std::max(0, n * 4)), ' ');
}

static inline std::string lcf_str(const lcf::DBString& ds) {
    return std::string(ds.begin(), ds.end());
}

// ──────────────────────────────────────────────────────────────────────────────
// RawCommand: レジストリに無い (または高水準表現できない) コマンドを無損失で保持する形式
//   RawCommand(code=N, str="...", p0=.., p1=.., ...)
// エンコーダ側 (commands.cpp の "RawCommand") が同じ形式を読み戻す。
// 以前は「// [unknown cmd=...]」というコメントで出力しており、エンコード時に失われていた。
// ──────────────────────────────────────────────────────────────────────────────

static std::string raw_command_text(const lcf::rpg::EventCommand& cmd) {
    std::string r = "RawCommand(code=" + std::to_string(cmd.code);
    std::string s = lcf_str(cmd.string);
    if (!s.empty()) r += ", str=" + quote_str(s);
    for (size_t j = 0; j < cmd.parameters.size(); ++j)
        r += ", p" + std::to_string(j) + "=" + std::to_string(cmd.parameters[j]);
    r += ")";
    return r;
}

// ──────────────────────────────────────────────────────────────────────────────
// ConditionalBranch のデコード
// ──────────────────────────────────────────────────────────────────────────────

// param[0]: condition type
//   0 = switch, 1 = var compare, 2 = timer, 3 = gold, 4 = item, 5 = hero,
//   6 = char facing, 7 = vehicle, 8 = key decision, 9 = bgm looped, 10 = timer2

// extra_cond_params: type の「既知パラメータ数（type含む）」以降を cond_pN として追加
// また、実際のパラメータ数が known_total より少ない場合は cond_p_count=N を出力して
// エンコーダがバイナリを完全復元できるようにする。
static std::string extra_cond_params(const lcf::rpg::EventCommand& c, size_t known_total) {
    std::string r;
    size_t actual = c.parameters.size();
    if (actual < known_total) {
        // 標準より少ない要素数: エンコーダが同じ要素数で再生成できるよう記録
        r += ", cond_p_count=" + std::to_string(actual);
    } else {
        // 標準以上の要素数: known_total 以降を cond_pN として出力
        for (size_t i = known_total; i < actual; ++i)
            r += ", cond_p" + std::to_string(i) + "=" + std::to_string(c.parameters[i]);
    }
    return r;
}

// 高水準表現できない条件 (未知の type や範囲外の値) は cond_raw(p0=.., p1=.., [str=".."]) で無損失に保持する
static std::string raw_cond_text(const lcf::rpg::EventCommand& c) {
    std::string r = "cond_raw(";
    for (size_t j = 0; j < c.parameters.size(); ++j) {
        if (j) r += ", ";
        r += "p" + std::to_string(j) + "=" + std::to_string(c.parameters[j]);
    }
    std::string s = lcf_str(c.string);
    if (!s.empty()) r += std::string(c.parameters.empty() ? "" : ", ") + "str=" + quote_str(s);
    r += ")";
    return r;
}

static std::string decode_cond(const lcf::rpg::EventCommand& c,
                                const NameTable* names = nullptr) {
    auto p = [&](size_t i) { return i < c.parameters.size() ? c.parameters[i] : 0; };
    int32_t type = p(0);
    switch (type) {
        case 0: { // switch [type,id,val,p3,p4,p5]
            int32_t id  = p(1);
            int32_t val = p(2); // 0=ON, 1=OFF
            if (val != 0 && val != 1) return raw_cond_text(c);
            std::string sw_s = names ? names->sw_label(id) : std::to_string(id);
            std::string r = "switch[" + sw_s + "] == " + (val ? "OFF" : "ON");
            r += extra_cond_params(c, 3);
            return r;
        }
        case 1: { // var compare [type,id,src,val,op,p5]
            int32_t id  = p(1);
            int32_t src = p(2); // 0=const, 1=var
            int32_t val = p(3);
            static const char* ops[] = {"==",">=","<=",">","<","!="};
            int32_t op  = p(4);
            if (op < 0 || op > 5 || (src != 0 && src != 1)) return raw_cond_text(c);
            std::string v_s   = names ? names->var_label(id)  : std::to_string(id);
            std::string rhs_s = names ? names->var_label(val) : std::to_string(val);
            std::string rhs   = src ? "var[" + rhs_s + "]" : std::to_string(val);
            std::string r     = "var[" + v_s + "] " + ops[op] + " " + rhs;
            r += extra_cond_params(c, 5);
            return r;
        }
        case 2: { // timer [type,val,op,p3,p4,p5]
            int32_t val = p(1);
            int32_t op  = p(2); // 0=>=, 1=<=
            if (op != 0 && op != 1) return raw_cond_text(c);
            std::string r = "timer " + std::string(op ? "<=" : ">=") + " " + std::to_string(val);
            r += extra_cond_params(c, 3);
            return r;
        }
        case 3: { // gold [type,amount,op(0=>=,1=<=)]
            int32_t val = p(1);
            int32_t op  = p(2);
            if (op != 0 && op != 1) return raw_cond_text(c);
            std::string r = "gold_cmp(amount=" + std::to_string(val) + ", op=" + std::string(op?"le":"ge") + ")";
            r += extra_cond_params(c, 3);
            return r;
        }
        case 4: { // item [type,item_id,mode(0=has,1=not)]
            int32_t item = p(1);
            int32_t mode = p(2);
            std::string r = "has_item(id=" + std::to_string(item) + ", mode=" + std::to_string(mode) + ")";
            r += extra_cond_params(c, 3);
            return r;
        }
        case 5: { // hero/actor [type,actor,sub,val] + string(名前比較)
            // sub: 0=in_party, 1=name, 2=level, 3=hp, 4=skill, 5=equip, 6=state
            std::string r = "hero_cond(actor=" + std::to_string(p(1))
                 + ", sub=" + std::to_string(p(2)) + ", val=" + std::to_string(p(3));
            if (c.string.begin() != c.string.end())
                r += ", str=" + quote_str(lcf_str(c.string));
            r += ")";
            r += extra_cond_params(c, 4);
            return r;
        }
        case 6: { // char facing [type,event_id,dir(0=up,1=right,2=down,3=left)]
            int32_t who = p(1);
            int32_t dir = p(2);
            if (dir < 0 || dir > 3) return raw_cond_text(c);
            static const char* dirs[] = {"up","right","down","left"};
            std::string who_s = (who == 10001) ? "this" : std::to_string(who);
            std::string r = "char_facing(who=" + who_s + ", dir=" + dirs[dir] + ")";
            r += extra_cond_params(c, 3);
            return r;
        }
        case 7: { // vehicle [type,vehicle_id(0=boat,1=ship,2=airship)]
            static const char* vehicles[] = {"boat","ship","airship"};
            int32_t id = p(1);
            std::string r = "on_vehicle(id=" + std::string((id >= 0 && id < 3) ? vehicles[id] : std::to_string(id)) + ")";
            r += extra_cond_params(c, 2);
            return r;
        }
        case 8: { // key decision triggered this event
            std::string r = "key_decision()";
            r += extra_cond_params(c, 1);
            return r;
        }
        case 9: { // BGM looped at least once
            std::string r = "bgm_looped()";
            r += extra_cond_params(c, 1);
            return r;
        }
        case 10: { // timer2 [type,val,op(0=>=,1=<=)] (RPG2k3)
            int32_t val = p(1);
            int32_t op  = p(2);
            if (op != 0 && op != 1) return raw_cond_text(c);
            std::string r = "timer2 " + std::string(op ? "<=" : ">=") + " " + std::to_string(val);
            r += extra_cond_params(c, 3);
            return r;
        }
        default:
            return raw_cond_text(c);
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// decode_event_commands: ストリームをインデント付きで文字列化
// ──────────────────────────────────────────────────────────────────────────────

// ShowMessage + ShowMessage_2 を統合して message("...") に
// ShowChoice + ShowChoiceOption + ShowChoiceEnd を choice(...){when(){}} に
// ControlSwitches → switch[N] = OP;
// ControlVars (src<=1) → var[N] op= val;

static std::string decode_control_switches(const lcf::rpg::EventCommand& c, int ind,
                                            const NameTable* names = nullptr) {
    auto p = [&](size_t i) -> int32_t { return i < c.parameters.size() ? c.parameters[i] : 0; };
    int32_t type = p(0);
    int32_t id1  = p(1);
    int32_t id2  = p(2);
    int32_t val  = p(3);
    // op / type が標準外の場合は ControlSwitches キー形式で保持
    if (val < 0 || val > 2 || type < 0 || type > 2) {
        std::string r = "ControlSwitches(type=" + std::to_string(type)
            + ", id=" + std::to_string(id1)
            + ", id2=" + std::to_string(id2)
            + ", op_code=" + std::to_string(val) + ")";
        return indent_str(ind) + r + ";";
    }
    const char* op = (val == 0) ? "OFF" : (val == 1) ? "ON" : "toggle";
    std::string s = indent_str(ind);
    std::string sw1 = names ? names->sw_label(id1) : std::to_string(id1);
    std::string sw2 = names ? names->sw_label(id2) : std::to_string(id2);
    if (type == 0)
        s += "switch[" + sw1 + "] = " + op + ";";
    else if (type == 1)
        s += "switch[" + sw1 + ".." + sw2 + "] = " + op + ";";
    else
        s += "switch[var[" + std::to_string(id1) + "]] = " + op + ";";
    return s;
}

static std::string decode_control_vars(const lcf::rpg::EventCommand& c, int ind,
                                        const NameTable* names = nullptr) {
    auto p = [&](size_t i) -> int32_t { return i < c.parameters.size() ? c.parameters[i] : 0; };
    int32_t type = p(0);
    int32_t id1  = p(1);
    int32_t id2  = p(2);
    int32_t op   = p(3);
    int32_t src  = p(4);
    int32_t val  = p(5);
    static const char* ops[] = {"=","+=","-=","*=","/=","%="};

    // src<=1 かつ src_extra(p[6])=0 かつ type<=1（single/range）かつ op が標準の場合のみシンプル形式。
    // ただし元が7パラメータのとき (has_src_extra=true) はキー形式で保持してバイナリを保持する。
    // それ以外 (type=2 の間接指定、src>=2、範囲外の op など) もキー形式。
    bool has_src_extra = (c.parameters.size() > 6);
    int32_t src_extra = has_src_extra ? p(6) : 0;
    if (src >= 0 && src <= 1 && src_extra == 0 && type >= 0 && type <= 1 && op >= 0 && op < 6
        && !has_src_extra) {
        std::string v1 = names ? names->var_label(id1) : std::to_string(id1);
        std::string v2 = names ? names->var_label(id2) : std::to_string(id2);
        std::string lhs = (type == 0) ? "var[" + v1 + "]" : "var[" + v1 + ".." + v2 + "]";
        std::string rhs;
        if (src == 0) {
            rhs = std::to_string(val);
        } else {
            std::string rv = names ? names->var_label(val) : std::to_string(val);
            rhs = "var[" + rv + "]";
        }
        return indent_str(ind) + lhs + " " + ops[op] + " " + rhs + ";";
    }
    // キー形式: ControlVars(var=N, type=T, id2=M, op_code=op, src=src, src_val=val [,src_extra=p6])
    // src_extra は元が7パラメータのとき (has_src_extra=true) のみ出力（0でも）してバイナリを保持する
    std::string r = indent_str(ind) + "ControlVars(var=" + std::to_string(id1)
         + ", type=" + std::to_string(type)
         + ", id2=" + std::to_string(id2)
         + ", op_code=" + std::to_string(op)
         + ", src=" + std::to_string(src)
         + ", src_val=" + std::to_string(val);
    if (has_src_extra)
        r += ", src_extra=" + std::to_string(src_extra);
    r += ");";
    return r;
}

// decode_cmds_impl: i を参照で受け取り、stop_code に出会ったら止まる
// stop_code1/2/3 が LcfCode::END の場合は EOF まで
static void decode_cmds_impl(
    const std::vector<lcf::rpg::EventCommand>& cmds,
    size_t& i, size_t n, int base_indent,
    std::ostringstream& out,
    const NameTable* names,
    LcfCode stop1 = LcfCode::END,
    LcfCode stop2 = LcfCode::END,
    LcfCode stop3 = LcfCode::END)
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
        if (stop3 != LcfCode::END && code == stop3) break;

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
                    decode_cmds_impl(cmds, i, n, base_indent, out, names,
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
            out << decode_control_switches(cmd, vis, names) << "\n";
            ++i; continue;
        }

        // ── ControlVars → var[N] op= val; ───────────────────────────────
        if (code == LcfCode::ControlVars) {
            out << decode_control_vars(cmd, vis, names) << "\n";
            ++i; continue;
        }

        // ── ConditionalBranch ────────────────────────────────────────────
        if (code == LcfCode::ConditionalBranch) {
            out << indent_str(vis) << "if (" << decode_cond(cmd, names) << ") {\n";
            ++i; continue;
        }
        if (code == LcfCode::ElseBranch || code == LcfCode::ElseBranch_B) {
            // ElseBranch の cmd.indent は ConditionalBranch と同じ値なので
            // vis = cmd.indent + base_indent をそのまま使う（if と同レベル）
            out << indent_str(vis) << "} else {\n";
            ++i; continue;
        }
        if (code == LcfCode::EndBranch || code == LcfCode::EndBranch_B) {
            // params がある場合は EndBranch(p0=N) としてコマンド文で保持
            // params がない場合は } として出力
            if (!cmd.parameters.empty()) {
                out << indent_str(vis) << "EndBranch(";
                for (size_t pi = 0; pi < cmd.parameters.size(); ++pi) {
                    if (pi) out << ", ";
                    out << "p" << pi << "=" << cmd.parameters[pi];
                }
                out << ");\n";
            } else {
                out << indent_str(vis) << "}\n";
            }
            ++i; continue;
        }

        // ── Loop / EndLoop ───────────────────────────────────────────────
        if (code == LcfCode::Loop) {
            out << indent_str(vis) << "loop {\n";
            ++i; continue;
        }
        if (code == LcfCode::EndLoop) {
            out << indent_str(vis) << "}\n";
            ++i; continue;
        }
        if (code == LcfCode::BreakLoop) {
            out << indent_str(vis) << "break;\n";
            ++i; continue;
        }
        if (code == LcfCode::Label) {
            int32_t lid = cmd.parameters.empty() ? 0 : cmd.parameters[0];
            out << indent_str(vis) << lid << ":\n";
            ++i; continue;
        }
        if (code == LcfCode::JumpToLabel) {
            int32_t lid = cmd.parameters.empty() ? 0 : cmd.parameters[0];
            out << indent_str(vis) << "goto " << lid << ";\n";
            ++i; continue;
        }

        // ── EnemyEncounter { VictoryHandler {...} [EscapeHandler {...}] [DefeatHandler {...}] EndBattle {} } ──
        // 戦闘ブランチはハンドラー+EndBattle で構成される。
        // ハンドラー順は VictoryHandler → (EscapeHandler?) → (DefeatHandler?) → EndBattle。
        // 各ブロックの body は cmd.indent+1 で保存されている。
        // ブロック構文でデコードして再エンコード時に LCF indent を正しく復元する。
        if (code == LcfCode::EnemyEncounter) {
            const CmdDef* ee_def = CommandRegistry::instance().find_by_code(code);
            std::string ee_text = ee_def ? ee_def->decode(cmd) : raw_command_text(cmd);
            // 次のコマンドがハンドラーかどうかを確認
            // ハンドラーがない場合 (can_escape=0, can_lose=0 など) は通常コマンドとして出力
            bool has_handler = (i + 1 < n) && (
                cmds[i+1].code == static_cast<int>(LcfCode::VictoryHandler) ||
                cmds[i+1].code == static_cast<int>(LcfCode::EscapeHandler)  ||
                cmds[i+1].code == static_cast<int>(LcfCode::DefeatHandler)  ||
                cmds[i+1].code == static_cast<int>(LcfCode::EndBattle));
            if (!has_handler) {
                // ハンドラーなし: 通常コマンドとして出力
                out << indent_str(vis) << ee_text << ";\n";
                ++i;
                continue;
            }
            out << indent_str(vis) << ee_text << " {\n";
            ++i;
            // 各ハンドラーは EnemyEncounter と同じ LCF indent を持つ。
            // テキスト上では EnemyEncounter より1レベル深く出力する (inner = vis+1)。
            // body コマンドは EnemyEncounter より2レベル深く出力する。
            // body の LCF indent は (EnemyEncounter.indent + 1) なので、
            // decode_cmds_impl に base_indent+1 を渡すことでテキストインデントが inner+1 になる。
            int inner = vis + 1;
            // EnemyEncounter の LCF indent
            int ee_indent = cmd.indent;
            // 次の同 indent コマンドが EndBattle になるまで各ハンドラーをデコード
            // ただし VictoryHandler/EscapeHandler/DefeatHandler 以外の同インデントコマンドが来たら終了
            while (i < n) {
                // より浅いインデントのコマンドが来たら終了
                if (cmds[i].indent < ee_indent) break;
                int next_code = cmds[i].code;
                // EndBattle で終了
                if (next_code == static_cast<int>(LcfCode::EndBattle)) break;
                // ハンドラー以外の同インデントコマンドが来たら終了 (ハンドラーなし形式)
                if (cmds[i].indent == ee_indent
                    && next_code != static_cast<int>(LcfCode::VictoryHandler)
                    && next_code != static_cast<int>(LcfCode::EscapeHandler)
                    && next_code != static_cast<int>(LcfCode::DefeatHandler)) break;
                if (next_code == static_cast<int>(LcfCode::VictoryHandler)) {
                    out << indent_str(inner) << "VictoryHandler {\n";
                    ++i;
                    decode_cmds_impl(cmds, i, n, base_indent + 1, out, names,
                                     LcfCode::EscapeHandler, LcfCode::DefeatHandler,
                                     LcfCode::EndBattle);
                    out << indent_str(inner) << "}\n";
                } else if (next_code == static_cast<int>(LcfCode::EscapeHandler)) {
                    out << indent_str(inner) << "EscapeHandler {\n";
                    ++i;
                    decode_cmds_impl(cmds, i, n, base_indent + 1, out, names,
                                     LcfCode::DefeatHandler, LcfCode::EndBattle);
                    out << indent_str(inner) << "}\n";
                } else if (next_code == static_cast<int>(LcfCode::DefeatHandler)) {
                    out << indent_str(inner) << "DefeatHandler {\n";
                    ++i;
                    decode_cmds_impl(cmds, i, n, base_indent + 1, out, names,
                                     LcfCode::EscapeHandler, LcfCode::EndBattle);
                    out << indent_str(inner) << "}\n";
                } else {
                    // 未知のハンドラー: そのままデコード
                    const CmdDef* hdef = CommandRegistry::instance().find_by_code(
                        static_cast<LcfCode>(next_code));
                    if (hdef && hdef->decode)
                        out << indent_str(inner) << hdef->decode(cmds[i]) << ";\n";
                    else
                        out << indent_str(inner) << raw_command_text(cmds[i]) << ";\n";
                    ++i;
                }
            }
            // EndBattle (body は通常空、直後に END(indent) のみ)
            if (i < n && cmds[i].code == static_cast<int>(LcfCode::EndBattle)) {
                out << indent_str(inner) << "EndBattle {}\n";
                ++i;
            }
            out << indent_str(vis) << "}\n";
            continue;
        }

        // ── 構造外で現れた ShowChoiceOption/End/ShowMessage_2 ────────────
        // 以前はコメント出力 (エンコード時に消える) だったが、RawCommand で無損失に保持する
        if (code == LcfCode::ShowChoiceOption || code == LcfCode::ShowChoiceEnd
            || code == LcfCode::ShowMessage_2) {
            out << indent_str(vis) << raw_command_text(cmd) << ";\n";
            ++i; continue;
        }

        // ── Comment / Comment_2 ──────────────────────────────────────────
        // 各コマンドを独立して出力 (文字列中の改行はそのまま保持)
        if (code == LcfCode::Comment) {
            out << indent_str(vis) << "comment(" << quote_str(lcf_str(cmd.string)) << ");\n";
            ++i; continue;
        }
        if (code == LcfCode::Comment_2) {
            out << indent_str(vis) << "comment_cont(" << quote_str(lcf_str(cmd.string)) << ");\n";
            ++i; continue;
        }

        // ── 汎用: レジストリ検索 ──────────────────────────────────────────
        {
            const CmdDef* def = CommandRegistry::instance().find_by_code(code);
            if (def && def->decode) {
                out << indent_str(vis) << def->decode(cmd) << ";\n";
            } else {
                // 未登録コマンド: RawCommand で無損失に保持 (エンコード時に元のコマンドへ戻る)
                out << indent_str(vis) << raw_command_text(cmd) << ";\n";
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
    decode_cmds_impl(cmds, i, cmds.size(), base_indent, out, names_);
    return out.str();
}

// ──────────────────────────────────────────────────────────────────────────────
// decode_map: event(N, x=.., y=.., name="..") { page(N, ...) { ... } }
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
            << ", y=" << ev.y;
        if (!ev_name.empty()) out << ", name=" << quote_str(ev_name);
        out << ") {\n";

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
        out << "event(" << ce.ID;
        if (!ce_name.empty()) out << ", name=" << quote_str(ce_name);
        out << ") {\n";

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
