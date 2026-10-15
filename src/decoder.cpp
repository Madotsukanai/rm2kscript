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

// ユーティリティ

static inline std::string quote_str(const std::string& s) { return quote_string(s); }

static std::string indent_str(int n) {
    return std::string(static_cast<size_t>(std::max(0, n * 4)), ' ');
}

static inline std::string lcf_str(const lcf::DBString& ds) {
    return std::string(ds.begin(), ds.end());
}

// RawCommand: レジストリに無い (または高水準表現できない) コマンドを無損失で保持する形式
//   RawCommand(code=N, str="...", p0=.., p1=.., ...)
// エンコーダ側 (commands.cpp の "RawCommand") が同じ形式を読み戻す。
// 以前は「// [unknown cmd=...]」というコメントで出力しており、エンコード時に失われていた。

static std::string raw_command_text(const lcf::rpg::EventCommand& cmd) {
    std::string r = "RawCommand(code=" + std::to_string(cmd.code);
    std::string s = lcf_str(cmd.string);
    if (!s.empty()) r += ", str=" + quote_str(s);
    for (size_t j = 0; j < cmd.parameters.size(); ++j)
        r += ", p" + std::to_string(j) + "=" + std::to_string(cmd.parameters[j]);
    r += ")";
    return r;
}

// ConditionalBranch のデコード

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
            std::string who_s = char_ref_name(who);
            if (who_s.empty()) who_s = std::to_string(who);
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

// decode_event_commands: ストリームをインデント付きで文字列化

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
    int32_t type     = p(0);
    int32_t id1      = p(1);
    int32_t id2      = p(2);
    int32_t op       = p(3);
    int32_t src      = p(4);
    int32_t val      = p(5);
    int32_t extra    = p(6); // src_extra: src=3→rand上限, src=4/5/6/8→sub_op or actor/event id
    bool has_extra   = (c.parameters.size() > 6);
    static const char* ops[] = {"=","+=","-=","*=","/=","%="};

    // op が範囲外はキー形式にフォールバック
    if (op < 0 || op >= 6) goto key_form;

    {
        // LHS 構築
        std::string v1 = names ? names->var_label(id1) : std::to_string(id1);
        std::string v2 = names ? names->var_label(id2) : std::to_string(id2);
        std::string lhs;
        if      (type == 0) lhs = "var[" + v1 + "]";
        else if (type == 1) lhs = "var[" + v1 + ".." + v2 + "]";
        else if (type == 2) {
            // 間接参照: var[*var[N]] — 数値IDで出力（名前だとエンコード時の名前解決が困難）
            lhs = "var[*var[" + std::to_string(id1) + "]]";
        } else {
            goto key_form;
        }

        // RHS 構築
        std::string rhs;
        switch (src) {
            case 0: // 定数
                rhs = std::to_string(val);
                break;
            case 1: { // 変数
                std::string rv = names ? names->var_label(val) : std::to_string(val);
                rhs = "var[" + rv + "]";
                break;
            }
            case 2: { // 間接参照 *var[N]
                std::string rv = names ? names->var_label(val) : std::to_string(val);
                rhs = "*var[" + rv + "]";
                break;
            }
            case 3: // rand(lo, hi)
                rhs = "rand(" + std::to_string(val) + ", " + std::to_string(extra) + ")";
                break;
            case 4: { // item_count(id=N, op=M)
                // p[5]=item_id=val, p[6]=sub_op=extra
                static const char* item_ops[] = {"count", "equip"};
                std::string iop = (extra >= 0 && extra <= 1) ? item_ops[extra] : std::to_string(extra);
                rhs = "item_count(id=" + std::to_string(val) + ", op=" + iop + ")";
                break;
            }
            case 5: { // actor_stat(id=N, stat=S)
                // p[5]=actor_id=val, p[6]=sub_op=extra
                static const char* actor_stats[] = {
                    "level","exp","hp","mp","max_hp","max_mp",
                    "atk","def","spi","agi",
                    "weapon_id","shield_id","armor_id","helmet_id","accessory_id"
                };
                std::string stat = (extra >= 0 && extra <= 14) ? actor_stats[extra] : std::to_string(extra);
                rhs = "actor_stat(id=" + std::to_string(val) + ", stat=" + stat + ")";
                break;
            }
            case 6: { // event_stat(event=N, stat=S)
                // p[5]=event_id=val, p[6]=sub_op=extra
                static const char* event_stats[] = {"map_id","x","y","dir","screen_x","screen_y"};
                std::string stat = (extra >= 0 && extra <= 5) ? event_stats[extra] : std::to_string(extra);
                std::string eid  = char_ref_name(val);
                if (eid.empty()) eid = std::to_string(val);
                rhs = "event_stat(event=" + eid + ", stat=" + stat + ")";
                break;
            }
            case 7: { // system_stat(name)
                // val=sub_op, extra unused
                static const char* sys_stats[] = {
                    "gold","timer1","party_size","save_count",
                    "battle_count","win_count","defeat_count","escape_count","midi_pos","timer2"
                };
                std::string stat = (val >= 0 && val <= 9) ? sys_stats[val] : std::to_string(val);
                rhs = "system_stat(" + stat + ")";
                break;
            }
            case 8: { // enemy_stat(id=N, stat=S)
                // p[5]=enemy_id=val, p[6]=sub_op=extra
                static const char* enemy_stats[] = {"hp","mp","max_hp","max_mp","atk","def","spi","agi"};
                std::string stat = (extra >= 0 && extra <= 7) ? enemy_stats[extra] : std::to_string(extra);
                rhs = "enemy_stat(id=" + std::to_string(val) + ", stat=" + stat + ")";
                break;
            }
            default:
                goto key_form;
        }

        // has_extra=false(6パラメータ以下)かつ src>=3 は p[6] が省略されている。
        // エンコード時にバイナリを再現できないためキー形式にフォールバック。
        // has_extra=true(7パラメータ) の場合のみ C言語風関数記法を使う。
        // src=0〜2 は has_extra にかかわらず C言語風でよい（src_extra は EasyRPG が無視する）。
        if (src >= 3 && !has_extra) goto key_form;

        return indent_str(ind) + lhs + " " + ops[op] + " " + rhs + ";";
    }

