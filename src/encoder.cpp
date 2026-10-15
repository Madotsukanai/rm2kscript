#include "codec.h"
#include "commands.h"
#include "parser.h"
#include <lcf/rpg/event.h>
#include <lcf/rpg/eventpage.h>
#include <lcf/rpg/commonevent.h>
#include <lcf/rpg/database.h>
#include <stdexcept>
#include <set>
#include <map>
#include <algorithm>
#include <variant>
#include <string>

namespace rm2k {

Encoder::Encoder() { register_all_commands(); }

// ConditionalBranch エンコード: if (cond_args) の引数を解釈

// テキスト上の if 文の条件を LCF ConditionalBranch パラメータに変換
// cond_args は parse_arglist() の結果。
// 最もシンプルな形を想定:
//   switch[N] == ON/OFF
//   var[N] == value  / var[N] >= var[M] etc.
//   has_item(N)
//   gold >= N
// cond_args から "op" キーの演算子を取得するヘルパー
// 名前 (または数値) → ID。名前指定で名前テーブルが無い/見つからない場合は CompileError
// (以前は ID 0 や var[1] に黙ってフォールバックしていた)
static int32_t resolve_sw_id(const std::string& label, int32_t ival,
                             const NameTable* names, SourceLoc loc) {
    if (label.empty()) return ival;
    if (!names)
        throw CompileError("switch[\"" + label + "\"]: a name table (RPG_RT.ldb) is required to use switch names", loc);
    auto id = names->find_sw(label);
    if (!id) throw CompileError("unknown switch name \"" + label + "\"", loc);
    return *id;
}

static int32_t resolve_var_id(const std::string& label, int32_t ival,
                              const NameTable* names, SourceLoc loc) {
    if (label.empty()) return ival;
    if (!names)
        throw CompileError("var[\"" + label + "\"]: a name table (RPG_RT.ldb) is required to use variable names", loc);
    auto id = names->find_var(label);
    if (!id) throw CompileError("unknown variable name \"" + label + "\"", loc);
    return *id;
}

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
// 返値: 追加後の params.size() (= trim0 の min_size として使う)
static size_t append_cond_extra(const std::vector<Arg>& args, size_t known_idx,
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
    return params.size();
}

// cond_p_count= の値を返す。なければ -1
static int32_t get_cond_p_count(const std::vector<Arg>& args) {
    for (const auto& a : args)
        if (a.key == "cond_p_count") return a.val.ival;
    return -1;
}

static std::vector<int32_t> encode_cond(const std::vector<Arg>& args,
                                         std::string* str_out = nullptr,
                                         const NameTable* names = nullptr,
                                         const lcf::rpg::Database* db = nullptr,
                                         SourceLoc loc = {}) {
    // ConditionalBranch のパラメータ列を min_size に調整する。
    // min_size より長い場合は末尾の連続ゼロを削除し、短い場合は0でパディングする。
    auto trim0 = [](std::vector<int32_t> p, size_t min_size) -> std::vector<int32_t> {
        while (p.size() > min_size && p.back() == 0)
            p.pop_back();
        while (p.size() < min_size)
            p.push_back(0);
        return p;
    };

    if (args.empty()) throw CompileError("empty condition", loc);

    // デコーダが出力した cond_p_count= があればそれをパラメータ数の上限として使う
    // (元のバイナリが標準より少ない要素数だった場合の復元)
    int32_t explicit_count = get_cond_p_count(args);
    // min_size 決定ヘルパー: cond_pN が追加された場合はその個数、
    // cond_p_count= が指定された場合はその値、どちらもなければ自然なサイズ
    auto final_size = [&](size_t before, size_t after) -> size_t {
        if (after > before) return after;                  // cond_pN あり
        if (explicit_count >= 0) return static_cast<size_t>(explicit_count); // cond_p_count あり
        return before;                                     // 自然サイズ
    };

    // switch[N] == true/false
    const Arg* sw = nullptr;
    for (const auto& a : args) {
        if (a.key.empty() && a.val.kind == ArgVal::Indexed && a.val.sval == "switch") {
            sw = &a; break;
        }
        // key="switch" の明示的キー引数
        if (a.key == "switch") { sw = &a; break; }
    }

    if (sw) {
        int32_t id = resolve_sw_id(sw->val.sidx, sw->val.ival, names, loc);
        // バリデーション
        if (db) {
            int32_t sw_count = static_cast<int32_t>(db->switches.size());
            if (sw_count > 0 && (id < 1 || id > sw_count))
                throw CompileError("switch[" + std::to_string(id) + "] is out of range (1.." + std::to_string(sw_count) + ")", loc);
        }
        // "op" キーの次の引数 = ON/OFF/toggle
        std::string val_s;
        bool after_op = false;
        for (const auto& a : args) {
            if (a.key == "op") { after_op = true; continue; }
            if (after_op && a.key.empty()) { val_s = a.val.sval; break; }
        }
        if (val_s.empty()) val_s = arg_ident(args, 1, "");
        if (val_s != "true" && val_s != "false")
            throw CompileError("switch condition must be '== true' or '== false'", loc);
        int32_t val = (val_s == "false") ? 1 : 0;
        std::vector<int32_t> p = {0, id, val};
        size_t before3 = p.size();
        size_t after3 = append_cond_extra(args, 3, p);
        // cond_pN が追加された場合はその個数まで保持（元バイナリの要素数を復元）
        // なければ自然なサイズ（3要素）を維持
        return trim0(p, final_size(before3, after3));
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
        int32_t id = resolve_var_id(va->val.sidx, va->val.ival, names, loc);
        // バリデーション
        if (db) {
            int32_t var_count = static_cast<int32_t>(db->variables.size());
            if (var_count > 0 && (id < 1 || id > var_count))
                throw CompileError("var[" + std::to_string(id) + "] is out of range (1.." + std::to_string(var_count) + ")", loc);
        }
        std::string op_s = get_cond_op(args);
        int32_t op_i = 0;
        if      (op_s == ">=") op_i = 1;
        else if (op_s == "<=") op_i = 2;
        else if (op_s == ">")  op_i = 3;
        else if (op_s == "<")  op_i = 4;
        else if (op_s == "!=") op_i = 5;
        else if (op_s != "==")
            throw CompileError("unknown comparison operator '" + op_s + "' (use == != >= <= > <)", loc);
        int32_t src = 0, val = 0;
        const Arg* rhs = get_cond_rhs(args);
        if (rhs) {
            if (rhs->val.kind == ArgVal::Indexed && rhs->val.sval == "var") {
                src = 1;
                val = resolve_var_id(rhs->val.sidx, rhs->val.ival, names, loc);
            } else {
                if (rhs->val.kind != ArgVal::Int)
                    throw CompileError("right-hand side of a variable comparison must be an integer or var[N]", loc);
                src = 0; val = rhs->val.ival;
            }
        }
        std::vector<int32_t> p = {1, id, src, val, op_i};
        size_t before5 = p.size();
        size_t after5 = append_cond_extra(args, 5, p);
        return trim0(p, final_size(before5, after5));
    }

    // cond_raw(p0=T, p1=.., [str=".."])  — デコーダが高水準表現できなかった条件を無損失で戻す
    for (const auto& a : args) {
        if (a.key == "cond_raw") {
            std::vector<int32_t> p;
            for (size_t i = 0; ; ++i) {
                std::string key = "p" + std::to_string(i);
                bool found = false;
                for (const auto& b : args)
                    if (b.key == key) { p.push_back(b.val.ival); found = true; break; }
                if (!found) break;
            }
            if (p.empty()) throw CompileError("cond_raw requires p0=...", loc);
            for (const auto& b : args)
                if (b.key == "str" && str_out) *str_out = b.val.sval;
            return p;
        }
    }

    // hero_cond(actor=N, sub=S, val=V [, str="名前"])  type=5
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
            size_t before_h = p.size();
            size_t after_h = append_cond_extra(args, 4, p);
            return trim0(p, final_size(before_h, after_h));
        }
    }

    // has_item(id=N, mode=0/1)  type=4  (0=has, 1=not)
    for (const auto& a : args) {
        if (a.key == "has_item" || (a.key.empty() && a.val.sval == "has_item")) {
            int32_t item_id = 1, mode = 0;
            for (const auto& b : args) {
                if (b.key == "id")   item_id = b.val.ival;
                if (b.key == "mode") mode    = b.val.ival;
            }
            if (a.key.empty()) item_id = arg_int(args, 0, "", 1);
            std::vector<int32_t> p = {4, item_id, mode};
            size_t before_i = p.size();
            size_t after_i = append_cond_extra(args, 3, p);
            return trim0(p, final_size(before_i, after_i));
        }
    }

    // on_vehicle(id=boat/ship/airship)  type=7
    for (const auto& a : args) {
        if (a.key == "on_vehicle") {
            std::string id_s;
            for (const auto& b : args) {
                if (b.key == "id") {
                    id_s = (b.val.kind == ArgVal::Int) ? std::to_string(b.val.ival) : b.val.sval;
                    break;
                }
            }
            int32_t id_i = 0;
            if      (id_s == "boat")    id_i = 0;
            else if (id_s == "ship")    id_i = 1;
            else if (id_s == "airship") id_i = 2;
            else {
                try { id_i = std::stoi(id_s); }
                catch (...) { throw CompileError("on_vehicle: id must be boat/ship/airship", loc); }
            }
            std::vector<int32_t> p = {7, id_i};
            size_t before_v = p.size();
            size_t after_v = append_cond_extra(args, 2, p);
            return trim0(p, final_size(before_v, after_v));
        }
    }

    // char_facing(who=N, dir=D)  type=6
    for (const auto& a : args) {
        if (a.key == "char_facing") {
            int32_t who_i = kCharThisEvent;
            std::string dir_s = "up";
            for (const auto& b : args) {
                if (b.key == "who") {
                    try { who_i = resolve_char_ref(b, "char_facing"); }
                    catch (const std::runtime_error& e) { throw CompileError(e.what(), loc); }
                }
                if (b.key == "dir") dir_s = b.val.sval;
            }
            int32_t dir_i = 0;
            if      (dir_s == "right") dir_i = 1;
            else if (dir_s == "down")  dir_i = 2;
            else if (dir_s == "left")  dir_i = 3;
            else if (dir_s != "up")
                throw CompileError("char_facing: dir must be up/right/down/left", loc);
            std::vector<int32_t> p = {6, who_i, dir_i};
            size_t before_c = p.size();
            size_t after_c = append_cond_extra(args, 3, p);
            return trim0(p, final_size(before_c, after_c));
        }
    }

    // gold_cmp(amount=N, op=ge/le)  type=3
    for (const auto& a : args) {
        if (a.key == "gold_cmp") {
            int32_t gold_v = 0;
            std::string op_s = "ge";
            for (const auto& b : args) {
                if (b.key == "amount" || b.key == "gold") gold_v = b.val.ival;
                if (b.key == "op")   op_s   = b.val.sval;
            }
            int32_t op_i = (op_s == "le") ? 1 : 0;
            std::vector<int32_t> p = {3, gold_v, op_i};
            size_t before_g = p.size();
            size_t after_g = append_cond_extra(args, 3, p);
            return trim0(p, final_size(before_g, after_g));
        }
    }

    // key_decision()  type=8
    for (const auto& a : args) {
        if (a.key == "key_decision") {
            std::vector<int32_t> p = {8};
            size_t before_kd = p.size();
            size_t after_kd = append_cond_extra(args, 1, p);
            return trim0(p, final_size(before_kd, after_kd));
        }
    }

    // bgm_looped()  type=9
    for (const auto& a : args) {
        if (a.key == "bgm_looped") {
            std::vector<int32_t> p = {9};
            size_t before_bl = p.size();
            size_t after_bl = append_cond_extra(args, 1, p);
            return trim0(p, final_size(before_bl, after_bl));
        }
    }

    // timer op val  type=2
    for (const auto& a : args) {
        if (a.key.empty() && a.val.sval == "timer") {
            std::string op_s = get_cond_op(args);
            int32_t op_i = (op_s == "<=") ? 1 : 0;
            int32_t val = arg_int(args, 1, "", 0);
            const Arg* rhs = get_cond_rhs(args);
            if (rhs) val = rhs->val.ival;
            std::vector<int32_t> p = {2, val, op_i};
            size_t before_t = p.size();
            size_t after_t = append_cond_extra(args, 3, p);
            return trim0(p, final_size(before_t, after_t));
        }
    }

    // timer2 op val  type=10 (RPG2k3)
    for (const auto& a : args) {
        if (a.key.empty() && a.val.sval == "timer2") {
            std::string op_s = get_cond_op(args);
            int32_t op_i = (op_s == "<=") ? 1 : 0;
            int32_t val = arg_int(args, 1, "", 0);
            const Arg* rhs = get_cond_rhs(args);
            if (rhs) val = rhs->val.ival;
            std::vector<int32_t> p = {10, val, op_i};
            size_t before_t2 = p.size();
            size_t after_t2 = append_cond_extra(args, 3, p);
            return trim0(p, final_size(before_t2, after_t2));
        }
    }

    // gold op N (旧形式フォールバック)
    std::string key0 = arg_ident(args, 0, "");
    if (key0 == "gold") {
        std::string op_s = get_cond_op(args);
        int32_t op_i = (op_s == "<=") ? 1 : 0;
        int32_t val  = arg_int(args, 2, "");
        return trim0({3, val, op_i}, 3);
    }

    // 未知/不正な条件はエラー (以前は switch[1]==ON に黙ってフォールバックしていた)
    throw CompileError("unsupported or malformed condition (see docs/language.md for supported conditions)", loc);
}

