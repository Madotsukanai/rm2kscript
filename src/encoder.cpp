#include "codec.h"
#include "commands.h"
#include "parser.h"
#include <lcf/rpg/event.h>
#include <lcf/rpg/eventpage.h>
#include <lcf/rpg/commonevent.h>
#include <stdexcept>
#include <variant>
#include <string>

namespace rm2k {

// ──────────────────────────────────────────────────────────────────────────────
// ConditionalBranch エンコード: if (cond_args) の引数を解釈
// ──────────────────────────────────────────────────────────────────────────────

// テキスト上の if 文の条件を LCF ConditionalBranch パラメータに変換
// cond_args は parse_arglist() の結果。
// 最もシンプルな形を想定:
//   switch[N] == ON/OFF
//   var[N] == value  / var[N] >= var[M] etc.
//   has_item(N)
//   gold >= N
// cond_args から "op" キーの演算子を取得するヘルパー
static std::string get_cond_op(const std::vector<Arg>& args) {
    // "op" キーを優先
    for (const auto& a : args) {
        if (a.key == "op" && a.val.kind == ArgVal::Ident) return a.val.sval;
    }
    // 位置1の位置引数 (旧形式フォールバック)
    return arg_ident(args, 1, "");
}

// cond_args の right-hand side (op の次の位置引数) を取得
static const Arg* get_cond_rhs(const std::vector<Arg>& args) {
    // "op" キーの後の最初の位置引数
    bool found_op = false;
    for (const auto& a : args) {
        if (a.key == "op") { found_op = true; continue; }
        if (found_op && a.key.empty()) return &a;
    }
    // フォールバック: 位置2
    return find_arg(args, 2, "");
}

// cond_pN キーを params に追加 (known_idx = 既知パラメータ開始インデックス)
static void append_cond_extra(const std::vector<Arg>& args, size_t known_idx,
                               std::vector<int32_t>& params) {
    // cond_pN キーを連続して探す
    for (size_t i = known_idx; ; ++i) {
        std::string key = "cond_p" + std::to_string(i);
        bool found = false;
        for (const auto& a : args) {
            if (a.key == key) { params.push_back(a.val.ival); found = true; break; }
        }
        if (!found) break;
    }
}

static std::vector<int32_t> encode_cond(const std::vector<Arg>& args,
                                         std::string* str_out = nullptr,
                                         const NameTable* names = nullptr) {
    // ConditionalBranch は常に6個のパラメータ (EasyRPG CmdSetup<...,6>)
    // デコード時に末尾の 0 を省略したため、ここで resize(6,0) して補完する
    auto pad6 = [](std::vector<int32_t> p) -> std::vector<int32_t> {
        p.resize(6, 0); return p;
    };

    if (args.empty()) return pad6({0, 1, 0}); // default: switch[1]==ON

    // switch[N] == ON/OFF
    const Arg* sw = nullptr;
    for (const auto& a : args) {
        if (a.key.empty() && a.val.kind == ArgVal::Indexed && a.val.sval == "switch") {
            sw = &a; break;
        }
        // key="switch" の明示的キー引数
        if (a.key == "switch") { sw = &a; break; }
    }

    if (sw) {
        int32_t id  = (!sw->val.sidx.empty() && names) ? names->sw_id(sw->val.sidx) : sw->val.ival;
        // "op" キーの次の引数 = ON/OFF/toggle
        std::string val_s;
        bool after_op = false;
        for (const auto& a : args) {
            if (a.key == "op") { after_op = true; continue; }
            if (after_op && a.key.empty()) { val_s = a.val.sval; break; }
        }
        if (val_s.empty()) val_s = arg_ident(args, 1, "");
        int32_t val = (val_s == "OFF") ? 1 : 0;
        std::vector<int32_t> p = {0, id, val};
        append_cond_extra(args, 3, p);
        return pad6(p);
    }

    // var[N] op value
    const Arg* va = nullptr;
    for (const auto& a : args) {
        if (a.key.empty() && a.val.kind == ArgVal::Indexed && a.val.sval == "var") {
            va = &a; break;
        }
        if (a.key == "var") { va = &a; break; }
    }

    if (va) {
        int32_t id = (!va->val.sidx.empty() && names) ? names->var_id(va->val.sidx) : va->val.ival;
        std::string op_s = get_cond_op(args);
        int32_t op_i = 0;
        if      (op_s == ">=") op_i = 1;
        else if (op_s == "<=") op_i = 2;
        else if (op_s == ">")  op_i = 3;
        else if (op_s == "<")  op_i = 4;
        else if (op_s == "!=") op_i = 5;
        int32_t src = 0, val = 0;
        const Arg* rhs = get_cond_rhs(args);
        if (rhs) {
            if (rhs->val.kind == ArgVal::Indexed && rhs->val.sval == "var") {
                src = 1;
                val = (!rhs->val.sidx.empty() && names) ? names->var_id(rhs->val.sidx) : rhs->val.ival;
            } else {
                src = 0; val = rhs->val.ival;
            }
        }
        std::vector<int32_t> p = {1, id, src, val, op_i};
        append_cond_extra(args, 5, p);
        return pad6(p);
    }

    // hero_cond(actor=N, sub=S, val=V [, str="名前"])  (ConditionalBranch type 5)
    for (const auto& a : args) {
        if (a.key == "hero_cond") {
            int32_t actor = 1, sub = 0, val = 0;
            for (const auto& b : args) {
                if (b.key == "actor") actor = b.val.ival;
                if (b.key == "sub")   sub   = b.val.ival;
                if (b.key == "val")   val   = b.val.ival;
                if (b.key == "str" && str_out) *str_out = b.val.sval;
            }
            std::vector<int32_t> p = {5, actor, sub, val};
            append_cond_extra(args, 4, p);
            return pad6(p);
        }
    }

    // has_item(id=N)
    for (const auto& a : args) {
        if (a.key == "has_item" || (a.key.empty() && a.val.sval == "has_item")) {
            int32_t item_id = 1;
            for (const auto& b : args) {
                if (b.key == "id") { item_id = b.val.ival; break; }
            }
            if (a.key.empty()) item_id = arg_int(args, 0, "", 1);
            std::vector<int32_t> p = {6, item_id};
            append_cond_extra(args, 2, p);
            return pad6(p);
        }
    }

    // on_vehicle(type=N)
    for (const auto& a : args) {
        if (a.key == "on_vehicle") {
            std::string t_s;
            for (const auto& b : args) {
                if (b.key == "type") { t_s = b.val.sval; break; }
            }
            int32_t t_i = 0;
            if      (t_s == "skiff")   t_i = 1;
            else if (t_s == "ship")    t_i = 0;
            else if (t_s == "airship") t_i = 2;
            else if (t_s == "any")     t_i = 3;
            std::vector<int32_t> p = {5, t_i};
            append_cond_extra(args, 2, p);
            return pad6(p);
        }
    }

    // char_facing(who=N, dir=D)
    for (const auto& a : args) {
        if (a.key == "char_facing") {
            int32_t who_i = 10001;
            std::string dir_s = "up";
            for (const auto& b : args) {
                if (b.key == "who") who_i = b.val.ival;
                if (b.key == "dir") dir_s = b.val.sval;
            }
            int32_t dir_i = 0;
            if      (dir_s == "right") dir_i = 1;
            else if (dir_s == "down")  dir_i = 2;
            else if (dir_s == "left")  dir_i = 3;
            std::vector<int32_t> p = {4, who_i, dir_i};
            append_cond_extra(args, 3, p);
            return pad6(p);
        }
    }

    // actor_cond(actor=N, cond=C, val=V)
    for (const auto& a : args) {
        if (a.key == "actor_cond") {
            int32_t actor = 1, cond = 0, val = 0;
            for (const auto& b : args) {
                if (b.key == "actor") actor = b.val.ival;
                if (b.key == "cond")  cond  = b.val.ival;
                if (b.key == "val")   val   = b.val.ival;
            }
            std::vector<int32_t> p = {3, actor, cond, val};
            append_cond_extra(args, 4, p);
            return pad6(p);
        }
    }

    // gold_cmp(gold=N, op=ge/le)
    for (const auto& a : args) {
        if (a.key == "gold_cmp") {
            int32_t gold_v = 0;
            std::string op_s = "ge";
            for (const auto& b : args) {
                if (b.key == "gold") gold_v = b.val.ival;
                if (b.key == "op")   op_s   = b.val.sval;
            }
            int32_t op_i = (op_s == "le") ? 1 : 0;
            std::vector<int32_t> p = {9, gold_v, op_i};
            append_cond_extra(args, 3, p);
            return pad6(p);
        }
    }

    // event_state(event=N, val=V)
    for (const auto& a : args) {
        if (a.key == "event_state") {
            int32_t event_id = 0, val = 0;
            for (const auto& b : args) {
                if (b.key == "event") event_id = b.val.ival;
                if (b.key == "val")   val      = b.val.ival;
            }
            std::vector<int32_t> p = {8, event_id, val};
            append_cond_extra(args, 3, p);
            return pad6(p);
        }
    }

    // has_equip(actor=N, item=M)
    for (const auto& a : args) {
        if (a.key == "has_equip") {
            int32_t actor = 1, item = 1;
            for (const auto& b : args) {
                if (b.key == "actor") actor = b.val.ival;
                if (b.key == "item")  item  = b.val.ival;
            }
            std::vector<int32_t> p = {7, actor, item};
            append_cond_extra(args, 3, p);
            return pad6(p);
        }
    }

    // timer op val
    for (const auto& a : args) {
        if (a.key.empty() && a.val.sval == "timer") {
            std::string op_s = get_cond_op(args);
            int32_t op_i = (op_s == "<=") ? 1 : 0;
            int32_t val = arg_int(args, 1, "", 0); // rhs
            const Arg* rhs = get_cond_rhs(args);
            if (rhs) val = rhs->val.ival;
            std::vector<int32_t> p = {2, val, op_i};
            append_cond_extra(args, 3, p);
            return pad6(p);
        }
    }

    // gold op N (旧形式)
    std::string key0 = arg_ident(args, 0, "");
    if (key0 == "gold") {
        std::string op_s = get_cond_op(args);
        int32_t op_i = (op_s == "<=") ? 1 : 0;
        int32_t val  = arg_int(args, 2, "");
        return pad6({9, val, op_i});
    }

    // fallback: switch[1]==ON
    return pad6({0, 1, 0});
}

// push_branch_end の前方宣言 (encode_stmt で使用するため)
static void push_branch_end(std::vector<lcf::rpg::EventCommand>& out, int32_t indent);

// ──────────────────────────────────────────────────────────────────────────────
// Encoder::encode_stmt
// ──────────────────────────────────────────────────────────────────────────────

void Encoder::encode_stmt(const StmtPtr& stmt_v, int32_t indent,
                          std::vector<lcf::rpg::EventCommand>& out) {
    std::visit([&](auto&& stmt) {
        using T = std::decay_t<decltype(stmt)>;
        if constexpr (std::is_same_v<T, IfStmt>) {
            encode_if(stmt, indent, out);
        } else if constexpr (std::is_same_v<T, ChoiceStmt>) {
            encode_choice(stmt, indent, out);
        } else if constexpr (std::is_same_v<T, SwitchAssign>) {
            encode_switch_assign(stmt, indent, out);
        } else if constexpr (std::is_same_v<T, VarAssign>) {
            encode_var_assign(stmt, indent, out);
        } else if constexpr (std::is_same_v<T, CmdStmt>) {
            register_all_commands();
            EncodeCtx ctx;
            ctx.indent = indent;
            const CmdDef* def = CommandRegistry::instance().find_by_name(stmt.name);
            if (def && def->encode) {
                def->encode(stmt, ctx, out);
            } else {
                // 未知コマンド: コメントとして記録
                LcfCmd c;
                c.code   = static_cast<int32_t>(LcfCode::Comment);
                c.indent = indent;
                c.string = lcf::DBString("unknown: " + stmt.name);
                out.push_back(c);
            }
            // Loop() { ... } などのボディがある場合: indent+1 でエンコード
            if (stmt.body) {
                for (const auto& st : stmt.body->stmts)
                    encode_stmt(st, indent + 1, out);
                // Loop の場合: ボディ末尾 END(indent+1) + EndLoop(indent) を追加
                if (stmt.name == "Loop") {
                    push_branch_end(out, indent + 1);
                    LcfCmd el;
                    el.code   = static_cast<int32_t>(LcfCode::EndLoop);
                    el.indent = indent;
                    out.push_back(el);
                }
            }
        }
    }, stmt_v);
}

// ブランチ節末尾に END (indent=depth) を挿入するヘルパー
// (encode_if と encode_choice で使用)
static void push_branch_end(std::vector<lcf::rpg::EventCommand>& out, int32_t indent) {
    LcfCmd c;
    c.code   = static_cast<int32_t>(LcfCode::END);
    c.indent = indent;
    out.push_back(c);
}

void Encoder::encode_choice(const ChoiceStmt& s, int32_t indent,
                             std::vector<lcf::rpg::EventCommand>& out) {
    // ShowChoice: string = "A/B/C", params[0] = cancel index
    std::string choices;
    for (const auto& opt : s.options) {
        if (!choices.empty()) choices += '/';
        choices += opt;
    }
    LcfCmd sc;
    sc.code   = static_cast<int32_t>(LcfCode::ShowChoice);
    sc.indent = indent;
    sc.string = lcf::DBString(choices);
    sc.parameters = lcf::DBArray<int32_t>{s.cancel};
    out.push_back(sc);

    // 各 when 節: ShowChoiceOption + body + END + (ShowChoiceEnd は最後の when 後に1つ)
    for (const auto& wc : s.whens) {
        LcfCmd opt_cmd;
        opt_cmd.code   = static_cast<int32_t>(LcfCode::ShowChoiceOption);
        opt_cmd.indent = indent;
        opt_cmd.string = lcf::DBString(wc.label);
        // RPGツクール2000: ShowChoiceOption.params[0] = 選択肢インデックス
        // index=-1 の場合はパラメータなし（古い形式）
        bool has_index = (wc.index >= 0);
        if (has_index)
            opt_cmd.parameters = lcf::DBArray<int32_t>{static_cast<int32_t>(wc.index)};
        out.push_back(opt_cmd);

        if (wc.body) {
            for (const auto& st : wc.body->stmts)
                encode_stmt(st, indent + 1, out);
        }
        // when 節の末尾 END: params[0] があった場合（新しい形式）のみ挿入
        if (has_index)
            push_branch_end(out, indent + 1);
    }

    // ShowChoiceEnd
    LcfCmd end_cmd;
    end_cmd.code   = static_cast<int32_t>(LcfCode::ShowChoiceEnd);
    end_cmd.indent = indent;
    out.push_back(end_cmd);
}

void Encoder::encode_switch_assign(const SwitchAssign& s, int32_t indent,
                                     std::vector<lcf::rpg::EventCommand>& out) {
    // ControlSwitches: params = [type, id1, id2, op]
    // type: 0=single, 1=range, 2=var_index
    // op: 0=OFF, 1=ON, 2=toggle
    int32_t id1 = (!s.id_label.empty() && names_) ? names_->sw_id(s.id_label) : s.id;
    int32_t type = s.is_var ? 2 : (s.is_range ? 1 : 0);
    int32_t id2_v;
    if (s.is_range) {
        id2_v = (!s.id2_label.empty() && names_) ? names_->sw_id(s.id2_label) : s.id2;
    } else if (s.is_var) {
        id2_v = 0;
    } else {
        id2_v = id1;
    }
    int32_t op   = (s.value == "ON") ? 1 : (s.value == "toggle") ? 2 : 0;

    // バリデーション: switch ID がデータベースの範囲内か
    if (db_ && !s.is_var) {
        int32_t sw_count = static_cast<int32_t>(db_->switches.size());
        if (sw_count > 0) {
            if (id1 < 1 || id1 > sw_count)
                throw CompileError("switch[" + std::to_string(id1) + "] is out of range (1.." + std::to_string(sw_count) + ")", s.loc);
            if (s.is_range && (id2_v < 1 || id2_v > sw_count))
                throw CompileError("switch[" + std::to_string(id2_v) + "] is out of range (1.." + std::to_string(sw_count) + ")", s.loc);
        }
    }

    LcfCmd c;
    c.code   = static_cast<int32_t>(LcfCode::ControlSwitches);
    c.indent = indent;
    c.parameters = lcf::DBArray<int32_t>{type, id1, id2_v, op};
    out.push_back(c);
}

void Encoder::encode_var_assign(const VarAssign& s, int32_t indent,
                                   std::vector<lcf::rpg::EventCommand>& out) {
    // ControlVars: params = [type, id1, id2, op, src, val]
    // type: 0=single, 1=range
    // op: 0==, 1+=, 2-=, 3*=, 4/=, 5%=
    // src: 0=const, 1=var
    int32_t id1   = (!s.id_label.empty() && names_) ? names_->var_id(s.id_label) : s.id;
    int32_t type  = s.is_range ? 1 : 0;
    int32_t id2_v;
    if (s.is_range) {
        id2_v = (!s.id2_label.empty() && names_) ? names_->var_id(s.id2_label) : s.id2;
    } else {
        id2_v = id1;
    }

    // バリデーション: var ID がデータベースの範囲内か
    if (db_) {
        int32_t var_count = static_cast<int32_t>(db_->variables.size());
        if (var_count > 0) {
            if (id1 < 1 || id1 > var_count)
                throw CompileError("var[" + std::to_string(id1) + "] is out of range (1.." + std::to_string(var_count) + ")", s.loc);
            if (s.is_range && (id2_v < 1 || id2_v > var_count))
                throw CompileError("var[" + std::to_string(id2_v) + "] is out of range (1.." + std::to_string(var_count) + ")", s.loc);
        }
    }
    int32_t op_i  = 0;
    if      (s.op == "+=") op_i = 1;
    else if (s.op == "-=") op_i = 2;
    else if (s.op == "*=") op_i = 3;
    else if (s.op == "/=") op_i = 4;
    else if (s.op == "%=") op_i = 5;

    int32_t src = 0, val = 0;
    if (s.rhs.kind == ArgVal::Indexed && s.rhs.sval == "var") {
        src = 1;
        val = (!s.rhs.sidx.empty() && names_) ? names_->var_id(s.rhs.sidx) : s.rhs.ival;
    } else {
        src = 0;
        val = s.rhs.ival;
    }

    LcfCmd c;
    c.code   = static_cast<int32_t>(LcfCode::ControlVars);
    c.indent = indent;
    // src_extra=0 を常に付与（RPGツクール2003は7個で保存するため）
    c.parameters = lcf::DBArray<int32_t>{type, id1, id2_v, op_i, src, val, 0};
    out.push_back(c);
}

void Encoder::encode_if(const IfStmt& s, int32_t indent,
                         std::vector<lcf::rpg::EventCommand>& out) {
    // ConditionalBranch
    std::string cond_str;
    auto params = encode_cond(s.cond_args, &cond_str, names_);
    LcfCmd cond_cmd;
    cond_cmd.code   = static_cast<int32_t>(LcfCode::ConditionalBranch);
    cond_cmd.indent = indent;
    cond_cmd.string = lcf::DBString(cond_str);
    cond_cmd.parameters = lcf::DBArray<int32_t>(params.begin(), params.end());
    out.push_back(cond_cmd);

    // then block
    if (s.then_body) {
        for (const auto& st : s.then_body->stmts)
            encode_stmt(st, indent + 1, out);
    }
    // RPGツクール2000: then 節の末尾に END (indent+1) を挿入
    push_branch_end(out, indent + 1);

    // else block
    if (s.else_body) {
        LcfCmd else_cmd;
        else_cmd.code   = static_cast<int32_t>(LcfCode::ElseBranch);
        else_cmd.indent = indent;
        out.push_back(else_cmd);
        for (const auto& st : s.else_body->stmts)
            encode_stmt(st, indent + 1, out);
        // else 節の末尾にも END
        push_branch_end(out, indent + 1);
    }

    // EndBranch (end_branch_params があれば params として付与)
    LcfCmd end_cmd;
    end_cmd.code   = static_cast<int32_t>(LcfCode::EndBranch);
    end_cmd.indent = indent;
    if (!s.end_branch_params.empty())
        end_cmd.parameters = lcf::DBArray<int32_t>(
            s.end_branch_params.begin(), s.end_branch_params.end());
    out.push_back(end_cmd);
}

std::vector<lcf::rpg::EventCommand>
Encoder::encode_stmtblock(const StmtBlock& block, int32_t base_indent, bool append_end) {
    std::vector<lcf::rpg::EventCommand> out;
    for (const auto& st : block.stmts)
        encode_stmt(st, base_indent, out);
    // LMUページは末尾 END (indent=0) が必要。LDBコモンイベントは不要。
    if (append_end) {
        LcfCmd end_cmd;
        end_cmd.code   = static_cast<int32_t>(LcfCode::END);
        end_cmd.indent = 0;
        out.push_back(end_cmd);
    }
    return out;
}

// ──────────────────────────────────────────────────────────────────────────────
// encode_to_map: テキストをパースして LMU マップに適用
// ──────────────────────────────────────────────────────────────────────────────

void Encoder::encode_to_map(const std::string& source,
                             lcf::rpg::Map& target_map,
                             const std::string& /*encoding*/) {
    register_all_commands();
    Lexer  lex(source);
    auto   tokens = lex.tokenize();
    Parser par(std::move(tokens));
    auto   doc = par.parse();

    // ルックアップテーブル
    static const std::string triggers[]   = {"action","touched","collision","auto_start","parallel"};
    static const std::string layers[]     = {"below","same","above"};
    static const std::string move_types[] = {"stationary","random","vertical","horizontal","toward","away","custom"};
    static const std::string move_speeds[]= {"","eighth","quarter","half","normal","double","fourfold"};
    static const std::string anim_types[] = {"non_continuous","continuous","fixed_non_continuous",
                                              "fixed_continuous","fixed_graphic","spin","step_frame_fix"};
    static const std::string dirs[]       = {"up","right","down","left"};
    static const std::string cmp_ops[]    = {"eq","ge","le","gt","lt","ne"};

    for (const auto& entry : doc.pages) {
        // イベントを探す or 作成
        lcf::rpg::Event* ev = nullptr;
        for (auto& e : target_map.events) {
            if (e.ID == entry.event_id) { ev = &e; break; }
        }
        if (!ev) {
            lcf::rpg::Event new_ev;
            new_ev.ID = entry.event_id;
            target_map.events.push_back(std::move(new_ev));
            ev = &target_map.events.back();
        }

        // イベント座標
        ev->x = entry.event_meta.x;
        ev->y = entry.event_meta.y;

        // ページを探す or 作成
        lcf::rpg::EventPage* pg = nullptr;
        for (auto& p : ev->pages) {
            if (p.ID == entry.page_id) { pg = &p; break; }
        }
        if (!pg) {
            lcf::rpg::EventPage new_pg;
            new_pg.ID = entry.page_id;
            ev->pages.push_back(std::move(new_pg));
            pg = &ev->pages.back();
        }

        const auto& m = entry.meta;

        // ── 基本 ────────────────────────────────────────────────────────
        for (int i = 0; i < 5; ++i)
            if (m.trigger == triggers[i]) { pg->trigger = i; break; }
        for (int i = 0; i < 3; ++i)
            if (m.layer == layers[i]) { pg->layer = i; break; }

        // ── グラフィック ──────────────────────────────────────────────
        if (!m.sprite.empty())
            pg->character_name = lcf::DBString(m.sprite);
        if (m.sprite_index >= 0)
            pg->character_index = m.sprite_index;
        for (int i = 0; i < 4; ++i)
            if (m.dir == dirs[i]) { pg->character_direction = i; break; }

        // ── 移動 ──────────────────────────────────────────────────────
        for (int i = 0; i < 7; ++i)
            if (m.move_type == move_types[i]) { pg->move_type = i; break; }
        for (int i = 1; i <= 6; ++i)
            if (m.move_speed == move_speeds[i]) { pg->move_speed = i; break; }
        if (m.move_freq >= 0)
            pg->move_frequency = m.move_freq;

        // ── アニメ ────────────────────────────────────────────────────
        for (int i = 0; i < 7; ++i)
            if (m.anim_type == anim_types[i]) { pg->animation_type = i; break; }

        // ── フラグ ────────────────────────────────────────────────────
        if (m.translucent >= 0)       pg->translucent       = (m.translucent != 0);
        if (m.overlap_forbidden >= 0) pg->overlap_forbidden = (m.overlap_forbidden != 0);

        // ── 発生条件 ──────────────────────────────────────────────────
        auto& cond = pg->condition;
        if (m.cond_switch_a) {
            cond.flags.switch_a = true;
            cond.switch_a_id    = m.cond_switch_a;
        }
        if (m.cond_switch_b) {
            cond.flags.switch_b = true;
            cond.switch_b_id    = m.cond_switch_b;
        }
        if (m.cond_var) {
            cond.flags.variable  = true;
            cond.variable_id     = m.cond_var;
            cond.variable_value  = m.cond_var_val;
            for (int i = 0; i < 6; ++i)
                if (m.cond_var_op == cmp_ops[i]) { cond.compare_operator = i; break; }
        }
        if (m.cond_item) {
            cond.flags.item = true;
            cond.item_id    = m.cond_item;
        }
        if (m.cond_actor) {
            cond.flags.actor = true;
            cond.actor_id    = m.cond_actor;
        }
        if (m.cond_timer >= 0) {
            cond.flags.timer = true;
            cond.timer_sec   = m.cond_timer;
        }
        if (m.cond_timer2 >= 0) {
            cond.flags.timer2 = true;
            cond.timer2_sec   = m.cond_timer2;
        }

        // コマンド列を生成して適用
        // has_end: -1=未指定(デフォルト=false), 0=なし, 1=あり
        // デコーダーが has_end=1 を明示した場合のみ END を追加
        bool want_end = (entry.meta.has_end == 1);
        pg->event_commands = encode_stmtblock(entry.body, 0, want_end);
    }
}

void Encoder::encode_to_database(const std::string& source,
                                   lcf::rpg::Database& db,
                                   const std::string& /*encoding*/) {
    register_all_commands();
    Lexer  lex(source);
    auto   tokens = lex.tokenize();
    Parser par(std::move(tokens));
    auto   doc = par.parse();

    for (const auto& entry : doc.pages) {
        // コモンイベントを探す (event_id が ID に相当)
        lcf::rpg::CommonEvent* ce = nullptr;
        for (auto& c : db.commonevents) {
            if (c.ID == entry.event_id) { ce = &c; break; }
        }
        if (!ce) {
            lcf::rpg::CommonEvent new_ce;
            new_ce.ID = entry.event_id;
            db.commonevents.push_back(std::move(new_ce));
            ce = &db.commonevents.back();
        }
        // LDBコモンイベントは末尾 END 不要
        ce->event_commands = encode_stmtblock(entry.body, 0, false);
    }
}

} // namespace rm2k