key_form:
    {
        // キー形式: ControlVars(var=N, type=T, id2=M, op_code=op, src=src, src_val=val [,src_extra=p6])
        std::string r = indent_str(ind) + "ControlVars(var=" + std::to_string(id1)
             + ", type=" + std::to_string(type)
             + ", id2=" + std::to_string(id2)
             + ", op_code=" + std::to_string(op)
             + ", src=" + std::to_string(src)
             + ", src_val=" + std::to_string(val);
        if (has_extra)
            r += ", src_extra=" + std::to_string(extra);
        r += ");";
        return r;
    }
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

        // END (code=10) はブランチ内末尾にも挿入される。
        // indent=0 の END だけが本当の終端。indent>0 の END はスキップ。
        if (code == LcfCode::END) {
            if (cmd.indent == 0) break; // 本当の終端
            ++i; continue;             // ブランチ内 END はスキップ
        }
        if (stop1 != LcfCode::END && code == stop1) break;
        if (stop2 != LcfCode::END && code == stop2) break;
        if (stop3 != LcfCode::END && code == stop3) break;

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

        if (code == LcfCode::ControlSwitches) {
            out << decode_control_switches(cmd, vis, names) << "\n";
            ++i; continue;
        }

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

        // for (var[id] = init; cond; var[id] op= step) { body }
        // 条件: ControlVars(single=, =) → Loop → while パターン → 末尾ステップが同変数の ControlVars
        if (code == LcfCode::ControlVars) {
            // while パターンマッチを試みる前に for として認識できるか先読みする
            auto p = [&](const lcf::rpg::EventCommand& c, size_t k) -> int32_t {
                return k < c.parameters.size() ? c.parameters[k] : 0;
            };
            int32_t cv_type = p(cmd, 0);
            int32_t cv_id1  = p(cmd, 1);
            int32_t cv_id2  = p(cmd, 2);
            int32_t cv_op   = p(cmd, 3);
            int32_t cv_src  = p(cmd, 4);
            int32_t cv_val  = p(cmd, 5);
            bool cv_has_extra = (cmd.parameters.size() > 6);
            bool cv_simple = (cv_type == 0 && cv_id1 == cv_id2 && cv_op == 0
                              && cv_src >= 0 && cv_src <= 1 && !cv_has_extra);
            // 次が Loop かチェック
            size_t j = i + 1;
            bool next_is_loop = (j < n && static_cast<LcfCode>(cmds[j].code) == LcfCode::Loop
                                 && cmds[j].indent == cmd.indent);
            if (cv_simple && next_is_loop) {
                // while パターン先読み
                int32_t loop_ind = cmds[j].indent;
                // Loop の後に ConditionalBranch(ind+1), END(ind+2), ElseBranch(ind+1),
                // BreakLoop(ind+2), END(ind+2), EndBranch(ind+1) の6コマンドがあるか
                auto at = [&](size_t k) -> const lcf::rpg::EventCommand* {
                    return (k < n) ? &cmds[k] : nullptr;
                };
                auto code_at = [&](size_t k) -> LcfCode {
                    auto* c = at(k); return c ? static_cast<LcfCode>(c->code) : LcfCode::END;
                };
                auto ind_at = [&](size_t k) -> int32_t {
                    auto* c = at(k); return c ? c->indent : -1;
                };
                bool while_hdr = (
                    code_at(j+1) == LcfCode::ConditionalBranch && ind_at(j+1) == loop_ind+1 &&
                    code_at(j+2) == LcfCode::END               && ind_at(j+2) == loop_ind+2 &&
                    code_at(j+3) == LcfCode::ElseBranch        && ind_at(j+3) == loop_ind+1 &&
                    code_at(j+4) == LcfCode::BreakLoop         && ind_at(j+4) == loop_ind+2 &&
                    code_at(j+5) == LcfCode::END               && ind_at(j+5) == loop_ind+2 &&
                    code_at(j+6) == LcfCode::EndBranch         && ind_at(j+6) == loop_ind+1
                );
                if (while_hdr) {
                    // EndLoop の位置を探す
                    size_t el = j + 7;
                    while (el < n && !(static_cast<LcfCode>(cmds[el].code) == LcfCode::EndLoop
                                       && cmds[el].indent == loop_ind)) ++el;
                    // EndLoop の直前(END を除いた直前)の命令がステップ ControlVars か判定
                    // 構造: body... END(loop_ind+1) EndLoop(loop_ind)
                    // el-2 が END(loop_ind+1), el-1 が... 実際は el-1=END, el=EndLoop なので
                    // ステップは el-2 (END の前)
                    size_t step_idx = (el >= 2) ? el - 2 : n;
                    // END を飛ばして実際のステップ命令を探す
                    // (ENDはブランチ内末尾に挿入されるので連続する場合がある)
                    while (step_idx > j + 7 &&
                           static_cast<LcfCode>(cmds[step_idx].code) == LcfCode::END)
                        --step_idx;
                    const auto* sc = (step_idx > j + 6 && step_idx < n) ? &cmds[step_idx] : nullptr;
                    bool step_ok = false;
                    int32_t step_op = 0, step_val = 0, step_src = 0;
                    if (sc && static_cast<LcfCode>(sc->code) == LcfCode::ControlVars) {
                        auto sp = [&](size_t k) -> int32_t {
                            return k < sc->parameters.size() ? sc->parameters[k] : 0;
                        };
                        int32_t s_type = sp(0), s_id1 = sp(1), s_id2 = sp(2);
                        int32_t s_op = sp(3), s_src = sp(4);
                        bool s_extra = (sc->parameters.size() > 6);
                        // 同変数・single・op≠0(=は使わない)・src<=1・extra無し
                        step_ok = (s_type == 0 && s_id1 == cv_id1 && s_id2 == cv_id1
                                   && s_op >= 1 && s_op <= 5 && s_src >= 0 && s_src <= 1
                                   && !s_extra);
                        if (step_ok) { step_op = s_op; step_val = sp(5); step_src = s_src; }
                    }
                    if (step_ok) {
                        // for 文として出力
                        static const char* ops[] = {"=","+=","-=","*=","/=","%="};
                        std::string var_s = names ? names->var_label(cv_id1) : std::to_string(cv_id1);
                        std::string init_s;
                        if (cv_src == 0) init_s = std::to_string(cv_val);
                        else init_s = "var[" + (names ? names->var_label(cv_val) : std::to_string(cv_val)) + "]";
                        std::string step_rhs;
                        if (step_src == 0) step_rhs = std::to_string(step_val);
                        else step_rhs = "var[" + (names ? names->var_label(step_val) : std::to_string(step_val)) + "]";
                        std::string cond_s = decode_cond(cmds[j+1], names);
                        out << indent_str(vis) << "for (var[" << var_s << "] = " << init_s
                            << "; " << cond_s << "; var[" << var_s << "] " << ops[step_op]
                            << " " << step_rhs << ") {\n";
                        // body: j+7 から step_idx-1 まで (step と末尾ENDを除く)
                        size_t body_end = step_idx; // step自身は除く
                        // body_end が END なら更に前
                        while (body_end > j + 7 &&
                               static_cast<LcfCode>(cmds[body_end - 1].code) == LcfCode::END
                               && cmds[body_end - 1].indent > loop_ind)
                            --body_end;
                        size_t body_i = j + 7;
                        decode_cmds_impl(cmds, body_i, body_end, base_indent, out, names);
                        out << indent_str(vis) << "}\n";
                        i = el + 1; // EndLoop の次へ
                        continue;
                    }
                    // ステップが不明なら while にフォールバック（ControlVars は通常処理）
                }
            }
            // for に変換できない場合は通常の ControlVars 処理
            out << decode_control_vars(cmd, vis, names) << "\n";
            ++i; continue;
        }

        if (code == LcfCode::Loop) {
            // while(cond) パターンの認識:
            // Loop(N) ConditionalBranch(N+1) END(N+2) ElseBranch(N+1) BreakLoop(N+2) END(N+2) EndBranch(N+1)
            int32_t loop_ind = cmd.indent;
            auto code_at = [&](size_t k) -> LcfCode {
                return (k < n) ? static_cast<LcfCode>(cmds[k].code) : LcfCode::END;
            };
            auto ind_at = [&](size_t k) -> int32_t {
                return (k < n) ? cmds[k].indent : -1;
            };
            bool while_hdr = (
                code_at(i+1) == LcfCode::ConditionalBranch && ind_at(i+1) == loop_ind+1 &&
                code_at(i+2) == LcfCode::END               && ind_at(i+2) == loop_ind+2 &&
                code_at(i+3) == LcfCode::ElseBranch        && ind_at(i+3) == loop_ind+1 &&
                code_at(i+4) == LcfCode::BreakLoop         && ind_at(i+4) == loop_ind+2 &&
                code_at(i+5) == LcfCode::END               && ind_at(i+5) == loop_ind+2 &&
                code_at(i+6) == LcfCode::EndBranch         && ind_at(i+6) == loop_ind+1
            );
            if (while_hdr) {
                std::string cond_s = decode_cond(cmds[i+1], names);
                out << indent_str(vis) << "while (" << cond_s << ") {\n";
                // body: i+7 から EndLoop まで
                size_t body_i = i + 7;
                decode_cmds_impl(cmds, body_i, n, base_indent, out, names,
                                 LcfCode::EndLoop);
                // body_i は EndLoop を指している
                out << indent_str(vis) << "}\n";
                i = body_i + 1; // EndLoop の次へ
                continue;
            }
            out << indent_str(vis) << "while(true) {\n";
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

        // 以前はコメント出力 (エンコード時に消える) だったが、RawCommand で無損失に保持する
        if (code == LcfCode::ShowChoiceOption || code == LcfCode::ShowChoiceEnd
            || code == LcfCode::ShowMessage_2) {
            out << indent_str(vis) << raw_command_text(cmd) << ";\n";
            ++i; continue;
        }

        // ツクールのコメントコマンドは常に関数形式で出力する (// は無視されるメモ用で、コマンドにはならない)。
        //   Comment   → comment("...")
        //   Comment_2 → comment_cont("...")   (直前の Comment の続きの行)
        // 1コマンド=1文で出力するので、文字列中の改行や末尾の空白も無損失で往復できる。
        if (code == LcfCode::Comment) {
            out << indent_str(vis) << "comment(" << quote_str(lcf_str(cmd.string)) << ");\n";
            ++i; continue;
        }
        if (code == LcfCode::Comment_2) {
            out << indent_str(vis) << "comment_cont(" << quote_str(lcf_str(cmd.string)) << ");\n";
            ++i; continue;
        }

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

// decode_map: event(N, x=.., y=.., name="..") { page(N, ...) { ... } }

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

// decode_common_events

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