// ブランチ節末尾に END (indent=depth) を挿入するヘルパー
// (encode_stmt / encode_if / encode_choice / encode_while / encode_for で使用)
static void push_branch_end(std::vector<lcf::rpg::EventCommand>& out, int32_t indent) {
    LcfCmd c;
    c.code   = static_cast<int32_t>(LcfCode::END);
    c.indent = indent;
    out.push_back(c);
}

// Encoder::encode_stmt

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
        } else if constexpr (std::is_same_v<T, WhileStmt>) {
            encode_while(stmt, indent, out);
        } else if constexpr (std::is_same_v<T, ForStmt>) {
            encode_for(stmt, indent, out);
        } else if constexpr (std::is_same_v<T, LoopStmt>) {
            // loop { body }  →  Loop; body; END(indent+1); EndLoop(indent)
            out.push_back(make_cmd(LcfCode::Loop, indent));
            if (stmt.body)
                for (const auto& st : stmt.body->stmts)
                    encode_stmt(st, indent + 1, out);
            push_branch_end(out, indent + 1);
            LcfCmd el;
            el.code   = static_cast<int32_t>(LcfCode::EndLoop);
            el.indent = indent;
            out.push_back(el);
        } else if constexpr (std::is_same_v<T, BreakStmt>) {
            out.push_back(make_cmd(LcfCode::BreakLoop, indent));
        } else if constexpr (std::is_same_v<T, GotoStmt>) {
            out.push_back(make_cmd(LcfCode::JumpToLabel, indent, {stmt.label_id}));
        } else if constexpr (std::is_same_v<T, LabelStmt>) {
            out.push_back(make_cmd(LcfCode::Label, indent, {stmt.label_id}));
        } else if constexpr (std::is_same_v<T, CmdStmt>) {
            // RawCommand(code=N, str="..", p0=.., p1=.., ...): デコーダが高水準表現できなかったコマンドをそのまま復元する
            if (stmt.name == "RawCommand") {
                bool has_code = false;
                LcfCmd raw;
                raw.indent = indent;
                std::string raw_str;
                for (const auto& a : stmt.args) {
                    if (a.key == "code") {
                        if (a.val.kind != ArgVal::Int)
                            throw CompileError("RawCommand: code must be an integer", stmt.loc);
                        raw.code = a.val.ival;
                        has_code = true;
                    } else if (a.key == "str") {
                        if (a.val.kind != ArgVal::Str)
                            throw CompileError("RawCommand: str must be a string", stmt.loc);
                        raw_str = a.val.sval;
                    } else if (a.key.size() >= 2 && a.key[0] == 'p'
                               && a.key.find_first_not_of("0123456789", 1) == std::string::npos) {
                        // pN は連続している必要がある (下で検証)
                    } else {
                        throw CompileError("RawCommand: unknown argument '" + a.key + "'", stmt.loc);
                    }
                }
                if (!has_code)
                    throw CompileError("RawCommand requires code=N", stmt.loc);
                std::vector<int32_t> rp;
                for (size_t i = 0; ; ++i) {
                    const std::string key = "p" + std::to_string(i);
                    const Arg* f = nullptr;
                    for (const auto& a : stmt.args)
                        if (a.key == key) { f = &a; break; }
                    if (!f) break;
                    if (f->val.kind != ArgVal::Int)
                        throw CompileError("RawCommand: " + key + " must be an integer", stmt.loc);
                    rp.push_back(f->val.ival);
                }
                size_t pn = 0;
                for (const auto& a : stmt.args)
                    if (a.key.size() >= 2 && a.key[0] == 'p') ++pn;
                if (pn != rp.size())
                    throw CompileError("RawCommand: parameters must be p0, p1, ... without gaps", stmt.loc);
                raw.string = lcf::DBString(raw_str);
                raw.parameters = lcf::DBArray<int32_t>(rp.begin(), rp.end());
                out.push_back(raw);
                return;
            }
            EncodeCtx ctx;
            ctx.indent = indent;
            ctx.names  = names_;
            const CmdDef* def = CommandRegistry::instance().find_by_name(stmt.name);
            if (def && def->encode) {
                size_t before = out.size();
                try {
                    def->encode(stmt, ctx, out);
                } catch (const CompileError&) {
                    throw;
                } catch (const std::exception& e) {
                    throw CompileError(stmt.name + ": " + e.what(), stmt.loc);
                }
                // バリデーション: 生成されたコマンドの switch/var ID をチェック
                if (db_) {
                    int32_t sw_count  = static_cast<int32_t>(db_->switches.size());
                    int32_t var_count = static_cast<int32_t>(db_->variables.size());
                    for (size_t ci = before; ci < out.size(); ++ci) {
                        const auto& cmd = out[ci];
                        int32_t code = cmd.code;
                        auto p = [&](size_t i) -> int32_t {
                            return i < cmd.parameters.size() ? cmd.parameters[i] : 0;
                        };
                        if (code == static_cast<int32_t>(LcfCode::ControlSwitches)) {
                            // params: [type, id1, id2, op]
                            // type=2 は var[N] 間接指定なので switch ID チェック不要
                            int32_t type = p(0), id1 = p(1), id2 = p(2);
                            if (sw_count > 0 && type != 2) {
                                if (id1 < 1 || id1 > sw_count)
                                    throw CompileError("switch[" + std::to_string(id1) + "] is out of range (1.." + std::to_string(sw_count) + ")", stmt.loc);
                                if (type == 1 && (id2 < 1 || id2 > sw_count))
                                    throw CompileError("switch[" + std::to_string(id2) + "] is out of range (1.." + std::to_string(sw_count) + ")", stmt.loc);
                            }
                        } else if (code == static_cast<int32_t>(LcfCode::ControlVars)) {
                            // params: [type, id1, id2, op, src, val, src_extra]
                            // type=2 は var[var[N]] 間接指定なので id チェック不要
                            int32_t type = p(0), id1 = p(1), id2 = p(2);
                            if (var_count > 0 && type != 2) {
                                if (id1 < 1 || id1 > var_count)
                                    throw CompileError("var[" + std::to_string(id1) + "] is out of range (1.." + std::to_string(var_count) + ")", stmt.loc);
                                if (type == 1 && (id2 < 1 || id2 > var_count))
                                    throw CompileError("var[" + std::to_string(id2) + "] is out of range (1.." + std::to_string(var_count) + ")", stmt.loc);
                            }
                        }
                    }
                }
            } else {
                // 未知コマンドはエラー (以前は「unknown: X」コメントに黙って変換していた)
                throw CompileError("unknown command '" + stmt.name + "'", stmt.loc);
            }
            // Loop() { ... } などのボディがある場合
            if (stmt.body) {
                if (stmt.name == "EnemyEncounter") {
                        // EnemyEncounter { VictoryHandler {...} EscapeHandler {...} EndBattle {} } の処理
                        // body 内の各ハンドラーは EnemyEncounter と同じ indent でエンコードし、
                        // 各 body は indent+1 でエンコードする。
                        for (const auto& sub_v : stmt.body->stmts) {
                            // sub_v は CmdStmt の VictoryHandler / EscapeHandler / EndBattle のはず
                            if (!std::holds_alternative<CmdStmt>(sub_v)) {
                                encode_stmt(sub_v, indent, out);
                                continue;
                            }
                            const auto& sub = std::get<CmdStmt>(sub_v);
                            if (sub.name == "VictoryHandler") {
                                // VictoryHandler (indent) + body (indent+1) + END(indent+1)
                                out.push_back(make_cmd(LcfCode::VictoryHandler, indent));
                                if (sub.body)
                                    for (const auto& st : sub.body->stmts)
                                        encode_stmt(st, indent + 1, out);
                                push_branch_end(out, indent + 1);
                            } else if (sub.name == "EscapeHandler") {
                                // EscapeHandler (indent) + body (indent+1) + END(indent+1)
                                out.push_back(make_cmd(LcfCode::EscapeHandler, indent));
                                if (sub.body)
                                    for (const auto& st : sub.body->stmts)
                                        encode_stmt(st, indent + 1, out);
                                push_branch_end(out, indent + 1);
                            } else if (sub.name == "DefeatHandler") {
                                // DefeatHandler (indent) + body (indent+1) + END(indent+1)
                                out.push_back(make_cmd(LcfCode::DefeatHandler, indent));
                                if (sub.body)
                                    for (const auto& st : sub.body->stmts)
                                        encode_stmt(st, indent + 1, out);
                                push_branch_end(out, indent + 1);
                            } else if (sub.name == "EndBattle") {
                                // EndBattle (indent)
                                // 末尾 END(indent) は上位ブランチ (if/Loop/page) が追加するので不要
                                out.push_back(make_cmd(LcfCode::EndBattle, indent));
                            } else {
                                // 未知のサブコマンドはそのままエンコード
                                encode_stmt(sub_v, indent, out);
                            }
                        }
                } else {
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
        }
    }, stmt_v);
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
    int32_t id1 = resolve_sw_id(s.id_label, s.id, names_, s.loc);
    int32_t type = s.is_var ? 2 : (s.is_range ? 1 : 0);
    int32_t id2_v;
    if (s.is_range) {
        id2_v = resolve_sw_id(s.id2_label, s.id2, names_, s.loc);
    } else if (s.is_var) {
        id2_v = 0;
    } else {
        id2_v = id1;
    }
    if (s.value != "true" && s.value != "false" && s.value != "toggle")
        throw CompileError("switch assignment must be true, false or toggle (got '" + s.value + "')", s.loc);
    int32_t op   = (s.value == "true") ? 1 : (s.value == "toggle") ? 2 : 0;

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
    int32_t id1   = resolve_var_id(s.id_label, s.id, names_, s.loc);
    int32_t type  = s.is_range ? 1 : 0;
    int32_t id2_v;
    if (s.is_range) {
        id2_v = resolve_var_id(s.id2_label, s.id2, names_, s.loc);
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
        val = resolve_var_id(s.rhs.sidx, s.rhs.ival, names_, s.loc);
    } else {
        src = 0;
        if (s.rhs.kind != ArgVal::Int)
            throw CompileError("right-hand side of a variable assignment must be an integer or var[N]", s.loc);
        val = s.rhs.ival;
    }

    LcfCmd c;
    c.code   = static_cast<int32_t>(LcfCode::ControlVars);
    c.indent = indent;
    // src_extra=0 は末尾0として省略する (EasyRPG は CmdSetup<...,7> で実行時に補完する)
    // RPGツクール2000 が保存した 6 パラメータのファイルにも対応するため末尾0を付与しない
    c.parameters = lcf::DBArray<int32_t>{type, id1, id2_v, op_i, src, val};
    out.push_back(c);
}

// while (cond) { body }
//   展開: Loop { if (!cond) { BreakLoop; } body } EndLoop

void Encoder::encode_while(const WhileStmt& s, int32_t indent,
                             std::vector<lcf::rpg::EventCommand>& out) {
    // Loop
    out.push_back(make_cmd(LcfCode::Loop, indent));

    // if (!cond) { BreakLoop }
    // cond を反転させる: switch == ON → OFF, var == N → !=N, etc.
    // 最も単純な実装: 条件を encode_cond して ConditionalBranch を生成し、
    // then 節に BreakLoop を入れる（条件が偽の時ループを抜ける = 正の if に BreakLoop）
    // → cond が「真の間継続」なので「偽の時 break」= if (cond が偽) BreakLoop
    // encode_cond はそのまま「真の時 then」なので、cond_args を反転させる必要がある
    // ここでは「if (cond) { // body } else { BreakLoop }」パターンではなく、
    // ツクール標準的な「if (!cond) { BreakLoop }」に相当するコードを生成するため、
    // cond_args を「真のとき break」用に反転して encode_cond に渡す。
    // 簡単のため: cond_args を if-then に使い、then = 空、else = BreakLoop としてもよい。
    // → ここでは IfStmt と同じ構造で cond_args をそのまま渡し、
    //    then = 空（continueに相当）、else = BreakLoop と生成する
    // ただしそれだと逆になる。正しくは:
    //    ConditionalBranch(cond反転) { BreakLoop } EndBranch で1命令
    // → encode_cond_inverted を作るのは複雑なので、
    //    if (cond) {} else { BreakLoop } の形で生成する（等価）
    {
        std::string cond_str;
        auto params = encode_cond(s.cond_args, &cond_str, names_, db_, s.loc);
        // ConditionalBranch: cond が真の時 then（空）、偽の時 else（BreakLoop）
        LcfCmd cond_cmd;
        cond_cmd.code   = static_cast<int32_t>(LcfCode::ConditionalBranch);
        cond_cmd.indent = indent + 1;
        cond_cmd.string = lcf::DBString(cond_str);
        cond_cmd.parameters = lcf::DBArray<int32_t>(params.begin(), params.end());
        out.push_back(cond_cmd);
        // then 節: 空 → END のみ
        push_branch_end(out, indent + 2);
        // else 節: BreakLoop
        LcfCmd else_cmd;
        else_cmd.code   = static_cast<int32_t>(LcfCode::ElseBranch);
        else_cmd.indent = indent + 1;
        out.push_back(else_cmd);
        out.push_back(make_cmd(LcfCode::BreakLoop, indent + 2));
        push_branch_end(out, indent + 2);
        // EndBranch
        out.push_back(make_cmd(LcfCode::EndBranch, indent + 1));
    }

    // body
    if (s.body) {
        for (const auto& st : s.body->stmts)
            encode_stmt(st, indent + 1, out);
    }
    // END(indent+1) + EndLoop(indent)
    push_branch_end(out, indent + 1);
    out.push_back(make_cmd(LcfCode::EndLoop, indent));
}

// for (var[N] = init; cond; var[N] op= step) { body }
//   展開: var[N] = init; Loop { if (!cond) { BreakLoop } body var[N] op= step } EndLoop

void Encoder::encode_for(const ForStmt& s, int32_t indent,
                          std::vector<lcf::rpg::EventCommand>& out) {
    // init: var[N] = init_val
    int32_t var_id = resolve_var_id(s.var_label, s.var_id, names_, s.loc);
    int32_t src = 0, val = 0;
    if (s.init_val.kind == ArgVal::Indexed && s.init_val.sval == "var") {
        src = 1;
        val = resolve_var_id(s.init_val.sidx, s.init_val.ival, names_, s.loc);
    } else {
        src = 0; val = s.init_val.ival;
    }
    // ControlVars: type=0(single), id1=id, id2=id, op=0(=), src, val — 末尾0省略
    out.push_back(make_cmd(LcfCode::ControlVars, indent,
        trim_trailing_zeros({0, var_id, var_id, 0, src, val})));

    // Loop
    out.push_back(make_cmd(LcfCode::Loop, indent));

    // if (cond) {} else { BreakLoop }
    {
        std::string cond_str;
        auto params = encode_cond(s.cond_args, &cond_str, names_, db_, s.loc);
        LcfCmd cond_cmd;
        cond_cmd.code   = static_cast<int32_t>(LcfCode::ConditionalBranch);
        cond_cmd.indent = indent + 1;
        cond_cmd.string = lcf::DBString(cond_str);
        cond_cmd.parameters = lcf::DBArray<int32_t>(params.begin(), params.end());
        out.push_back(cond_cmd);
        push_branch_end(out, indent + 2);
        LcfCmd else_cmd;
        else_cmd.code   = static_cast<int32_t>(LcfCode::ElseBranch);
        else_cmd.indent = indent + 1;
        out.push_back(else_cmd);
        out.push_back(make_cmd(LcfCode::BreakLoop, indent + 2));
        push_branch_end(out, indent + 2);
        out.push_back(make_cmd(LcfCode::EndBranch, indent + 1));
    }

    // body
    if (s.body) {
        for (const auto& st : s.body->stmts)
            encode_stmt(st, indent + 1, out);
    }

    // step: var[N] op= step_val
    {
        VarAssign step_va;
        step_va.loc = s.loc;
        step_va.id  = var_id;
        step_va.op  = s.step_op;
        step_va.rhs = s.step_val;
        encode_var_assign(step_va, indent + 1, out);
    }

    // END(indent+1) + EndLoop(indent)
    push_branch_end(out, indent + 1);
    out.push_back(make_cmd(LcfCode::EndLoop, indent));
}

void Encoder::encode_if(const IfStmt& s, int32_t indent,
                         std::vector<lcf::rpg::EventCommand>& out) {
    // ConditionalBranch
    std::string cond_str;
    auto params = encode_cond(s.cond_args, &cond_str, names_, db_, s.loc);
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

// encode_to_map: テキストをパースして LMU マップに適用

// 列挙値文字列 → 整数。v が空なら何もしない。未知の値は CompileError (以前は黙って無視していた)
template <size_t N, typename T>
static void set_enum(const std::string (&tbl)[N], const std::string& v, size_t first,
                     T& dst, const char* what, SourceLoc loc) {
    if (v.empty()) return;
    for (size_t i = first; i < N; ++i) {
        if (v == tbl[i]) { dst = static_cast<T>(i); return; }
    }
    throw CompileError(std::string("unknown ") + what + " '" + v + "'", loc);
}

void Encoder::encode_to_map(const std::string& source,
                             lcf::rpg::Map& target_map,
                             const std::string& /*encoding*/) {
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

    std::set<std::pair<int, int>> seen_pages;        // (event, page) の重複定義を検出
    std::map<int, SourceLoc> events_with_new_pages;  // 新規ページを作ったイベント -> 位置 (ページ番号の連番検証用)

    for (const auto& entry : doc.pages) {
        // 同じ event/page を 2 回書くと後者が黙って前者を上書きするのでエラーにする
        if (!seen_pages.insert({entry.event_id, entry.page_id}).second)
            throw CompileError("duplicate definition of event " + std::to_string(entry.event_id)
                               + " page " + std::to_string(entry.page_id), entry.loc);
        // イベントを探す or 作成
        lcf::rpg::Event* ev = nullptr;
        for (auto& e : target_map.events) {
            if (e.ID == entry.event_id) { ev = &e; break; }
        }
        if (!ev) {
            lcf::rpg::Event new_ev;
            if (entry.event_meta.x < 0 || entry.event_meta.y < 0)
                throw CompileError("new event " + std::to_string(entry.event_id)
                                   + " requires x= and y=", entry.loc);
            new_ev.ID = entry.event_id;
            target_map.events.push_back(std::move(new_ev));
            ev = &target_map.events.back();
        }

        // イベント座標
        // イベント座標 / 名前 (省略時は既存の値を維持)
        if (entry.event_meta.x >= 0) ev->x = entry.event_meta.x;
        if (entry.event_meta.y >= 0) ev->y = entry.event_meta.y;
        if (entry.event_meta.has_name) ev->name = lcf::DBString(entry.event_meta.name);

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
            events_with_new_pages.emplace(ev->ID, entry.loc);
        }

        const auto& m = entry.meta;
        // cond_switch= はコモンイベント専用。マップページでは黙って無視され、発生条件のないページになっていた
        if (m.cond_switch != 0)
            throw CompileError("cond_switch= is only valid for common events; "
                               "use cond_switch_a= / cond_switch_b= for map pages", entry.loc);

        auto check_range = [&](int v, int lo, int hi, const char* name) {
            if (v < lo || v > hi)
                throw CompileError(std::string(name) + "=" + std::to_string(v)
                    + " is out of range (" + std::to_string(lo) + ".." + std::to_string(hi) + ")",
                    entry.loc);
        };
        auto check_min = [&](int v, int lo, const char* name) {
            if (v < lo)
                throw CompileError(std::string(name) + "=" + std::to_string(v)
                    + " must be >= " + std::to_string(lo), entry.loc);
        };
        if (m.sprite_index >= 0)    check_range(m.sprite_index,    0, 7, "sprite_index");
        if (m.move_freq >= 0)       check_range(m.move_freq,        1, 8, "move_freq");
        if (m.translucent >= 0)     check_range(m.translucent,      0, 1, "translucent");
        if (m.overlap_forbidden >= 0) check_range(m.overlap_forbidden, 0, 1, "overlap_forbidden");
        if (m.has_end >= 0)         check_range(m.has_end,          0, 1, "has_end");
        if (m.cond_switch_a != 0)   check_min(m.cond_switch_a,      1, "cond_switch_a");
        if (m.cond_switch_b != 0)   check_min(m.cond_switch_b,      1, "cond_switch_b");
        if (m.cond_var != 0)        check_min(m.cond_var,           1, "cond_var");
        if (m.cond_item != 0)       check_min(m.cond_item,          1, "cond_item");
        if (m.cond_actor != 0)      check_min(m.cond_actor,         1, "cond_actor");
        if (m.cond_timer >= 0)      check_min(m.cond_timer,         0, "cond_timer");
        if (m.cond_timer2 >= 0)     check_min(m.cond_timer2,        0, "cond_timer2");

        set_enum(triggers, m.trigger, 0, pg->trigger, "trigger", entry.loc);
        set_enum(layers,   m.layer,   0, pg->layer,   "layer",   entry.loc);

        // sprite= 省略時は「グラフィックなし」(デコーダも空のとき sprite を出力しない)。sprite="" で明示的に消去もできる
        pg->character_name = lcf::DBString(m.has_sprite ? m.sprite : std::string());
        if (m.sprite_index >= 0)
            pg->character_index = m.sprite_index;
        set_enum(dirs, m.dir, 0, pg->character_direction, "dir", entry.loc);

        set_enum(move_types, m.move_type, 0, pg->move_type, "move_type", entry.loc);
        set_enum(move_speeds, m.move_speed, 1, pg->move_speed, "move_speed", entry.loc);
        if (m.move_freq >= 0)
            pg->move_frequency = m.move_freq;

        set_enum(anim_types, m.anim_type, 0, pg->animation_type, "anim_type", entry.loc);

        // 省略時は false (デコーダは true のときだけ出力するので、テキストから消すと解除される)
        pg->translucent       = (m.translucent > 0);
        pg->overlap_forbidden = (m.overlap_forbidden > 0);

        auto& cond = pg->condition;
        // いったん全フラグを解除してからテキストの指定を適用する (テキストから条件を消したときに反映されるように)
        cond.flags.switch_a = false;
        cond.flags.switch_b = false;
        cond.flags.variable = false;
        cond.flags.item     = false;
        cond.flags.actor    = false;
        cond.flags.timer    = false;
        cond.flags.timer2   = false;
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
            if (!m.cond_var_op.empty())
                set_enum(cmp_ops, m.cond_var_op, 0, cond.compare_operator, "cond_var_op", entry.loc);
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

    // --prune: スクリプトに現れないイベント/ページを削除する
    if (prune_) {
        std::set<int> keep_events;
        std::set<std::pair<int, int>> keep_pages;
        for (const auto& entry : doc.pages) {
            keep_events.insert(entry.event_id);
            keep_pages.insert({entry.event_id, entry.page_id});
        }
        auto& evs = target_map.events;
        evs.erase(std::remove_if(evs.begin(), evs.end(),
                      [&](const lcf::rpg::Event& e) { return keep_events.count(e.ID) == 0; }),
                  evs.end());
        for (auto& e : evs) {
            auto& pgs = e.pages;
            const int eid = e.ID;
            pgs.erase(std::remove_if(pgs.begin(), pgs.end(),
                          [&](const lcf::rpg::EventPage& p) { return keep_pages.count({eid, p.ID}) == 0; }),
                      pgs.end());
        }
    }

    // 新規にページを作ったイベントは、ページ番号が 1..N の連番でなければならない。
    // (エンジンは page= をページ配列のインデックスとして扱うため、飛び番・順序違いは別ページを指してしまう)
    // 記述順が page(2) -> page(1) のように逆でも、ここで ID 順に並べ替える。
    for (auto& e : target_map.events) {
        auto it = events_with_new_pages.find(e.ID);
        if (it == events_with_new_pages.end()) continue;
        std::stable_sort(e.pages.begin(), e.pages.end(),
                         [](const lcf::rpg::EventPage& a, const lcf::rpg::EventPage& b) { return a.ID < b.ID; });
        for (size_t i = 0; i < e.pages.size(); ++i) {
            if (e.pages[i].ID != static_cast<int32_t>(i) + 1)
                throw CompileError("event " + std::to_string(e.ID)
                                   + ": page numbers must be consecutive starting from 1 (found page "
                                   + std::to_string(e.pages[i].ID) + " at position "
                                   + std::to_string(i + 1) + ")", it->second);
        }
    }
}

// マップイベント専用のページオプションが指定されていればそのキー名を返す (なければ nullptr)
// コモンイベントにこれらを書いても黙って無視されていた。
static const char* first_map_only_option(const PageMeta& m) {
    if (!m.layer.empty())                          return "layer";
    if (m.has_sprite || !m.sprite.empty())         return "sprite";
    if (m.sprite_index >= 0)                       return "sprite_index";
    if (!m.dir.empty())                            return "dir";
    if (!m.move_type.empty())                      return "move_type";
    if (!m.move_speed.empty())                     return "move_speed";
    if (m.move_freq >= 0)                          return "move_freq";
    if (!m.anim_type.empty())                      return "anim_type";
    if (m.translucent >= 0)                        return "translucent";
    if (m.overlap_forbidden >= 0)                  return "overlap_forbidden";
    if (m.cond_switch_a != 0)                      return "cond_switch_a";
    if (m.cond_switch_b != 0)                      return "cond_switch_b";
    if (m.cond_var != 0 || !m.cond_var_op.empty() || m.cond_var_val != 0) return "cond_var";
    if (m.cond_item != 0)                          return "cond_item";
    if (m.cond_actor != 0)                         return "cond_actor";
    if (m.cond_timer >= 0)                         return "cond_timer";
    if (m.cond_timer2 >= 0)                        return "cond_timer2";
    return nullptr;
}

void Encoder::encode_to_database(const std::string& source,
                                   lcf::rpg::Database& db,
                                   const std::string& /*encoding*/) {
    Lexer  lex(source);
    auto   tokens = lex.tokenize();
    Parser par(std::move(tokens));
    auto   doc = par.parse();

    std::set<int> seen_events;  // コモンイベントの重複定義を検出

    for (const auto& entry : doc.pages) {
        // コモンイベントは 1 ページのみ。page(2) 以降や重複定義が黙って上書きされないようにする
        if (entry.page_id != 1)
            throw CompileError("common events have exactly one page: use page(1) (got page "
                               + std::to_string(entry.page_id) + ")", entry.loc);
        if (!seen_events.insert(entry.event_id).second)
            throw CompileError("duplicate definition of common event "
                               + std::to_string(entry.event_id), entry.loc);
        if (const char* bad = first_map_only_option(entry.meta))
            throw CompileError(std::string("page option '") + bad
                               + "' is only valid for map events, not common events", entry.loc);
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
        // トリガー / トリガースイッチ / 名前を適用 (以前はコマンド列しか反映されなかった)
        {
            if (entry.meta.cond_switch < 0)
                throw CompileError("cond_switch=" + std::to_string(entry.meta.cond_switch)
                    + " must be >= 1", entry.loc);
            static const std::string ce_trigs[] = {"none", "auto_start", "parallel"};
            set_enum(ce_trigs, entry.meta.trigger, 0, ce->trigger, "common event trigger", entry.loc);
            if (entry.meta.cond_switch > 0) {
                ce->switch_flag = true;
                ce->switch_id   = entry.meta.cond_switch;
            } else {
                ce->switch_flag = false;
            }
            if (entry.event_meta.has_name) ce->name = lcf::DBString(entry.event_meta.name);
        }
        // LDBコモンイベントは末尾 END 不要
        ce->event_commands = encode_stmtblock(entry.body, 0, false);
    }
}

} // namespace rm2k
