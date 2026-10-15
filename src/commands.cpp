#include "commands.h"
#include "textutil.h"
#include <cassert>
#include <cctype>
#include <cstdlib>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <algorithm>

namespace rm2k {

// CommandRegistry

CommandRegistry& CommandRegistry::instance() {
    static CommandRegistry reg;
    return reg;
}

void CommandRegistry::register_cmd(CmdDef def, bool alias_only) {
    // 同じ名前を alias_only=false で二重登録するのはバグ。デバッグ時に検出する。
    assert(by_name_.find(def.name) == by_name_.end()
           && "CommandRegistry: duplicate command name registration");
    if (!alias_only) {
        assert(by_code_.find(static_cast<int32_t>(def.code)) == by_code_.end()
               && "CommandRegistry: duplicate command code registration");
    }
    size_t idx = defs_.size();
    by_name_[def.name] = idx;
    if (!alias_only)
        by_code_[static_cast<int32_t>(def.code)] = idx;
    defs_.push_back(std::move(def));
}

const CmdDef* CommandRegistry::find_by_name(const std::string& name) const {
    auto it = by_name_.find(name);
    if (it == by_name_.end()) return nullptr;
    return &defs_[it->second];
}

const CmdDef* CommandRegistry::find_by_code(LcfCode code) const {
    auto it = by_code_.find(static_cast<int32_t>(code));
    if (it == by_code_.end()) return nullptr;
    return &defs_[it->second];
}

// 引数ヘルパー (内部)

// find_arg_impl: key が空なら位置検索、非空なら名前検索 → なければ位置検索
static const Arg* find_arg_impl(const std::vector<Arg>& args, size_t pos, const std::string& key) {
    if (!key.empty()) {
        for (const auto& a : args)
            if (a.key == key) return &a;
        // 名前で見つからなかった場合は位置検索にフォールスルー
    }
    size_t unnamed = 0;
    for (const auto& a : args) {
        if (a.key.empty()) {
            if (unnamed == pos) return &a;
            ++unnamed;
        }
    }
    return nullptr;
}

// 名前のみで検索 (位置フォールスルーなし)
static const Arg* find_arg_by_key(const std::vector<Arg>& args, const std::string& key) {
    for (const auto& a : args)
        if (a.key == key) return &a;
    return nullptr;
}


// 公開ヘルパー (commands.h で宣言)
const Arg* find_arg(const std::vector<Arg>& args, size_t pos, const std::string& key) {
    return find_arg_impl(args, pos, key);
}

int32_t arg_int(const std::vector<Arg>& args, size_t pos, const std::string& key, int32_t def) {
    const Arg* a = find_arg_impl(args, pos, key);
    if (!a) return def;
    if (a->val.kind == ArgVal::Int)     return a->val.ival;
    if (a->val.kind == ArgVal::Indexed) return a->val.ival;
    try { return std::stoi(a->val.sval); } catch (...) { return def; }
}

std::string arg_str(const std::vector<Arg>& args, size_t pos, const std::string& key, std::string def) {
    const Arg* a = find_arg_impl(args, pos, key);
    if (!a) return def;
    return a->val.sval;
}

std::string arg_ident(const std::vector<Arg>& args, size_t pos, const std::string& key, std::string def) {
    const Arg* a = find_arg_impl(args, pos, key);
    if (!a) return def;
    return a->val.sval;
}

std::string char_ref_name(int32_t id) {
    switch (id) {
        case kCharPlayer:    return "player";
        case kCharBoat:      return "boat";
        case kCharShip:      return "ship";
        case kCharAirship:   return "airship";
        case kCharThisEvent: return "this";
        default:             return "";
    }
}

int32_t resolve_char_ref(const Arg& a, const char* what) {
    if (a.val.kind == ArgVal::Int) return a.val.ival;
    if (a.val.kind == ArgVal::Ident) {
        const std::string& n = a.val.sval;
        if (n == "player")  return kCharPlayer;
        if (n == "boat")    return kCharBoat;
        if (n == "ship")    return kCharShip;
        if (n == "airship") return kCharAirship;
        if (n == "this")    return kCharThisEvent;
    }
    // 未知の識別子を黙って ID 1 等にしない
    throw std::runtime_error(std::string(what)
        + ": target must be player/boat/ship/airship/this or an event ID");
}

// エンコード用ヘルパー: party/actor の解析

static std::pair<int32_t,int32_t> parse_who(const std::vector<Arg>& args) {
    const Arg* pos0 = find_arg_impl(args, 0, "");
    if (pos0 && pos0->val.kind == ArgVal::Ident && pos0->val.sval == "party")
        return {0, 0};
    const Arg* av = find_arg_by_key(args, "actor_var");
    if (av) return {2, av->val.ival};
    const Arg* act = find_arg_by_key(args, "actor");
    if (act) return {1, act->val.ival};
    return {1, 1};
}

static std::string who_text(int32_t who, int32_t id, bool with_actor_for_party = false) {
    if (who == 1) return "actor=" + std::to_string(id);
    if (who == 2) return "actor_var=" + std::to_string(id);
    return with_actor_for_party ? "party, actor=" + std::to_string(id) : std::string("party");
}

// 余分なパラメータ保持ヘルパー

static void append_extra_params(const CmdStmt& s, size_t known_count,
                                 std::vector<int32_t>& params) {
    for (size_t i = known_count; ; ++i) {
        std::string key = "p" + std::to_string(i);
        const Arg* a = nullptr;
        for (const auto& arg : s.args)
            if (arg.key == key) { a = &arg; break; }
        if (!a) break;
        params.push_back(a->val.ival);
    }
}

// デコード用ユーティリティ

// textutil.h の quote_string を共通利用する (以前はここに \t を落とす重複実装があった)
static inline std::string quote(const std::string& s) { return quote_string(s); }

static inline std::string str(const lcf::DBString& ds) {
    return std::string(ds.begin(), ds.end());
}

// register_all_commands

// add/remove を ident (add/remove) または整数 (0/1) で受け取る。それ以外はエラー
// (以前は arg_ident が Int に対して空文字を返し、op=1 が黙って add 扱いになっていた)
static int32_t add_remove_op(const std::vector<Arg>& args, const std::string& key) {
    const Arg* a = find_arg_by_key(args, key);
    if (!a) return 0;
    if (a->val.kind == ArgVal::Int) return a->val.ival ? 1 : 0;
    if (a->val.sval == "remove") return 1;
    if (a->val.sval == "add")    return 0;
    throw std::runtime_error("'" + key + "' must be add or remove (got '" + a->val.sval + "')");
}

void register_all_commands() {
    static std::once_flag flag;
    std::call_once(flag, []() {
    auto& reg = CommandRegistry::instance();

    auto encode_message_fn = [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
        std::string text = arg_str(s.args, 0, "");
        std::vector<std::string> lines;
        std::string cur;
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '\n') { lines.push_back(cur); cur.clear(); }
            else                  cur += text[i];
        }
        lines.push_back(cur);
        if (lines.empty()) lines.push_back("");
        out.push_back(make_cmd(LcfCode::ShowMessage, ctx.indent, lines[0], {}));
        for (size_t i = 1; i < lines.size(); ++i)
            out.push_back(make_cmd(LcfCode::ShowMessage_2, ctx.indent, lines[i], {}));
    };

    reg.register_cmd(CmdDef{ LcfCode::ShowMessage, "ShowMessage",
        [](const LcfCmd& c) -> std::string {
            return "ShowMessage(" + quote(str(c.string)) + ")";
        },
        encode_message_fn
    });

    {
        CmdDef d;
        d.code   = LcfCode::ShowMessage;
        d.name   = "message";
        d.decode = nullptr;
        d.encode = encode_message_fn;
        reg.register_cmd(std::move(d), /*alias_only=*/true);
    }

    reg.register_cmd(CmdDef{ LcfCode::ShowMessage_2, "ShowMessage_2",
        [](const LcfCmd& c) -> std::string {
            return "ShowMessage_2(" + quote(str(c.string)) + ")";
        },
        [](const CmdStmt&, EncodeCtx&, CmdVec&) {}
    });

    // デコード: 連続する Comment + Comment_2 を1つの comment("...", "...") にまとめる
    // ただし decoder.cpp が逐次呼ぶので、ここでは1コマンド分のみ登録する。
    // 複数行まとめは decoder.cpp 側の decode_cmds_impl で行う。
    // ここでは単純に comment("text") / comment_cont("text") を登録。
    {
        auto encode_comment_fn = [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            // comment("line1", "line2", ...) → Comment + Comment_2...
            // 引数が1個なら従来どおり
            if (s.args.empty()) {
                out.push_back(make_cmd(LcfCode::Comment, ctx.indent, "", {}));
                return;
            }
            // 位置引数をすべてコメント行として展開
            bool first = true;
            for (const auto& a : s.args) {
                if (a.key == "comment_cont") continue; // 旧式キー引数は無視
                if (first) {
                    out.push_back(make_cmd(LcfCode::Comment, ctx.indent, a.val.sval, {}));
                    first = false;
                } else {
                    out.push_back(make_cmd(LcfCode::Comment_2, ctx.indent, a.val.sval, {}));
                }
            }
            if (first) // 引数がキー引数だけだった場合
                out.push_back(make_cmd(LcfCode::Comment, ctx.indent, "", {}));
        };
        reg.register_cmd(CmdDef{ LcfCode::Comment, "Comment",
            [](const LcfCmd& c) -> std::string {
                return "comment(" + quote(str(c.string)) + ")";
            },
            encode_comment_fn
        });
        {
            CmdDef d;
            d.code   = LcfCode::Comment;
            d.name   = "comment";
            d.decode = nullptr;
            d.encode = encode_comment_fn;
            reg.register_cmd(std::move(d), /*alias_only=*/true);
        }
        reg.register_cmd(CmdDef{ LcfCode::Comment_2, "Comment_2",
            [](const LcfCmd& c) -> std::string {
                return "comment_cont(" + quote(str(c.string)) + ")";
            },
            [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
                out.push_back(make_cmd(LcfCode::Comment_2, ctx.indent, arg_str(s.args, 0, ""), {}));
            }
        });
        {
            CmdDef d;
            d.code   = LcfCode::Comment_2;
            d.name   = "comment_cont";
            d.decode = nullptr;
            d.encode = [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
                out.push_back(make_cmd(LcfCode::Comment_2, ctx.indent, arg_str(s.args, 0, ""), {}));
            };
            reg.register_cmd(std::move(d), /*alias_only=*/true);
        }
    }

    reg.register_cmd(CmdDef{ LcfCode::ShowChoice, "ShowChoice",
        [](const LcfCmd& c) -> std::string {
            std::string sv = str(c.string);
            std::string out2 = "ShowChoice(";
            std::istringstream ss(sv);
            std::string token;
            bool first = true;
            while (std::getline(ss, token, '/')) {
                if (!first) out2 += ", ";
                out2 += quote(token);
                first = false;
            }
            out2 += ", cancel=" + std::to_string(param(c, 0));
            out2 += ")";
            return out2;
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            std::string choices;
            int cancel = arg_int(s.args, static_cast<size_t>(-1), "cancel", 0);
            for (size_t i = 0; i < s.args.size(); ++i) {
                if (s.args[i].key == "cancel") continue;
                if (!choices.empty()) choices += '/';
                choices += arg_str(s.args, i, "");
            }
            out.push_back(make_cmd(LcfCode::ShowChoice, ctx.indent, choices, {cancel}));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::ShowChoiceOption, "ShowChoiceOption",
        [](const LcfCmd& c) -> std::string {
            return "case " + quote(str(c.string)) + ": {";
        },
        [](const CmdStmt&, EncodeCtx&, CmdVec&) {}
    });

    reg.register_cmd(CmdDef{ LcfCode::ShowChoiceEnd, "ShowChoiceEnd",
        [](const LcfCmd&) -> std::string { return "}  // choice end"; },
        [](const CmdStmt&, EncodeCtx&, CmdVec&) {}
    });

    reg.register_cmd(CmdDef{ LcfCode::MessageOptions, "MessageOptions",
        [](const LcfCmd& c) -> std::string {
            return "MessageOptions(skip=" + std::to_string(param(c,0))
                 + ", position=" + std::to_string(param(c,1))
                 + ", transparent=" + std::to_string(param(c,2))
                 + ", continue_events=" + std::to_string(param(c,3)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::MessageOptions, ctx.indent, {
                arg_int(s.args,0,"skip",0), arg_int(s.args,1,"position",2),
                arg_int(s.args,2,"transparent",0), arg_int(s.args,3,"continue_events",0)
            }));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::ChangeFaceGraphic, "ChangeFaceGraphic",
        [](const LcfCmd& c) -> std::string {
            return "ChangeFaceGraphic(file=" + quote(str(c.string))
                 + ", index=" + std::to_string(param(c,0))
                 + ", right=" + std::to_string(param(c,1))
                 + ", flip="  + std::to_string(param(c,2)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::ChangeFaceGraphic, ctx.indent,
                arg_str(s.args,0,"file"),
                { arg_int(s.args,1,"index",0), arg_int(s.args,2,"right",0), arg_int(s.args,3,"flip",0) }));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::ControlSwitches, "ControlSwitches",
        [](const LcfCmd& c) -> std::string {
            int32_t type = param(c,0);
            int32_t id1  = param(c,1);
            int32_t id2  = param(c,2);
            int32_t val  = param(c,3);
            std::string op = (val==0?"false": val==1?"true":"toggle");
            if (type == 0)
                return "ControlSwitches(switch[" + std::to_string(id1) + "] = " + op + ")";
            if (type == 1)
                return "ControlSwitches(switch[" + std::to_string(id1) + ".."
                     + std::to_string(id2) + "] = " + op + ")";
            return "ControlSwitches(switch[var[" + std::to_string(id1) + "]] = " + op + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            // キー引数 type= がある場合は明示的キー形式 (ControlSwitches(type=N,...))
            // find_arg_by_key を使い位置フォールスルーを避ける
            const Arg* type_a = find_arg_by_key(s.args, "type");
            if (type_a) {
                int32_t type  = type_a->val.ival;
                int32_t id1   = arg_int(s.args, 0, "id",      1);
                int32_t id2   = arg_int(s.args, 0, "id2",     id1);
                int32_t op_i  = arg_int(s.args, 0, "op_code", 0);
                out.push_back(make_cmd(LcfCode::ControlSwitches, ctx.indent, {type, id1, id2, op_i}));
                return;
            }
            const Arg* sw = find_arg(s.args, 0, "switch");
            if (!sw) throw std::runtime_error("ControlSwitches: missing switch arg");
            int32_t id1 = sw->val.ival;
            // switch[var[N]] 形式: sval="" or sval="-" で間接指定
            if (sw->val.kind == ArgVal::Indexed && sw->val.sval == "var") {
                // type=2: 変数インデックス
                out.push_back(make_cmd(LcfCode::ControlSwitches, ctx.indent, {2, id1, id1, 0}));
                return;
            }
            int32_t id2 = id1;
            // to= は名前検索のみ (位置フォールスルーなし)
            const Arg* to = find_arg_by_key(s.args, "to");
            if (to) id2 = to->val.ival;
            int32_t type = (id1 == id2) ? 0 : 1;
            // op は "op" キーを優先 (parse_arglist が switch[N] = OP を op="=" + {OP} に分解する)
            // switch[N] = OFF → op="=", 次の位置引数="OFF"
            // switch[N] = ON  → op="=", 次の位置引数="ON"
            const Arg* op_a = find_arg_by_key(s.args, "op");
            std::string op_s = "true";
            if (op_a) {
                if (op_a->val.sval == "=") {
                    // do_compound が = を消費して次の引数が実際の値
                    // args の中で op の後の最初の無名引数を探す
                    bool after_op = false;
                    for (const auto& a : s.args) {
                        if (a.key == "op") { after_op = true; continue; }
                        if (after_op && a.key.empty()) { op_s = a.val.sval; break; }
                    }
                } else {
                    op_s = op_a->val.sval;
                }
            }
            int32_t op_i = (op_s=="false"?0: op_s=="true"?1:2);
            out.push_back(make_cmd(LcfCode::ControlSwitches, ctx.indent, {type, id1, id2, op_i}));
        }
    });

    // ControlVars のデコード: src<=1 かつ p[6](src_extra)=0 のとき var[N] op= val; シンプル形式
    // p[6] が存在して非0のとき、または src>=2 のとき、キー形式
    reg.register_cmd(CmdDef{ LcfCode::ControlVars, "ControlVars",
        [](const LcfCmd& c) -> std::string {
            int32_t type = param(c,0);
            int32_t id1  = param(c,1);
            int32_t id2  = param(c,2);
            int32_t op   = param(c,3);
            int32_t src  = param(c,4);
            int32_t val  = param(c,5);
            static const char* ops[] = {"=","+=","-=","*=","/=","%="};
            std::string op_s = (op < 6) ? ops[op] : "=";
            // src<=1 かつ src_extra=0 かつ type<=1 かつ元が7パラメータでないときシンプル形式
            bool has_src_extra = (c.parameters.size() > 6);
            int32_t src_extra = has_src_extra ? param(c,6) : 0;
            if (src <= 1 && src_extra == 0 && type <= 1 && !has_src_extra) {
                std::string rhs = (src == 0) ? std::to_string(val) : "var[" + std::to_string(val) + "]";
                if (type == 0)
                    return "ControlVars(var[" + std::to_string(id1) + "] " + op_s + " " + rhs + ")";
                return "ControlVars(var[" + std::to_string(id1) + ".." + std::to_string(id2) + "] " + op_s + " " + rhs + ")";
            }
            // キー形式: src>=2 / type>=2 / src_extra!=0 / 元が7パラメータ
            std::string r = "ControlVars(var=" + std::to_string(id1)
                 + ", type=" + std::to_string(type)
                 + ", id2=" + std::to_string(id2)
                 + ", op_code=" + std::to_string(op)
                 + ", src=" + std::to_string(src)
                 + ", src_val=" + std::to_string(val);
            // src_extra は元が7パラメータのとき (has_src_extra) のみ出力（0でも）してバイナリを保持する
            if (has_src_extra)
                r += ", src_extra=" + std::to_string(src_extra);
            r += ")";
            return r;
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            int32_t id1 = 1, val = 0, src = 0, op_i = 0;
            const Arg* va = find_arg(s.args, 0, "var");
            if (va) {
                // 名前ラベルがある場合は NameTable で解決する
                if (!va->val.sidx.empty()) {
                    if (!ctx.names)
                        throw std::runtime_error("var[\"" + va->val.sidx + "\"]: name table required");
                    auto rid = ctx.names->find_var(va->val.sidx);
                    if (!rid) throw std::runtime_error("unknown variable name \"" + va->val.sidx + "\"");
                    id1 = *rid;
                } else {
                    id1 = va->val.ival;
                }
                int32_t type_v = arg_int(s.args, 0, "type", 0);
                // id2: range の終端。キーが存在すればその値を使い、なければ type に応じてデフォルト
                //   type=0(single): default=id1（エディタは id2=id1 で保存するが同一のため無視可）
                //   type=1(range) : default=id1
                //   type=2(indirect): default=0（EasyRPG は id2 を参照しない）
                int32_t id2_default = (type_v == 2) ? 0 : id1;
                int32_t id2_v = id2_default;
                {
                    const Arg* id2a = find_arg_by_key(s.args, "id2");
                    if (id2a) {
                        if (!id2a->val.sidx.empty()) {
                            if (!ctx.names)
                                throw std::runtime_error("var id2: name table required");
                            auto rid2 = ctx.names->find_var(id2a->val.sidx);
                            if (!rid2) throw std::runtime_error("unknown variable name \"" + id2a->val.sidx + "\"");
                            id2_v = *rid2;
                        } else {
                            id2_v = id2a->val.ival;
                        }
                    }
                }
                {
                    // src_val の名前指定 (var["名前"] / *var["名前"]) を NameTable で解決する
                    const Arg* sva = find_arg_by_key(s.args, "src_val");
                    if (sva && !sva->val.sidx.empty()) {
                        if (!ctx.names)
                            throw std::runtime_error("var[\"" + sva->val.sidx + "\"]: name table required");
                        auto rid = ctx.names->find_var(sva->val.sidx);
                        if (!rid) throw std::runtime_error("unknown variable name \"" + sva->val.sidx + "\"");
                        val = *rid;
                    } else {
                        val = arg_int(s.args, 0, "src_val", 0);
                    }
                }
                src  = arg_int(s.args, 0, "src", 0);
                op_i = arg_int(s.args, 0, "op_code", 0);
                std::string op_s = arg_ident(s.args, 0, "op", "");
                if      (op_s == "+=") op_i = 1;
                else if (op_s == "-=") op_i = 2;
                else if (op_s == "*=") op_i = 3;
                else if (op_s == "/=") op_i = 4;
                else if (op_s == "%=") op_i = 5;
                // src_extra キーが存在する場合は7パラメータを固定出力（0でも省略しない）
                // src_extra キーがない場合（シンプル形式からのエンコード等）は末尾0を省略する
                const Arg* extra_a = find_arg_by_key(s.args, "src_extra");
                if (extra_a) {
                    // 元が7パラメータのデコード結果 → 7個固定で出力
                    std::vector<int32_t> params = {type_v, id1, id2_v, op_i, src, val, extra_a->val.ival};
                    out.push_back(make_cmd(LcfCode::ControlVars, ctx.indent, params));
                } else {
                    std::vector<int32_t> params = {type_v, id1, id2_v, op_i, src, val};
                    out.push_back(make_cmd(LcfCode::ControlVars, ctx.indent,
                                           trim_trailing_zeros(params)));
                }
                return;
            } else {
                const Arg* vi = find_arg(s.args, 0, "");
                if (vi && vi->val.kind == ArgVal::Indexed) id1 = vi->val.ival;
                val = arg_int(s.args, 1, "value", 0);
                const Arg* v2 = find_arg(s.args, 2, "");
                if (v2) val = v2->val.ival;
            }
            const Arg* op_a = find_arg(s.args, 0, "op");
            if (op_a && op_a->val.kind == ArgVal::Ident) {
                std::string op_s = op_a->val.sval;
                if      (op_s == "+=") op_i = 1;
                else if (op_s == "-=") op_i = 2;
                else if (op_s == "*=") op_i = 3;
                else if (op_s == "/=") op_i = 4;
                else if (op_s == "%=") op_i = 5;
            }
            // 末尾0は省略する (EasyRPG は CmdSetup<...,7> で実行時に補完する)
            out.push_back(make_cmd(LcfCode::ControlVars, ctx.indent, {0, id1, id1, op_i, src, val}));
        }
    });

    // op=0:set, op=1:start, op=2:stop
    // params: [op, val, show_in_battle, show_on_map, p4]
    reg.register_cmd(CmdDef{ LcfCode::TimerOperation, "TimerOperation",
        [](const LcfCmd& c) -> std::string {
            int32_t op = param(c,0);
            if (op == 0) {
                std::string r = "TimerOperation(set=" + std::to_string(param(c,1));
                for (size_t i = 2; i < c.parameters.size(); ++i)
                    r += ", p" + std::to_string(i) + "=" + std::to_string(c.parameters[i]);
                return r + ")";
            }
            std::string op_s = (op == 1) ? "start" : "stop";
            std::string r = "TimerOperation(" + op_s;
            for (size_t i = 1; i < c.parameters.size(); ++i)
                r += ", p" + std::to_string(i) + "=" + std::to_string(c.parameters[i]);
            return r + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            std::string op_s = arg_ident(s.args, 0, "op", "");
            int32_t op_i;
            if      (op_s == "start") op_i = 1;
            else if (op_s == "stop")  op_i = 2;
            else                      op_i = 0;
            int32_t val = arg_int(s.args, 0, "set", 0);
            if (op_s == "start" || op_s == "stop")
                val = arg_int(s.args, 0, "p1", 0);
            int32_t p2 = arg_int(s.args, 0, "p2", 0);
            int32_t p3 = arg_int(s.args, 0, "p3", 0);
            int32_t p4 = arg_int(s.args, 0, "p4", 0);
            std::vector<int32_t> params = {op_i, val, p2, p3, p4};
            // RPGツクール2003 の p5 以降 (タイマー番号など) も保持する。デコーダは p2.. を全て出力するため
            append_extra_params(s, 5, params);
            out.push_back(make_cmd(LcfCode::TimerOperation, ctx.indent, params));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::ChangeGold, "ChangeGold",
        [](const LcfCmd& c) -> std::string {
            int32_t op  = param(c,0);
            int32_t src = param(c,1);
            int32_t val = param(c,2);
            std::string v = src ? "var[" + std::to_string(val) + "]" : std::to_string(val);
            return "ChangeGold(delta=" + std::string(op?"-":"") + v + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            const Arg* da = find_arg_impl(s.args, 0, "delta");
            if (da && da->val.kind == ArgVal::Indexed && da->val.sval == "-var") {
                out.push_back(make_cmd(LcfCode::ChangeGold, ctx.indent,
                    {1, 1, da->val.ival}));
            } else if (da && da->val.kind == ArgVal::Indexed && da->val.sval == "var") {
                out.push_back(make_cmd(LcfCode::ChangeGold, ctx.indent,
                    {0, 1, da->val.ival}));
            } else {
                int32_t delta = da ? da->val.ival : 0;
                out.push_back(make_cmd(LcfCode::ChangeGold, ctx.indent,
                    {delta < 0 ? 1 : 0, 0, std::abs(delta)}));
            }
        }
    });

    // params: [op(0=add,1=remove), item_src(0=const,1=var), item_id, amount_src(0=const,1=var), amount]
    // EasyRPG: OperateValue(p[0], p[3], p[4]); AddItem(p[2] or var[p[2]])
    reg.register_cmd(CmdDef{ LcfCode::ChangeItems, "ChangeItems",
        [](const LcfCmd& c) -> std::string {
            int32_t op        = param(c,0); // 0=add, 1=remove
            int32_t item_src  = param(c,1); // 0=const item_id, 1=var item_id
            int32_t item_id   = param(c,2);
            int32_t amt_src   = param(c,3); // 0=const amount, 1=var amount
            int32_t amount    = param(c,4);
            std::string op_s     = (op == 0) ? "add" : "remove";
            std::string item_s   = (item_src == 0) ? std::to_string(item_id)
                                                   : "var[" + std::to_string(item_id) + "]";
            std::string amount_s = (amt_src == 0) ? std::to_string(amount)
                                                  : "var[" + std::to_string(amount) + "]";
            std::string r = "ChangeItems(op=" + op_s
                         + ", item=" + item_s
                         + ", amount=" + amount_s;
            for (size_t i = 5; i < c.parameters.size(); ++i)
                r += ", p" + std::to_string(i) + "=" + std::to_string(c.parameters[i]);
            r += ")";
            return r;
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            int32_t op_v = add_remove_op(s.args, "op");
            // item= は数値または var[N]
            int32_t item_src_v = 0, item_id = 1;
            const Arg* ia = find_arg_by_key(s.args, "item");
            if (ia) {
                if (ia->val.kind == ArgVal::Indexed && ia->val.sval == "var") {
                    item_src_v = 1; item_id = ia->val.ival;
                } else {
                    item_src_v = 0; item_id = ia->val.ival;
                }
            }
            // amount= は数値または var[N]
            int32_t amt_src_v = 0, amount = 0;
            const Arg* aa = find_arg_by_key(s.args, "amount");
            if (aa) {
                if (aa->val.kind == ArgVal::Indexed && aa->val.sval == "var") {
                    amt_src_v = 1; amount = aa->val.ival;
                } else {
                    amt_src_v = 0; amount = aa->val.ival;
                }
            }
            std::vector<int32_t> params = {op_v, item_src_v, item_id, amt_src_v, amount};
            for (size_t i = 5; ; ++i) {
                std::string key = "p" + std::to_string(i);
                const Arg* a = nullptr;
                for (const auto& arg : s.args) if (arg.key == key) { a = &arg; break; }
                if (!a) break;
                params.push_back(a->val.ival);
            }
            // 末尾0を省略: op,item_src,item_id,amt_src の4個は必須
            // (amount=0 は元ファイルが保存していないことがある)
            out.push_back(make_cmd(LcfCode::ChangeItems, ctx.indent,
                trim_trailing_zeros(params, 4)));
        }
    });

    // params: [op(0=add,1=remove), src(0=const,1=var), actor_id]
    reg.register_cmd(CmdDef{ LcfCode::ChangePartyMembers, "ChangePartyMembers",
        [](const LcfCmd& c) -> std::string {
            const char* op = param(c,0) ? "remove" : "add";
            int32_t src = param(c,1);
            int32_t actor = param(c,2);
            std::string actor_s = (src == 0) ? std::to_string(actor) : "var[" + std::to_string(actor) + "]";
            std::string r = std::string("ChangePartyMembers(") + op + ", actor=" + actor_s;
            for (size_t i = 3; i < c.parameters.size(); ++i)
                r += ", p" + std::to_string(i) + "=" + std::to_string(c.parameters[i]);
            r += ")";
            return r;
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            int32_t op = add_remove_op(s.args, "op");
            // 旧形式互換: 位置引数0が "add"/"remove" の場合
            const Arg* p0 = find_arg_impl(s.args, 0, "");
            if (p0 && p0->val.kind == ArgVal::Ident) {
                op = (p0->val.sval == "remove") ? 1 : 0;
            }
            int32_t src = 0, actor = 1;
            const Arg* aa = find_arg_by_key(s.args, "actor");
            if (aa) {
                if (aa->val.kind == ArgVal::Indexed && aa->val.sval == "var") {
                    src = 1; actor = aa->val.ival;
                } else {
                    src = 0; actor = aa->val.ival;
                }
            } else {
                src   = arg_int(s.args,0,"src",0);
                actor = arg_int(s.args,0,"actor",1);
            }
            std::vector<int32_t> params = {op, src, actor};
            for (size_t i = 3; ; ++i) {
                std::string key = "p" + std::to_string(i);
                const Arg* a = nullptr;
                for (const auto& arg : s.args) if (arg.key == key) { a = &arg; break; }
                if (!a) break;
                params.push_back(a->val.ival);
            }
            // 末尾0を省略: op,src の2個は意味的に必須
            // (actor=0 は元ファイルが保存していないことがある)
            out.push_back(make_cmd(LcfCode::ChangePartyMembers, ctx.indent,
                trim_trailing_zeros(params, 2)));
        }
    });

    // ChangeExp: params=[who, actor_id, op(0=add,1=sub), src(0=const,1=var), val, show_levelup]
    reg.register_cmd(CmdDef{ LcfCode::ChangeExp, "ChangeExp",
        [](const LcfCmd& c) -> std::string {
            int32_t who = param(c,0); int32_t id = param(c,1);
            int32_t op = param(c,2); int32_t src = param(c,3); int32_t val = param(c,4);
            int32_t show = param(c,5);
            std::string t = who_text(who, id, true);
            std::string rhs = (src == 0) ? std::to_string(val) : "var["+std::to_string(val)+"]";
            std::string delta = std::string(op?"-":"") + rhs;
            std::string r = "ChangeExp(" + t + ", delta=" + delta
                          + ", show_levelup=" + std::to_string(show);
            r += ")";
            return r;
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            auto [who, actor] = parse_who(s.args);
            const Arg* act_a = find_arg_by_key(s.args, "actor");
            if (act_a) actor = act_a->val.ival;
            int32_t op_i = 0, src = 0, val = 0;
            const Arg* da = find_arg_impl(s.args, 0, "delta");
            if (da) {
                if (da->val.kind == ArgVal::Indexed && da->val.sval == "var") {
                    src = 1; val = da->val.ival; op_i = 0;
                } else if (da->val.kind == ArgVal::Indexed && da->val.sval == "-var") {
                    src = 1; val = da->val.ival; op_i = 1;
                } else {
                    src = 0; op_i = da->val.ival < 0 ? 1 : 0; val = std::abs(da->val.ival);
                }
            } else {
                // 旧形式互換
                op_i = arg_int(s.args,0,"op",0);
                src  = arg_int(s.args,0,"src",0);
                const Arg* va = find_arg_impl(s.args, 0, "val");
                if (va) {
                    if (va->val.kind == ArgVal::Indexed && va->val.sval == "var") {
                        src = 1; val = va->val.ival;
                    } else val = va->val.ival;
                }
            }
            int32_t show = arg_int(s.args,0,"show_levelup",0);
            if (!show) show = arg_int(s.args,0,"show",0);
            out.push_back(make_cmd(LcfCode::ChangeExp, ctx.indent,
                {who, actor, op_i, src, val, show}));
        }
    });

    // ChangeLevel: params=[who, actor_id, op, src, val, show_msg]
    // show_msg は 0 でも常に6番目パラメータとして出力される
    reg.register_cmd(CmdDef{ LcfCode::ChangeLevel, "ChangeLevel",
        [](const LcfCmd& c) -> std::string {
            int32_t who = param(c,0); int32_t id = param(c,1);
            int32_t op = param(c,2); int32_t src = param(c,3); int32_t val = param(c,4);
            std::string t = who_text(who, id, true);
            std::string rhs = (src == 0) ? std::to_string(val) : "var["+std::to_string(val)+"]";
            int32_t show = (c.parameters.size() >= 6) ? param(c,5) : 0;
            std::string r = "ChangeLevel(" + t + ", delta=" + std::string(op?"-":"") + rhs
                          + ", show_levelup=" + std::to_string(show);
            r += ")";
            return r;
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            auto [who, actor] = parse_who(s.args);
            const Arg* act_a = find_arg_by_key(s.args, "actor");
            if (act_a) actor = act_a->val.ival;
            int32_t op_i = 0, src = 0, val = 0;
            const Arg* da = find_arg_impl(s.args, 0, "delta");
            if (da) {
                if (da->val.kind == ArgVal::Indexed && da->val.sval == "var") {
                    src = 1; val = da->val.ival; op_i = 0;
                } else if (da->val.kind == ArgVal::Indexed && da->val.sval == "-var") {
                    src = 1; val = da->val.ival; op_i = 1;
                } else {
                    src = 0; op_i = da->val.ival < 0 ? 1 : 0; val = std::abs(da->val.ival);
                }
            } else {
                op_i = arg_int(s.args,0,"op",0);
                src  = arg_int(s.args,1,"src",0);
                val  = arg_int(s.args,2,"val",0);
            }
            const Arg* show_a = find_arg_by_key(s.args, "show_levelup");
            if (!show_a) show_a = find_arg_by_key(s.args, "show");
            int32_t show = show_a ? show_a->val.ival : 0;
            // show_msg は常に6番目パラメータとして出力（0でも省略しない）
            out.push_back(make_cmd(LcfCode::ChangeLevel, ctx.indent, {who, actor, op_i, src, val, show}));
        }
    });

    // params: [who, actor_id, op(0=add,1=sub), src(0=const,1=var), val, die_allowed]
    reg.register_cmd(CmdDef{ LcfCode::ChangeHP, "ChangeHP",
        [](const LcfCmd& c) -> std::string {
            int32_t who = param(c,0); int32_t id = param(c,1);
            int32_t op = param(c,2); int32_t src = param(c,3); int32_t val = param(c,4);
            int32_t die = param(c,5);
            std::string t = who_text(who, id);
            std::string rhs = (src==1) ? "var["+std::to_string(val)+"]" : std::to_string(val);
            std::string r = "ChangeHP(" + t + ", delta=" + std::string(op?"-":"") + rhs
                          + ", die_allowed=" + std::to_string(die);
            r += ")";
            return r;
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            auto [who, actor] = parse_who(s.args);
            int32_t die = arg_int(s.args,0,"die_allowed",0);
            int32_t op_i = 0, src = 0, val = 0;
            const Arg* da = find_arg_impl(s.args, 0, "delta");
            if (da) {
                if (da->val.kind == ArgVal::Indexed && da->val.sval == "var") {
                    src = 1; val = da->val.ival; op_i = 0;
                } else if (da->val.kind == ArgVal::Indexed && da->val.sval == "-var") {
                    // デコーダは減算の変数参照を delta=-var[N] と出力する (以前はここで定数 N の加算にすり替わっていた)
                    src = 1; val = da->val.ival; op_i = 1;
                } else {
                    src = 0; op_i = da->val.ival < 0 ? 1 : 0; val = std::abs(da->val.ival);
                }
            }
            out.push_back(make_cmd(LcfCode::ChangeHP, ctx.indent,
                {who, actor, op_i, src, val, die}));
        }
    });

    // params: [who, actor_id, op(0=add,1=sub), src(0=const,1=var), val]
    reg.register_cmd(CmdDef{ LcfCode::ChangeSP, "ChangeSP",
        [](const LcfCmd& c) -> std::string {
            int32_t who = param(c,0); int32_t id = param(c,1);
            int32_t op = param(c,2); int32_t src = param(c,3); int32_t val = param(c,4);
            std::string t = who_text(who, id);
            std::string rhs = (src==1) ? "var["+std::to_string(val)+"]" : std::to_string(val);
            return "ChangeSP(" + t + ", delta=" + std::string(op?"-":"") + rhs + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            auto [who, actor] = parse_who(s.args);
            int32_t op_i = 0, src = 0, val = 0;
            const Arg* da = find_arg_impl(s.args, 0, "delta");
            if (da) {
                if (da->val.kind == ArgVal::Indexed && da->val.sval == "var") {
                    src = 1; val = da->val.ival; op_i = 0;
                } else if (da->val.kind == ArgVal::Indexed && da->val.sval == "-var") {
                    src = 1; val = da->val.ival; op_i = 1;
                } else {
                    src = 0; op_i = da->val.ival < 0 ? 1 : 0; val = std::abs(da->val.ival);
                }
            }
            out.push_back(make_cmd(LcfCode::ChangeSP, ctx.indent,
                {who, actor, op_i, src, val}));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::FullHeal, "FullHeal",
        [](const LcfCmd& c) -> std::string {
            int32_t who = param(c,0); int32_t id = param(c,1);
            std::string t = who_text(who, id);
            return "FullHeal(" + t + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            auto [who, actor] = parse_who(s.args);
            out.push_back(make_cmd(LcfCode::FullHeal, ctx.indent, {who, actor}));
        }
    });

    // params: [who(0=actor,1=party), actor_id, op(0=learn,1=forget), src(0=const,1=var), skill_id]
    reg.register_cmd(CmdDef{ LcfCode::ChangeSkills, "ChangeSkills",
        [](const LcfCmd& c) -> std::string {
            int32_t who = param(c,0); int32_t id = param(c,1);
            int32_t op = param(c,2); int32_t src = param(c,3); int32_t sk = param(c,4);
            std::string t = who_text(who, id);
            std::string rhs = (src==0) ? std::to_string(sk) : "var["+std::to_string(sk)+"]";
            return "ChangeSkills(" + t + ", op=" + (op?"forget":"learn") + ", skill=" + rhs + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            auto [who, actor] = parse_who(s.args);
            std::string op_s = arg_ident(s.args,1,"op","learn");
            int32_t src = arg_int(s.args,0,"src",0);
            int32_t sk = 0;
            const Arg* ska = find_arg_impl(s.args, 0, "skill");
            if (ska) {
                if (ska->val.kind == ArgVal::Indexed) { src = 1; sk = ska->val.ival; }
                else sk = ska->val.ival;
            }
            out.push_back(make_cmd(LcfCode::ChangeSkills, ctx.indent,
                {who, actor, op_s=="forget"?1:0, src, sk}));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::ChangeCondition, "ChangeCondition",
        [](const LcfCmd& c) -> std::string {
            int32_t who = param(c,0); int32_t id = param(c,1);
            int32_t op = param(c,2); int32_t st = param(c,3);
            std::string t = who_text(who, id);
            return "ChangeCondition(" + t + ", op=" + (op?"remove":"add") + ", state=" + std::to_string(st) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            auto [who, actor] = parse_who(s.args);
            std::string op_s = arg_ident(s.args,1,"op","add");
            int32_t st = arg_int(s.args,2,"state",1);
            out.push_back(make_cmd(LcfCode::ChangeCondition, ctx.indent,
                {who, actor, op_s=="remove"?1:0, st}));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::ChangeEquipment, "ChangeEquipment",
        [](const LcfCmd& c) -> std::string {
            int32_t who  = param(c,0); int32_t actor = param(c,1);
            int32_t op   = param(c,2); int32_t src   = param(c,3); int32_t item = param(c,4);
            std::string t = who_text(who, actor);
            std::string r = "ChangeEquipment(" + t + ", op=" + std::to_string(op)
                          + ", src=" + std::to_string(src);
            // item= は元ファイルに5番目のパラメータが存在する場合のみ出力する
            if (c.parameters.size() >= 5) {
                std::string rhs = (src==0) ? std::to_string(item) : "var["+std::to_string(item)+"]";
                r += ", item=" + rhs;
            }
            return r + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            auto [who, actor] = parse_who(s.args);
            int32_t op  = arg_int(s.args,0,"op",0);
            int32_t src = arg_int(s.args,0,"src",0);
            int32_t item = 0;
            const Arg* ia = find_arg_by_key(s.args, "item");
            if (ia) item = ia->val.ival;
            // item= キーが指定されているなら5個固定、なければ4個まで trim する
            bool has_item = (ia != nullptr);
            std::vector<int32_t> params = {who, actor, op, src, item};
            out.push_back(make_cmd(LcfCode::ChangeEquipment, ctx.indent,
                trim_trailing_zeros(params, has_item ? 5 : 4)));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::ChangeHeroName, "ChangeHeroName",
        [](const LcfCmd& c) -> std::string {
            return "ChangeHeroName(actor=" + std::to_string(param(c,0))
                 + ", name=" + quote(str(c.string)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::ChangeHeroName, ctx.indent,
                arg_str(s.args,0,"name"), {arg_int(s.args,1,"actor",1)}));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::ChangeHeroTitle, "ChangeHeroTitle",
        [](const LcfCmd& c) -> std::string {
            return "ChangeHeroTitle(actor=" + std::to_string(param(c,0))
                 + ", title=" + quote(str(c.string)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::ChangeHeroTitle, ctx.indent,
                arg_str(s.args,0,"title"), {arg_int(s.args,1,"actor",1)}));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::EnterHeroName, "EnterHeroName",
        [](const LcfCmd& c) -> std::string {
            std::string r = "EnterHeroName(actor=" + std::to_string(param(c,0));
            for (size_t i = 1; i < c.parameters.size(); ++i)
                r += ", p" + std::to_string(i) + "=" + std::to_string(c.parameters[i]);
            r += ")";
            return r;
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            std::vector<int32_t> params = { arg_int(s.args,0,"actor",1) };
            append_extra_params(s, 1, params);
            while (params.size() < 3) params.push_back(0);
            out.push_back(make_cmd(LcfCode::EnterHeroName, ctx.indent, params));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::Teleport, "Teleport",
        [](const LcfCmd& c) -> std::string {
            std::string r = "Teleport(map=" + std::to_string(param(c,0))
                 + ", x=" + std::to_string(param(c,1))
                 + ", y=" + std::to_string(param(c,2));
            if (c.parameters.size() > 3)
                r += ", dir=" + std::to_string(param(c,3));
            if (c.parameters.size() > 4)
                r += ", fade=" + std::to_string(param(c,4));
            r += ")";
            return r;
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            std::vector<int32_t> params = {
                arg_int(s.args,0,"map",1), arg_int(s.args,1,"x",0), arg_int(s.args,2,"y",0)
            };
            const Arg* dir_a  = find_arg_by_key(s.args, "dir");
            const Arg* fade_a = find_arg_by_key(s.args, "fade");
            if (dir_a || fade_a) params.push_back(dir_a ? dir_a->val.ival : 0);
            if (fade_a)          params.push_back(fade_a->val.ival);
            out.push_back(make_cmd(LcfCode::Teleport, ctx.indent, params));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::MemorizeLocation, "MemorizeLocation",
        [](const LcfCmd& c) -> std::string {
            return "MemorizeLocation(var_map=" + std::to_string(param(c,0))
                 + ", var_x=" + std::to_string(param(c,1))
                 + ", var_y=" + std::to_string(param(c,2)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::MemorizeLocation, ctx.indent, {
                arg_int(s.args,0,"var_map",1), arg_int(s.args,1,"var_x",2), arg_int(s.args,2,"var_y",3)
            }));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::RecallToLocation, "RecallToLocation",
        [](const LcfCmd& c) -> std::string {
            return "RecallToLocation(var_map=" + std::to_string(param(c,0))
                 + ", var_x=" + std::to_string(param(c,1))
                 + ", var_y=" + std::to_string(param(c,2)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::RecallToLocation, ctx.indent, {
                arg_int(s.args,0,"var_map",1), arg_int(s.args,1,"var_x",2), arg_int(s.args,2,"var_y",3)
            }));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::StoreTerrainID, "StoreTerrainID",
        [](const LcfCmd& c) -> std::string {
            return "StoreTerrainID(src=" + std::to_string(param(c,0))
                 + ", x=" + std::to_string(param(c,1))
                 + ", y=" + std::to_string(param(c,2))
                 + ", var=" + std::to_string(param(c,3)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::StoreTerrainID, ctx.indent, {
                arg_int(s.args,0,"src",0), arg_int(s.args,1,"x",0),
                arg_int(s.args,2,"y",0), arg_int(s.args,3,"var",1)
            }));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::StoreEventID, "StoreEventID",
        [](const LcfCmd& c) -> std::string {
            return "StoreEventID(src=" + std::to_string(param(c,0))
                 + ", x=" + std::to_string(param(c,1))
                 + ", y=" + std::to_string(param(c,2))
                 + ", var=" + std::to_string(param(c,3)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::StoreEventID, ctx.indent, {
                arg_int(s.args,0,"src",0), arg_int(s.args,1,"x",0),
                arg_int(s.args,2,"y",0), arg_int(s.args,3,"var",1)
            }));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::EraseScreen, "EraseScreen",
        [](const LcfCmd& c) -> std::string {
            if (c.parameters.size() >= 2)
                return "EraseScreen(type=" + std::to_string(param(c,0)) + ", speed=" + std::to_string(param(c,1)) + ")";
            return "EraseScreen(type=" + std::to_string(param(c,0)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            std::vector<int32_t> p = {arg_int(s.args,0,"type",0)};
            const Arg* sp = find_arg(s.args, 1, "speed");
            if (sp) p.push_back(sp->val.ival);
            out.push_back(make_cmd(LcfCode::EraseScreen, ctx.indent, p));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::ShowScreen, "ShowScreen",
        [](const LcfCmd& c) -> std::string {
            if (c.parameters.size() >= 2)
                return "ShowScreen(type=" + std::to_string(param(c,0)) + ", speed=" + std::to_string(param(c,1)) + ")";
            return "ShowScreen(type=" + std::to_string(param(c,0)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            std::vector<int32_t> p = {arg_int(s.args,0,"type",0)};
            const Arg* sp = find_arg(s.args, 1, "speed");
            if (sp) p.push_back(sp->val.ival);
            out.push_back(make_cmd(LcfCode::ShowScreen, ctx.indent, p));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::TintScreen, "TintScreen",
        [](const LcfCmd& c) -> std::string {
            return "TintScreen(r=" + std::to_string(param(c,0))
                 + ", g=" + std::to_string(param(c,1)) + ", b=" + std::to_string(param(c,2))
                 + ", s=" + std::to_string(param(c,3)) + ", frames=" + std::to_string(param(c,4))
                 + ", wait=" + std::to_string(param(c,5)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::TintScreen, ctx.indent, {
                arg_int(s.args,0,"r",100), arg_int(s.args,1,"g",100),
                arg_int(s.args,2,"b",100), arg_int(s.args,3,"s",100),
                arg_int(s.args,4,"frames",20), arg_int(s.args,5,"wait",1)
            }));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::FlashScreen, "FlashScreen",
        [](const LcfCmd& c) -> std::string {
            std::string r = "FlashScreen(r=" + std::to_string(param(c,0))
                 + ", g=" + std::to_string(param(c,1)) + ", b=" + std::to_string(param(c,2))
                 + ", power=" + std::to_string(param(c,3)) + ", frames=" + std::to_string(param(c,4))
                 + ", wait=" + std::to_string(param(c,5));
            for (size_t i = 6; i < c.parameters.size(); ++i)
                r += ", p" + std::to_string(i) + "=" + std::to_string(c.parameters[i]);
            r += ")";
            return r;
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            std::vector<int32_t> params = {
                arg_int(s.args,0,"r",255), arg_int(s.args,1,"g",255),
                arg_int(s.args,2,"b",255), arg_int(s.args,3,"power",31),
                arg_int(s.args,4,"frames",20), arg_int(s.args,5,"wait",1)
            };
            append_extra_params(s, 6, params);
            out.push_back(make_cmd(LcfCode::FlashScreen, ctx.indent, params));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::ShakeScreen, "ShakeScreen",
        [](const LcfCmd& c) -> std::string {
            std::string r = "ShakeScreen(power=" + std::to_string(param(c,0))
                 + ", speed=" + std::to_string(param(c,1)) + ", frames=" + std::to_string(param(c,2))
                 + ", wait=" + std::to_string(param(c,3));
            for (size_t i = 4; i < c.parameters.size(); ++i)
                r += ", p" + std::to_string(i) + "=" + std::to_string(c.parameters[i]);
            r += ")";
            return r;
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            std::vector<int32_t> params = {
                arg_int(s.args,0,"power",5), arg_int(s.args,1,"speed",5),
                arg_int(s.args,2,"frames",20), arg_int(s.args,3,"wait",1)
            };
            append_extra_params(s, 4, params);
            out.push_back(make_cmd(LcfCode::ShakeScreen, ctx.indent, params));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::PanScreen, "PanScreen",
        [](const LcfCmd& c) -> std::string {
            int32_t op = param(c,0);
            std::string op_s;
            if      (op == 0) op_s = "lock";
            else if (op == 1) op_s = "unlock";
            else if (op == 2) op_s = "move";
            else if (op == 3) op_s = "reset";
            else              op_s = std::to_string(op);
            return "PanScreen(op=" + op_s
                + ", dir=" + std::to_string(param(c,1))
                + ", dist=" + std::to_string(param(c,2))
                + ", speed=" + std::to_string(param(c,3))
                + ", wait=" + std::to_string(param(c,4)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            std::string op_s = arg_ident(s.args,0,"op","move");
            int32_t op_i = 2;
            if      (op_s == "lock")   op_i = 0;
            else if (op_s == "unlock") op_i = 1;
            else if (op_s == "move")   op_i = 2;
            else if (op_s == "reset")  op_i = 3;
            else { try { op_i = std::stoi(op_s); } catch (...) {} }
            out.push_back(make_cmd(LcfCode::PanScreen, ctx.indent, {
                op_i, arg_int(s.args,1,"dir",0), arg_int(s.args,2,"dist",0),
                arg_int(s.args,3,"speed",0), arg_int(s.args,4,"wait",0)
            }));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::WeatherEffects, "WeatherEffects",
        [](const LcfCmd& c) -> std::string {
            return "WeatherEffects(type=" + std::to_string(param(c,0)) + ", power=" + std::to_string(param(c,1)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::WeatherEffects, ctx.indent,
                {arg_int(s.args,0,"type",0), arg_int(s.args,1,"power",2)}));
        }
    });

    // ShowPicture params[0..13] (RPG2k標準):
    //   0=id, 1=pos_mode(0=const/1=var), 2=x, 3=y, 4=fixed_to_map,
    //   5=magnify, 6=top_trans, 7=use_transparent_color,
    //   8=red, 9=green, 10=blue, 11=saturation,
    //   12=effect_mode(0=none/1=rotate/2=wave), 13=effect_power
    //   14=bottom_trans (RPG2k3拡張)
    static const char* show_pic_names[] = {
        "id","pos_mode","x","y","fixed_to_map",
        "magnify","top_trans","use_transparent_color",
        "red","green","blue","saturation",
        "effect_mode","effect_power","bottom_trans"
    };
    static const size_t show_pic_known = 15;

    reg.register_cmd(CmdDef{ LcfCode::ShowPicture, "ShowPicture",
        [](const LcfCmd& c) -> std::string {
            std::string r = "ShowPicture(file=" + quote(str(c.string));
            for (size_t i = 0; i < c.parameters.size(); ++i) {
                if (i < show_pic_known)
                    r += ", " + std::string(show_pic_names[i]) + "=" + std::to_string(c.parameters[i]);
                else
                    r += ", p" + std::to_string(i) + "=" + std::to_string(c.parameters[i]);
            }
            r += ")";
            return r;
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            // 名前付き引数と旧来の pN= 両方に対応
            auto named = [&](size_t i, const char* key, int32_t def) -> int32_t {
                // 名前付き引数を優先
                const Arg* a = find_arg_by_key(s.args, key);
                if (a) return a->val.ival;
                // 旧形式 pN=
                std::string pk = "p" + std::to_string(i);
                a = find_arg_by_key(s.args, pk);
                if (a) return a->val.ival;
                return def;
            };
            // 引数の数を確定（名前付き or pN= が存在する最大インデックスまで）
            // まず名前付きの最大インデックスを求める
            size_t max_idx = 0;
            for (const auto& a : s.args) {
                if (a.key == "file") continue;
                for (size_t i = 0; i < show_pic_known; ++i)
                    if (a.key == show_pic_names[i]) { max_idx = std::max(max_idx, i + 1); break; }
                if (a.key.size() > 1 && a.key[0] == 'p' && std::isdigit(a.key[1])) {
                    size_t idx = static_cast<size_t>(std::stoul(a.key.substr(1)));
                    max_idx = std::max(max_idx, idx + 1);
                }
            }
            std::vector<int32_t> params;
            params.reserve(max_idx);
            for (size_t i = 0; i < max_idx; ++i) {
                if (i < show_pic_known)
                    params.push_back(named(i, show_pic_names[i], 0));
                else {
                    std::string pk = "p" + std::to_string(i);
                    const Arg* a = find_arg_by_key(s.args, pk);
                    params.push_back(a ? a->val.ival : 0);
                }
            }
            out.push_back(make_cmd(LcfCode::ShowPicture, ctx.indent,
                arg_str(s.args, 0, "file"), params));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::ErasePicture, "ErasePicture",
        [](const LcfCmd& c) -> std::string {
            std::string r = "ErasePicture(id=" + std::to_string(param(c,0));
            for (size_t i = 1; i < c.parameters.size(); ++i)
                r += ", p" + std::to_string(i) + "=" + std::to_string(c.parameters[i]);
            r += ")";
            return r;
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            std::vector<int32_t> params = {arg_int(s.args,0,"id",1)};
            append_extra_params(s, 1, params);
            out.push_back(make_cmd(LcfCode::ErasePicture, ctx.indent, params));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::ShowBattleAnimation, "ShowBattleAnimation",
        [](const LcfCmd& c) -> std::string {
            int32_t who = param(c,1);
            std::string nm = char_ref_name(who);
            std::string w = "event=" + (nm.empty() ? std::to_string(who) : nm);
            std::string r = "ShowBattleAnimation(anim=" + std::to_string(param(c,0))
                 + ", " + w + ", wait=" + std::to_string(param(c,2));
            if (c.parameters.size() >= 4)
                r += ", global=" + std::to_string(param(c,3));
            r += ")";
            return r;
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            int32_t who_i = kCharThisEvent;
            const Arg* ea = find_arg_by_key(s.args, "event");
            if (!ea) ea = find_arg_impl(s.args, 0, "");  // 旧形式: 位置引数
            if (ea) who_i = resolve_char_ref(*ea, "ShowBattleAnimation");
            std::vector<int32_t> params = {
                arg_int(s.args,0,"anim",1), who_i, arg_int(s.args,2,"wait",1)
            };
            const Arg* ga = find_arg_by_key(s.args, "global");
            if (!ga) ga = find_arg(s.args, 3, "p3"); // 旧形式互換
            if (ga) params.push_back(ga->val.ival);
            out.push_back(make_cmd(LcfCode::ShowBattleAnimation, ctx.indent, params));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::PlayerVisibility, "PlayerVisibility",
        [](const LcfCmd& c) -> std::string {
            return "PlayerVisibility(visible=" + std::to_string(param(c,0)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::PlayerVisibility, ctx.indent, {arg_int(s.args,0,"visible",1)}));
        }
    });

    // params: [who, freq, repeat, skippable, <move_cmds...>]
    // 移動コマンド列: 各コマンドはコードの後に可変長の引数が続く
    //   code 0-31, 36-41: 引数なし（1要素）
    //   code 32, 33 (switch_on/off): + スイッチID
    //   code 34 (change_graphic): + 文字列(エンコード済み) + インデックス
    //   code 35 (play_sfx): + 文字列 + volume + tempo + balance
    static const char* move_cmd_names[] = {
        "up","right","down","left",                             // 0-3
        "upright","downright","downleft","upleft",              // 4-7
        "random","toward_hero","away_from_hero","forward",      // 8-11
        "face_up","face_right","face_down","face_left",         // 12-15
        "turn_right","turn_left","turn_180","turn_random",      // 16-19
        "face_random","face_hero","face_away",                  // 20-22
        "wait",                                                 // 23
        "jump_start","jump_end",                                // 24-25
        "lock_facing","unlock_facing",                          // 26-27
        "speed_up","speed_down","freq_up","freq_down",          // 28-31
        "switch_on","switch_off",                               // 32-33
        "change_graphic","play_sfx",                            // 34-35
        "through_on","through_off",                             // 36-37
        "stop_anim","start_anim",                               // 38-39
        "transp_up","transp_down",                              // 40-41
    };

    reg.register_cmd(CmdDef{ LcfCode::MoveEvent, "MoveEvent",
        [](const LcfCmd& c) -> std::string {
            int32_t who  = param(c,0);
            int32_t freq = param(c,1);
            int32_t rep  = param(c,2);
            int32_t skip = param(c,3);
            std::string nm = char_ref_name(who);
            std::string w = nm.empty() ? "event=" + std::to_string(who) : nm;
            std::string r = "MoveEvent(" + w;
            // freq/rep/skip が全て0のとき省略（元ファイルで省略されているケース対応）
            if (freq != 0 || rep != 0 || skip != 0) {
                r += ", freq=" + std::to_string(freq)
                   + ", repeat=" + std::to_string(rep)
                   + ", skip=" + std::to_string(skip);
            }

            // 移動コマンド列を move=[...] 形式でデコード
            // code 0-31, 36-41: 1要素。32/33: switch_on/off(id)。34/35: 文字列引数あり
            bool has_complex = false;
            for (size_t j = 4; j < c.parameters.size(); ++j)
                if (c.parameters[j] == 34 || c.parameters[j] == 35) { has_complex = true; break; }

            if (!has_complex) {
                // 全コマンドが名前で表現できる → move=[name, name, ...]
                r += ", move=[";
                bool first = true;
                size_t i = 4;
                while (i < c.parameters.size()) {
                    int32_t code = c.parameters[i++];
                    if (!first) r += ", ";
                    first = false;
                    if (code >= 0 && code <= 41) r += move_cmd_names[code];
                    else r += std::to_string(code);
                    if (code == 32 || code == 33) {
                        // switch_on/off(id): id をリスト内に "(id)" として表現
                        if (i < c.parameters.size())
                            r += "(" + std::to_string(c.parameters[i++]) + ")";
                    }
                }
                r += "]";
            } else {
                // 文字列引数を含む (change_graphic, play_sfx) → pN= 数値で保持
                for (size_t j = 4; j < c.parameters.size(); ++j)
                    r += ", p" + std::to_string(j) + "=" + std::to_string(c.parameters[j]);
            }
            r += ")";
            return r;
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            // 移動コマンド名→コードの逆引きテーブル
            static const struct { const char* name; int32_t code; } move_name_codes[] = {
                {"up",0},{"right",1},{"down",2},{"left",3},
                {"upright",4},{"downright",5},{"downleft",6},{"upleft",7},
                {"random",8},{"toward_hero",9},{"away_from_hero",10},{"forward",11},
                {"face_up",12},{"face_right",13},{"face_down",14},{"face_left",15},
                {"turn_right",16},{"turn_left",17},{"turn_180",18},{"turn_random",19},
                {"face_random",20},{"face_hero",21},{"face_away",22},
                {"wait",23},{"jump_start",24},{"jump_end",25},
                {"lock_facing",26},{"unlock_facing",27},
                {"speed_up",28},{"speed_down",29},{"freq_up",30},{"freq_down",31},
                {"switch_on",32},{"switch_off",33},
                {"change_graphic",34},{"play_sfx",35},
                {"through_on",36},{"through_off",37},
                {"stop_anim",38},{"start_anim",39},
                {"transp_up",40},{"transp_down",41},
            };
            auto name_to_code = [&](const std::string& s) -> int32_t {
                for (const auto& nc : move_name_codes)
                    if (s == nc.name) return nc.code;
                // 未知の名前を黙って 0 (up) にしない (typo が別の移動コマンドになるのを防ぐ)
                try {
                    size_t used = 0;
                    int32_t v = static_cast<int32_t>(std::stoi(s, &used));
                    if (used == s.size()) return v;
                } catch (...) {}
                throw std::runtime_error("unknown move command '" + s + "'");
            };

            // 対象イベントを解決: player / this / 整数ID (event=... でも位置引数でも可)
            // (以前は位置引数の整数や未知の識別子が黙って this になり、event=this は 0 になっていた)
            auto who_from_arg = [](const Arg& a) -> int32_t {
                return resolve_char_ref(a, "MoveEvent");
            };
            const Arg* ea = find_arg_by_key(s.args, "event");
            int32_t who = kCharThisEvent;
            if (ea) {
                who = who_from_arg(*ea);
            } else if (const Arg* p0 = find_arg_impl(s.args, 0, "")) {
                who = who_from_arg(*p0);
            }
            int32_t freq = find_arg_by_key(s.args,"freq")   ? find_arg_by_key(s.args,"freq")->val.ival   : 0;
            int32_t rep  = find_arg_by_key(s.args,"repeat") ? find_arg_by_key(s.args,"repeat")->val.ival : 0;
            int32_t skip = find_arg_by_key(s.args,"skip")   ? find_arg_by_key(s.args,"skip")->val.ival   : 0;
            // freq/rep/skip が明示されているか確認
            bool has_freq = (find_arg_by_key(s.args,"freq")   != nullptr);
            bool has_rep  = (find_arg_by_key(s.args,"repeat") != nullptr);
            bool has_skip = (find_arg_by_key(s.args,"skip")   != nullptr);
            bool has_frs  = has_freq || has_rep || has_skip;
            std::vector<int32_t> params = {who};
            if (has_frs) {
                params.push_back(freq);
                params.push_back(rep);
                params.push_back(skip);
            }

            // move=[...] リスト形式
            const Arg* mv = find_arg_by_key(s.args, "move");
            if (mv && mv->val.kind == ArgVal::List) {
                for (const auto& elem : mv->val.list) {
                    // "switch_on(3)" のような id 付き表現を解析
                    auto paren = elem.find('(');
                    if (paren != std::string::npos) {
                        std::string cmd_name = elem.substr(0, paren);
                        int32_t sw_id = std::stoi(elem.substr(paren + 1, elem.size() - paren - 2));
                        params.push_back(name_to_code(cmd_name));
                        params.push_back(sw_id);
                    } else {
                        params.push_back(name_to_code(elem));
                    }
                }
            } else {
                // リスト形式でない場合: has_frs なら p0=who, p1=freq, p2=rep, p3=skip の後ろから、それ以外は p1 から追加
                size_t extra_start = has_frs ? 4 : 1;
                append_extra_params(s, extra_start, params);
            }

            // move コマンドリストの末尾0は有効なコマンド(up=0)なので削除してはならない
            // ただし move が空のとき (params.size() <= 4) は末尾0を削除して元のバイナリ数に合わせる
            // has_frs の場合は最低4個 ({who,freq,rep,skip}) を保持する
            size_t min_sz = has_frs ? 4 : 1;
            size_t move_start = has_frs ? 4 : 1;
            if (params.size() > move_start) {
                // move コマンドあり: 末尾0は有効コマンド(up=0)なので削除しない
                out.push_back(make_cmd(LcfCode::MoveEvent, ctx.indent, params));
            } else {
                out.push_back(make_cmd(LcfCode::MoveEvent, ctx.indent,
                    trim_trailing_zeros(params, min_sz)));
            }
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::ProceedWithMovement, "ProceedWithMovement",
        [](const LcfCmd&) -> std::string { return "ProceedWithMovement()"; },
        [](const CmdStmt&, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::ProceedWithMovement, ctx.indent));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::HaltAllMovement, "HaltAllMovement",
        [](const LcfCmd&) -> std::string { return "HaltAllMovement()"; },
        [](const CmdStmt&, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::HaltAllMovement, ctx.indent));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::Wait, "Wait",
        [](const LcfCmd& c) -> std::string {
            std::string r = "Wait(frames=" + std::to_string(param(c,0));
            for (size_t i = 1; i < c.parameters.size(); ++i)
                r += ", p" + std::to_string(i) + "=" + std::to_string(c.parameters[i]);
            r += ")";
            return r;
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            std::vector<int32_t> params = {arg_int(s.args,0,"frames",30)};
            append_extra_params(s, 1, params);
            out.push_back(make_cmd(LcfCode::Wait, ctx.indent, params));
        }
    });

    // params: [fadein_ms, volume, tempo, balance]  (EasyRPG でも p[0] は fadein。テキスト上は fade=N)
    // p[0] はフェードイン(ms)。FadeOutBGM は別コマンド。
    reg.register_cmd(CmdDef{ LcfCode::PlayBGM, "PlayBGM",
        [](const LcfCmd& c) -> std::string {
            std::string r = "PlayBGM(" + quote(str(c.string))
                          + ", fade="    + std::to_string(param(c,0))
                          + ", volume="  + std::to_string(param(c,1))
                          + ", tempo="   + std::to_string(param(c,2));
            // balance= は元ファイルに4番目のパラメータが存在する場合のみ出力
            if (c.parameters.size() >= 4)
                r += ", balance=" + std::to_string(param(c,3));
            r += ")";
            return r;
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            // キー引数のみで検索（位置フォールスルーなし）
            const Arg* fade_a = find_arg_by_key(s.args, "fade");
            int32_t bitfield = fade_a ? fade_a->val.ival : 0;
            const Arg* vol_a = find_arg_by_key(s.args, "volume");
            int32_t vol = vol_a ? vol_a->val.ival : 100;
            const Arg* tpo_a = find_arg_by_key(s.args, "tempo");
            int32_t tpo = tpo_a ? tpo_a->val.ival : 100;
            const Arg* bal_a = find_arg_by_key(s.args, "balance");
            // balance= キーがあれば4個固定（balance=0 は有効値なので trim しない）
            // balance= キーがなければ3個まで trim する
            if (bal_a) {
                out.push_back(make_cmd(LcfCode::PlayBGM, ctx.indent, arg_str(s.args,0,""),
                    {bitfield, vol, tpo, bal_a->val.ival}));
            } else {
                out.push_back(make_cmd(LcfCode::PlayBGM, ctx.indent, arg_str(s.args,0,""),
                    trim_trailing_zeros({bitfield, vol, tpo}, 3)));
            }
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::FadeOutBGM, "FadeOutBGM",
        [](const LcfCmd& c) -> std::string { return "FadeOutBGM(ms=" + std::to_string(param(c,0)) + ")"; },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::FadeOutBGM, ctx.indent, {arg_int(s.args,0,"ms",800)}));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::MemorizeBGM, "MemorizeBGM",
        [](const LcfCmd&) -> std::string { return "MemorizeBGM()"; },
        [](const CmdStmt&, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::MemorizeBGM, ctx.indent));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::PlayMemorizedBGM, "PlayMemorizedBGM",
        [](const LcfCmd&) -> std::string { return "PlayMemorizedBGM()"; },
        [](const CmdStmt&, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::PlayMemorizedBGM, ctx.indent));
        }
    });

    // params: [volume, tempo, balance]  (3個固定)
    reg.register_cmd(CmdDef{ LcfCode::PlaySound, "PlaySound",
        [](const LcfCmd& c) -> std::string {
            std::string r = "PlaySound(" + quote(str(c.string));
            // 実データは常に [volume, tempo, balance] の3個
            if (c.parameters.size() >= 1) r += ", volume=" + std::to_string(c.parameters[0]);
            if (c.parameters.size() >= 2) r += ", tempo="  + std::to_string(c.parameters[1]);
            if (c.parameters.size() >= 3) r += ", balance=" + std::to_string(c.parameters[2]);
            for (size_t i = 3; i < c.parameters.size(); ++i)
                r += ", p" + std::to_string(i) + "=" + std::to_string(c.parameters[i]);
            r += ")";
            return r;
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            // volume/tempo/balance キー引数を優先、なければ p0/p1/p2 旧形式
            std::vector<int32_t> params;
            const Arg* vol = find_arg_by_key(s.args, "volume");
            const Arg* tpo = find_arg_by_key(s.args, "tempo");
            const Arg* bal = find_arg_by_key(s.args, "balance");
            if (vol || tpo || bal) {
                params.push_back(vol ? vol->val.ival : 100);
                params.push_back(tpo ? tpo->val.ival : 100);
                params.push_back(bal ? bal->val.ival : 50);
                // 追加の pN= は append_extra_params で
                for (size_t i = 3; ; ++i) {
                    std::string key = "p" + std::to_string(i);
                    const Arg* a = find_arg_by_key(s.args, key);
                    if (!a) break;
                    params.push_back(a->val.ival);
                }
            } else {
                // 旧形式 p0=/p1=/p2= との互換
                append_extra_params(s, 0, params);
            }
            out.push_back(make_cmd(LcfCode::PlaySound, ctx.indent, arg_str(s.args,0,""), params));
        }
    });

    // params: [pos_mode(0=direct,1=variable), x, y, w, h]
    reg.register_cmd(CmdDef{ LcfCode::PlayMovie, "PlayMovie",
        [](const LcfCmd& c) -> std::string {
            return "PlayMovie(file=" + quote(str(c.string))
                 + ", pos_mode=" + std::to_string(param(c,0))
                 + ", x=" + std::to_string(param(c,1))
                 + ", y=" + std::to_string(param(c,2))
                 + ", w=" + std::to_string(param(c,3))
                 + ", h=" + std::to_string(param(c,4)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::PlayMovie, ctx.indent,
                arg_str(s.args,0,"file"),
                { arg_int(s.args,0,"pos_mode",0), arg_int(s.args,1,"x",0),
                  arg_int(s.args,2,"y",0), arg_int(s.args,3,"w",320),
                  arg_int(s.args,4,"h",240) }));
        }
    });

    // params: [var, wait, dir(方向まとめ), act(決定), cancel, shift, key_down, key_left, key_right, key_up]
    static const char* key_input_names[] = {
        "var","wait","dir","act","cancel","shift",
        "key_down","key_left","key_right","key_up"
    };
    static const size_t key_input_known = 10;
    reg.register_cmd(CmdDef{ LcfCode::KeyInputProc, "KeyInputProc",
        [](const LcfCmd& c) -> std::string {
            std::string r = "KeyInputProc(";
            for (size_t i = 0; i < c.parameters.size(); ++i) {
                if (i) r += ", ";
                if (i < key_input_known)
                    r += std::string(key_input_names[i]) + "=" + std::to_string(c.parameters[i]);
                else
                    r += "p" + std::to_string(i) + "=" + std::to_string(c.parameters[i]);
            }
            r += ")";
            return r;
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            // 名前付きとpN=両方対応
            auto named = [&](size_t i, const char* key, int32_t def) -> int32_t {
                const Arg* a = find_arg_by_key(s.args, key);
                if (a) return a->val.ival;
                std::string pk = "p" + std::to_string(i);
                a = find_arg_by_key(s.args, pk);
                return a ? a->val.ival : def;
            };
            size_t max_idx = 0;
            for (const auto& a : s.args) {
                for (size_t i = 0; i < key_input_known; ++i)
                    if (a.key == key_input_names[i]) { max_idx = std::max(max_idx, i + 1); break; }
                if (a.key.size() > 1 && a.key[0] == 'p' && std::isdigit(a.key[1])) {
                    size_t idx = static_cast<size_t>(std::stoul(a.key.substr(1)));
                    max_idx = std::max(max_idx, idx + 1);
                }
            }
            // 最低限 var と wait は出力
            if (max_idx < 2) max_idx = 2;
            std::vector<int32_t> params;
            for (size_t i = 0; i < max_idx; ++i) {
                if (i < key_input_known)
                    params.push_back(named(i, key_input_names[i], 0));
                else {
                    std::string pk = "p" + std::to_string(i);
                    const Arg* a = find_arg_by_key(s.args, pk);
                    params.push_back(a ? a->val.ival : 0);
                }
            }
            out.push_back(make_cmd(LcfCode::KeyInputProc, ctx.indent, params));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::EnemyEncounter, "EnemyEncounter",
        [](const LcfCmd& c) -> std::string {
            std::string r = "EnemyEncounter(troop=" + std::to_string(param(c,0))
                 + ", type=" + std::to_string(param(c,1))
                 + ", can_escape=" + std::to_string(param(c,4))
                 + ", can_lose=" + std::to_string(param(c,5));
            // 背景ファイル名 (文字列) と既知フィールド以外のパラメータは無損失に保持する
            // (以前は文字列・p2・p3・p6以降がデコード/エンコードで失われていた)
            std::string bg = str(c.string);
            if (!bg.empty()) r += ", bg=" + quote(bg);
            if (param(c,2) != 0) r += ", p2=" + std::to_string(param(c,2));
            if (param(c,3) != 0) r += ", p3=" + std::to_string(param(c,3));
            for (size_t i = 6; i < c.parameters.size(); ++i)
                r += ", p" + std::to_string(i) + "=" + std::to_string(c.parameters[i]);
            return r + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            auto opt = [&](const char* key) -> int32_t {
                const Arg* a = find_arg_by_key(s.args, key);
                return a ? a->val.ival : 0;
            };
            std::vector<int32_t> params = {
                arg_int(s.args,0,"troop",1), arg_int(s.args,1,"type",0), opt("p2"), opt("p3"),
                arg_int(s.args,2,"can_escape",1), arg_int(s.args,3,"can_lose",0)
            };
            append_extra_params(s, 6, params);
            const Arg* bg = find_arg_by_key(s.args, "bg");
            out.push_back(make_cmd(LcfCode::EnemyEncounter, ctx.indent,
                bg ? bg->val.sval : std::string(), params));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::OpenShop, "OpenShop",
        [](const LcfCmd& c) -> std::string {
            std::string r = "OpenShop(type=" + std::to_string(param(c,0));
            for (size_t i = 1; i < c.parameters.size(); ++i)
                r += ", item" + std::to_string(i) + "=" + std::to_string(c.parameters[i]);
            return r + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            std::vector<int32_t> ps = {arg_int(s.args,0,"type",0)};
            int idx = 1;
            while (true) {
                std::string key = "item" + std::to_string(idx);
                bool found = false;
                for (const auto& a : s.args) {
                    if (a.key == key) { ps.push_back(a.val.ival); found = true; break; }
                }
                if (!found) {
                    const Arg* pa = find_arg(s.args, static_cast<size_t>(idx), "");
                    if (pa && pa->key.empty()) { ps.push_back(pa->val.ival); found = true; }
                }
                if (!found) break;
                ++idx;
            }
            out.push_back(make_cmd(LcfCode::OpenShop, ctx.indent, ps));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::ShowInn, "ShowInn",
        [](const LcfCmd& c) -> std::string {
            std::string r = "ShowInn(type=" + std::to_string(param(c,0)) + ", cost=" + std::to_string(param(c,1));
            // 3番目以降 (宿屋イベントハンドラの有無など) は無損失に保持する
            for (size_t i = 2; i < c.parameters.size(); ++i)
                r += ", p" + std::to_string(i) + "=" + std::to_string(c.parameters[i]);
            return r + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            std::vector<int32_t> params = {arg_int(s.args,0,"type",0), arg_int(s.args,1,"cost",100)};
            append_extra_params(s, 2, params);
            out.push_back(make_cmd(LcfCode::ShowInn, ctx.indent, params));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::Label, "Label",
        [](const LcfCmd& c) -> std::string { return "Label(" + std::to_string(param(c,0)) + ")"; },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::Label, ctx.indent, {arg_int(s.args,0,"",0)}));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::JumpToLabel, "JumpToLabel",
        [](const LcfCmd& c) -> std::string { return "JumpToLabel(" + std::to_string(param(c,0)) + ")"; },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::JumpToLabel, ctx.indent, {arg_int(s.args,0,"",0)}));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::Loop, "Loop",
        [](const LcfCmd&) -> std::string { return "loop {"; },
        [](const CmdStmt&, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::Loop, ctx.indent));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::BreakLoop, "BreakLoop",
        [](const LcfCmd&) -> std::string { return "BreakLoop()"; },
        [](const CmdStmt&, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::BreakLoop, ctx.indent));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::EndLoop, "EndLoop",
        [](const LcfCmd&) -> std::string { return "}  // end loop"; },
        [](const CmdStmt&, EncodeCtx&, CmdVec&) {}
    });

    reg.register_cmd(CmdDef{ LcfCode::ConditionalBranch, "ConditionalBranch",
        [](const LcfCmd& c) -> std::string {
            return "if (/* cond type=" + std::to_string(param(c,0)) + " */) {";
        },
        [](const CmdStmt&, EncodeCtx&, CmdVec&) {}
    });

    reg.register_cmd(CmdDef{ LcfCode::ElseBranch, "ElseBranch",
        [](const LcfCmd&) -> std::string { return "} else {"; },
        [](const CmdStmt&, EncodeCtx&, CmdVec&) {}
    });

    reg.register_cmd(CmdDef{ LcfCode::EndBranch, "EndBranch",
        [](const LcfCmd&) -> std::string { return "}"; },
        [](const CmdStmt&, EncodeCtx&, CmdVec&) {}
    });

    reg.register_cmd(CmdDef{ LcfCode::EndEventProcessing, "EndEventProcessing",
        [](const LcfCmd&) -> std::string { return "EndEventProcessing()"; },
        [](const CmdStmt&, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::EndEventProcessing, ctx.indent));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::EraseEvent, "EraseEvent",
        [](const LcfCmd&) -> std::string { return "EraseEvent()"; },
        [](const CmdStmt&, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::EraseEvent, ctx.indent));
        }
    });

    // params: [type, id, page]
    // EasyRPG: type=0 → コモンイベント, type=1 → マップイベント, type=2 → 変数指定
    reg.register_cmd(CmdDef{ LcfCode::CallEvent, "CallEvent",
        [](const LcfCmd& c) -> std::string {
            int32_t type = param(c,0);
            if (type == 0)
                return "CallEvent(common=" + std::to_string(param(c,1)) + ")";
            if (type == 1)
                return "CallEvent(event=" + std::to_string(param(c,1))
                     + ", page=" + std::to_string(param(c,2)) + ")";
            // type=2: 変数で指定 / その他
            return "CallEvent(type=" + std::to_string(type)
                 + ", id=" + std::to_string(param(c,1))
                 + ", page=" + std::to_string(param(c,2)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            const Arg* co_a = find_arg_by_key(s.args, "common");
            if (co_a) {
                // type=0: コモンイベント
                out.push_back(make_cmd(LcfCode::CallEvent, ctx.indent,
                    {0, co_a->val.ival, 0}));
                return;
            }
            const Arg* ev_a = find_arg_by_key(s.args, "event");
            if (ev_a) {
                // type=1: マップイベント
                const Arg* pg_a = find_arg_by_key(s.args, "page");
                int32_t page = pg_a ? pg_a->val.ival : 0;
                out.push_back(make_cmd(LcfCode::CallEvent, ctx.indent,
                    {1, ev_a->val.ival, page}));
                return;
            }
            // type= 明示形式 (type=2 etc.)
            int32_t type = find_arg_by_key(s.args,"type") ? find_arg_by_key(s.args,"type")->val.ival : 0;
            int32_t id   = find_arg_by_key(s.args,"id")   ? find_arg_by_key(s.args,"id")->val.ival   : 0;
            int32_t page = find_arg_by_key(s.args,"page") ? find_arg_by_key(s.args,"page")->val.ival : 0;
            out.push_back(make_cmd(LcfCode::CallEvent, ctx.indent, {type, id, page}));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::CallCommonEvent, "CallCommonEvent",
        [](const LcfCmd& c) -> std::string {
            return "CallCommonEvent(id=" + std::to_string(param(c,0)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::CallCommonEvent, ctx.indent, {arg_int(s.args,0,"id",1)}));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::GameOver, "GameOver",
        [](const LcfCmd&) -> std::string { return "GameOver()"; },
        [](const CmdStmt&, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::GameOver, ctx.indent));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::ReturntoTitleScreen, "ReturntoTitleScreen",
        [](const LcfCmd&) -> std::string { return "ReturntoTitleScreen()"; },
        [](const CmdStmt&, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::ReturntoTitleScreen, ctx.indent));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::OpenSaveMenu, "OpenSaveMenu",
        [](const LcfCmd&) -> std::string { return "OpenSaveMenu()"; },
        [](const CmdStmt&, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::OpenSaveMenu, ctx.indent));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::OpenMainMenu, "OpenMainMenu",
        [](const LcfCmd&) -> std::string { return "OpenMainMenu()"; },
        [](const CmdStmt&, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::OpenMainMenu, ctx.indent));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::OpenLoadMenu, "OpenLoadMenu",
        [](const LcfCmd&) -> std::string { return "OpenLoadMenu()"; },
        [](const CmdStmt&, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::OpenLoadMenu, ctx.indent));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::ExitGame, "ExitGame",
        [](const LcfCmd&) -> std::string { return "ExitGame()"; },
        [](const CmdStmt&, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::ExitGame, ctx.indent));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::ChangeSaveAccess, "ChangeSaveAccess",
        [](const LcfCmd& c) -> std::string { return "ChangeSaveAccess(enable=" + std::to_string(param(c,0)) + ")"; },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::ChangeSaveAccess, ctx.indent, {arg_int(s.args,0,"enable",1)}));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::ChangeTeleportAccess, "ChangeTeleportAccess",
        [](const LcfCmd& c) -> std::string { return "ChangeTeleportAccess(enable=" + std::to_string(param(c,0)) + ")"; },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::ChangeTeleportAccess, ctx.indent, {arg_int(s.args,0,"enable",1)}));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::ChangeEscapeAccess, "ChangeEscapeAccess",
        [](const LcfCmd& c) -> std::string { return "ChangeEscapeAccess(enable=" + std::to_string(param(c,0)) + ")"; },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::ChangeEscapeAccess, ctx.indent, {arg_int(s.args,0,"enable",1)}));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::ChangeMainMenuAccess, "ChangeMainMenuAccess",
        [](const LcfCmd& c) -> std::string { return "ChangeMainMenuAccess(enable=" + std::to_string(param(c,0)) + ")"; },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::ChangeMainMenuAccess, ctx.indent, {arg_int(s.args,0,"enable",1)}));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::ChangeMapTileset, "ChangeMapTileset",
        [](const LcfCmd& c) -> std::string { return "ChangeMapTileset(id=" + std::to_string(param(c,0)) + ")"; },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::ChangeMapTileset, ctx.indent, {arg_int(s.args,0,"id",1)}));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::ChangeEncounterSteps, "ChangeEncounterSteps",
        [](const LcfCmd& c) -> std::string { return "ChangeEncounterSteps(steps=" + std::to_string(param(c,0)) + ")"; },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::ChangeEncounterSteps, ctx.indent, {arg_int(s.args,0,"steps",30)}));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::EnterExitVehicle, "EnterExitVehicle",
        [](const LcfCmd&) -> std::string { return "EnterExitVehicle()"; },
        [](const CmdStmt&, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::EnterExitVehicle, ctx.indent));
        }
    });

    // params: [event(context), fadein?, volume?, tempo?, balance?, var_flags?]
    // var_flags is a bitfield: bit N means p(N+1) is a variable ID instead of direct value
    reg.register_cmd(CmdDef{ LcfCode::ChangeSystemBGM, "ChangeSystemBGM",
        [](const LcfCmd& c) -> std::string {
            std::string r = "ChangeSystemBGM(event=" + std::to_string(param(c,0))
                 + ", bgm=" + quote(str(c.string));
            if (c.parameters.size() > 1)
                r += ", fadein=" + std::to_string(param(c,1))
                   + ", volume=" + std::to_string(param(c,2))
                   + ", tempo=" + std::to_string(param(c,3))
                   + ", balance=" + std::to_string(param(c,4));
            if (c.parameters.size() > 5)
                r += ", var_flags=" + std::to_string(param(c,5));
            r += ")";
            return r;
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            int32_t ev  = arg_int(s.args,0,"event",0);
            std::vector<int32_t> params = {ev};
            const Arg* fadein = find_arg_by_key(s.args, "fadein");
            if (fadein) {
                params.push_back(fadein->val.ival);
                params.push_back(arg_int(s.args,0,"volume",100));
                params.push_back(arg_int(s.args,0,"tempo",100));
                params.push_back(arg_int(s.args,0,"balance",50));
                // デコーダは 6 番目のパラメータを var_flags= として出力する (旧形式の p5= も受け付ける)
                const Arg* vf = find_arg_by_key(s.args, "var_flags");
                if (vf) params.push_back(vf->val.ival);
                else    append_extra_params(s, 5, params);
            }
            out.push_back(make_cmd(LcfCode::ChangeSystemBGM, ctx.indent,
                arg_str(s.args,0,"bgm"), params));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::ChangeSystemGraphics, "ChangeSystemGraphics",
        [](const LcfCmd& c) -> std::string {
            std::string r = "ChangeSystemGraphics(system=" + quote(str(c.string))
                + ", stretch=" + std::to_string(param(c,0))
                + ", font=" + std::to_string(param(c,1));
            for (size_t i = 2; i < c.parameters.size(); ++i)
                r += ", p" + std::to_string(i) + "=" + std::to_string(c.parameters[i]);
            r += ")";
            return r;
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            int32_t stretch = arg_int(s.args,0,"stretch",0);
            int32_t font    = arg_int(s.args,0,"font",0);
            // 旧形式 p0= / opacity= との互換
            if (!find_arg_by_key(s.args,"stretch") && !find_arg_by_key(s.args,"font")) {
                const Arg* op = find_arg_by_key(s.args, "opacity");
                if (op) stretch = op->val.ival;
                const Arg* p0 = find_arg_by_key(s.args, "p0");
                if (p0) stretch = p0->val.ival;
                const Arg* p1 = find_arg_by_key(s.args, "p1");
                if (p1) font = p1->val.ival;
            }
            std::vector<int32_t> params = {stretch, font};
            for (size_t i = 2; ; ++i) {
                std::string pk = "p" + std::to_string(i);
                const Arg* a = find_arg_by_key(s.args, pk);
                if (!a) break;
                params.push_back(a->val.ival);
            }
            out.push_back(make_cmd(LcfCode::ChangeSystemGraphics, ctx.indent,
                arg_str(s.args,0,"system"), params));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::InputNumber, "InputNumber",
        [](const LcfCmd& c) -> std::string {
            return "InputNumber(digits=" + std::to_string(param(c,0)) + ", var=" + std::to_string(param(c,1)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::InputNumber, ctx.indent,
                trim_trailing_zeros({arg_int(s.args,0,"digits",4), arg_int(s.args,1,"var",1)})));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::ChangeSpriteAssociation, "ChangeSpriteAssociation",
        [](const LcfCmd& c) -> std::string {
            return "ChangeSpriteAssociation(actor=" + std::to_string(param(c,0))
                 + ", sprite=" + quote(str(c.string))
                 + ", sprite_index=" + std::to_string(param(c,1))
                 + ", transparent=" + std::to_string(param(c,2)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::ChangeSpriteAssociation, ctx.indent,
                arg_str(s.args,0,"sprite"),
                { arg_int(s.args,0,"actor",1),
                  arg_int(s.args,1,"sprite_index",0),
                  arg_int(s.args,2,"transparent",arg_int(s.args,2,"p2",0)) }));
        }
    });

    // EnemyEncounter の後は VictoryHandler が来て victory body (indent+1) が続く。
    // VictoryHandler/EscapeHandler/EndBattle ブロックの END は body コマンドの LCF indent が
    // デコード→エンコード往復で保存されないため、現状ではバイナリ完全再現はできない。
    // (パーサーが中括弧ベースのネストのみを LCF indent に反映するため、
    //  VictoryHandler body の装飾的インデントが失われる)
    reg.register_cmd(CmdDef{ LcfCode::VictoryHandler, "VictoryHandler",
        [](const LcfCmd&) -> std::string { return "VictoryHandler()"; },
        [](const CmdStmt&, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::VictoryHandler, ctx.indent));
        }
    });
    reg.register_cmd(CmdDef{ LcfCode::EscapeHandler, "EscapeHandler",
        [](const LcfCmd&) -> std::string { return "EscapeHandler()"; },
        [](const CmdStmt&, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::EscapeHandler, ctx.indent));
        }
    });
    reg.register_cmd(CmdDef{ LcfCode::DefeatHandler, "DefeatHandler",
        [](const LcfCmd&) -> std::string { return "DefeatHandler()"; },
        [](const CmdStmt&, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::DefeatHandler, ctx.indent));
        }
    });
    reg.register_cmd(CmdDef{ LcfCode::EndBattle, "EndBattle",
        [](const LcfCmd&) -> std::string { return "EndBattle()"; },
        [](const CmdStmt&, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::EndBattle, ctx.indent));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::ChangeSystemSFX, "ChangeSystemSFX",
        [](const LcfCmd& c) -> std::string {
            return "ChangeSystemSFX(type=" + std::to_string(param(c,0))
                 + ", sfx=" + quote(str(c.string))
                 + ", volume=" + std::to_string(param(c,1))
                 + ", tempo=" + std::to_string(param(c,2))
                 + ", balance=" + std::to_string(param(c,3)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::ChangeSystemSFX, ctx.indent,
                arg_str(s.args, 0, "sfx"),
                { arg_int(s.args,0,"type",0),
                  arg_int(s.args,1,"volume",100),
                  arg_int(s.args,2,"tempo",100),
                  arg_int(s.args,3,"balance",50) }));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::ChangeScreenTransitions, "ChangeScreenTransitions",
        [](const LcfCmd& c) -> std::string {
            return "ChangeScreenTransitions(type=" + std::to_string(param(c,0))
                 + ", id=" + std::to_string(param(c,1)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::ChangeScreenTransitions, ctx.indent,
                { arg_int(s.args,0,"type",0), arg_int(s.args,1,"id",0) }));
        }
    });

    // MovePicture params[0..15]:
    //   ShowPicture と同じ 0-13 + 14=duration, 15=wait
    //   (p4=fixed_to_map, p7=use_transparent_color はMovePictureでは無視されるが構造は同一)
    static const char* move_pic_names[] = {
        "id","pos_mode","x","y","fixed_to_map",
        "magnify","top_trans","use_transparent_color",
        "red","green","blue","saturation",
        "effect_mode","effect_power","duration","wait"
    };
    static const size_t move_pic_known = 16;

    reg.register_cmd(CmdDef{ LcfCode::MovePicture, "MovePicture",
        [](const LcfCmd& c) -> std::string {
            std::string r = "MovePicture(";
            for (size_t i = 0; i < c.parameters.size(); ++i) {
                if (i) r += ", ";
                if (i < move_pic_known)
                    r += std::string(move_pic_names[i]) + "=" + std::to_string(c.parameters[i]);
                else
                    r += "p" + std::to_string(i) + "=" + std::to_string(c.parameters[i]);
            }
            r += ")";
            return r;
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            auto named = [&](size_t i, const char* key, int32_t def) -> int32_t {
                const Arg* a = find_arg_by_key(s.args, key);
                if (a) return a->val.ival;
                std::string pk = "p" + std::to_string(i);
                a = find_arg_by_key(s.args, pk);
                if (a) return a->val.ival;
                return def;
            };
            size_t max_idx = 0;
            for (const auto& a : s.args) {
                for (size_t i = 0; i < move_pic_known; ++i)
                    if (a.key == move_pic_names[i]) { max_idx = std::max(max_idx, i + 1); break; }
                if (a.key.size() > 1 && a.key[0] == 'p' && std::isdigit(a.key[1])) {
                    size_t idx = static_cast<size_t>(std::stoul(a.key.substr(1)));
                    max_idx = std::max(max_idx, idx + 1);
                }
            }
            std::vector<int32_t> params;
            params.reserve(max_idx);
            for (size_t i = 0; i < max_idx; ++i) {
                if (i < move_pic_known)
                    params.push_back(named(i, move_pic_names[i], 0));
                else {
                    std::string pk = "p" + std::to_string(i);
                    const Arg* a = find_arg_by_key(s.args, pk);
                    params.push_back(a ? a->val.ival : 0);
                }
            }
            out.push_back(make_cmd(LcfCode::MovePicture, ctx.indent, params));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::FlashSprite, "FlashSprite",
        [](const LcfCmd& c) -> std::string {
            std::string nm = char_ref_name(param(c,0));
            std::string who = nm.empty() ? std::to_string(param(c,0)) : nm;
            return "FlashSprite(event=" + who
                 + ", r=" + std::to_string(param(c,1))
                 + ", g=" + std::to_string(param(c,2))
                 + ", b=" + std::to_string(param(c,3))
                 + ", chroma=" + std::to_string(param(c,4))
                 + ", duration=" + std::to_string(param(c,5))
                 + ", wait=" + std::to_string(param(c,6)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            const Arg* ea = find_arg_impl(s.args, 0, "event");
            int32_t ev_id = ea ? resolve_char_ref(*ea, "FlashSprite") : kCharThisEvent;
            out.push_back(make_cmd(LcfCode::FlashSprite, ctx.indent,
                { ev_id,
                  arg_int(s.args,1,"r",31),
                  arg_int(s.args,2,"g",31),
                  arg_int(s.args,3,"b",31),
                  arg_int(s.args,4,"chroma",31),
                  arg_int(s.args,5,"duration",5),
                  arg_int(s.args,6,"wait",1) }));
        }
    });

    // RawCommand: デコーダが未登録コマンドを無損失で出力する形式
    //   RawCommand(code=N, str="...", p0=.., p1=.., ...)
    {
        CmdDef d;
        d.code   = LcfCode::END;   // alias_only: コード逆引きには登録されない
        d.name   = "RawCommand";
        d.decode = nullptr;
        d.encode = [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            const Arg* code_a = find_arg_by_key(s.args, "code");
            if (!code_a) throw std::runtime_error("RawCommand requires code=N");
            LcfCmd c;
            c.code   = code_a->val.ival;
            c.indent = ctx.indent;
            const Arg* str_a = find_arg_by_key(s.args, "str");
            c.string = lcf::DBString(str_a ? str_a->val.sval : std::string());
            std::vector<int32_t> params;
            for (size_t i = 0; ; ++i) {
                const Arg* a = find_arg_by_key(s.args, "p" + std::to_string(i));
                if (!a) break;
                params.push_back(a->val.ival);
            }
            c.parameters = lcf::DBArray<int32_t>(params.begin(), params.end());
            out.push_back(c);
        };
        reg.register_cmd(std::move(d), /*alias_only=*/true);
    }

    reg.register_cmd(CmdDef{ LcfCode::END, "END",
        [](const LcfCmd&) -> std::string { return ""; },
        [](const CmdStmt&, EncodeCtx&, CmdVec&) {}
    });

    // ══════════════════════════════════════════════════════════════════════════
    // ゆめ2っき対応コマンド
    // ══════════════════════════════════════════════════════════════════════════

    // params: [event, mode(0=direct,1=variable), x, y, direction(RPG2k3, 1-8)]
    reg.register_cmd(CmdDef{ LcfCode::ChangeEventLocation, "ChangeEventLocation",
        [](const LcfCmd& c) -> std::string {
            std::string r = "ChangeEventLocation(event=" + (char_ref_name(param(c,0)).empty() ? std::to_string(param(c,0)) : char_ref_name(param(c,0)))
                + ", mode=" + std::to_string(param(c,1))
                + ", x=" + std::to_string(param(c,2))
                + ", y=" + std::to_string(param(c,3));
            if (c.parameters.size() >= 5)
                r += ", direction=" + std::to_string(param(c,4));
            r += ")";
            return r;
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            const Arg* ea = find_arg_impl(s.args, 0, "event");
            std::vector<int32_t> params = {
                ea ? resolve_char_ref(*ea, "ChangeEventLocation") : 1,
                arg_int(s.args, 1, "mode",  0),
                arg_int(s.args, 2, "x",     0),
                arg_int(s.args, 3, "y",     0)
            };
            const Arg* dir = find_arg_by_key(s.args, "direction");
            if (dir) params.push_back(dir->val.ival);
            out.push_back(make_cmd(LcfCode::ChangeEventLocation, ctx.indent, params));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::TileSubstitution, "TileSubstitution",
        [](const LcfCmd& c) -> std::string {
            return "TileSubstitution(chip_set=" + std::to_string(param(c,0))
                + ", src=" + std::to_string(param(c,1))
                + ", dst=" + std::to_string(param(c,2)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::TileSubstitution, ctx.indent,
                { arg_int(s.args, 0, "chip_set", 1),
                  arg_int(s.args, 1, "src",  0),
                  arg_int(s.args, 2, "dst",  0) }));
        }
    });

    // ChangePBG params:
    //   0=scroll_horz, 1=scroll_vert, 2=scroll_horz_auto, 3=scroll_horz_speed,
    //   4=scroll_vert_auto, 5=scroll_vert_speed
    reg.register_cmd(CmdDef{ LcfCode::ChangePBG, "ChangePBG",
        [](const LcfCmd& c) -> std::string {
            static const char* pbg_names[] = {
                "scroll_horz","scroll_vert","scroll_horz_auto","scroll_horz_speed",
                "scroll_vert_auto","scroll_vert_speed"
            };
            std::string r = "ChangePBG(name=" + quote(str(c.string));
            for (size_t i = 0; i < c.parameters.size(); ++i) {
                if (i < 6)
                    r += ", " + std::string(pbg_names[i]) + "=" + std::to_string(c.parameters[i]);
                else
                    r += ", p" + std::to_string(i) + "=" + std::to_string(c.parameters[i]);
            }
            r += ")";
            return r;
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            static const char* pbg_names[] = {
                "scroll_horz","scroll_vert","scroll_horz_auto","scroll_horz_speed",
                "scroll_vert_auto","scroll_vert_speed"
            };
            auto named = [&](size_t i, const char* key) -> int32_t {
                const Arg* a = find_arg_by_key(s.args, key);
                if (a) return a->val.ival;
                std::string pk = "p" + std::to_string(i);
                a = find_arg_by_key(s.args, pk);
                return a ? a->val.ival : 0;
            };
            // 引数の有無にかかわらず 6 個出力（デフォルトは 0）
            // ただし全部 0 かつ名前指定なしなら省略してもよい…が、
            // ラウンドトリップのため元のパラメータ数を維持する
            size_t max_idx = 0;
            for (const auto& a : s.args) {
                if (a.key == "name") continue;
                for (size_t i = 0; i < 6; ++i)
                    if (a.key == pbg_names[i]) { max_idx = std::max(max_idx, i + 1); break; }
                if (a.key.size() > 1 && a.key[0] == 'p' && std::isdigit(a.key[1])) {
                    size_t idx = static_cast<size_t>(std::stoul(a.key.substr(1)));
                    max_idx = std::max(max_idx, idx + 1);
                }
            }
            std::vector<int32_t> params;
            for (size_t i = 0; i < max_idx; ++i) {
                if (i < 6)
                    params.push_back(named(i, pbg_names[i]));
                else {
                    std::string pk = "p" + std::to_string(i);
                    const Arg* a = find_arg_by_key(s.args, pk);
                    params.push_back(a ? a->val.ival : 0);
                }
            }
            out.push_back(make_cmd(LcfCode::ChangePBG, ctx.indent,
                arg_str(s.args, 0, "name"), params));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::TradeEventLocations, "TradeEventLocations",
        [](const LcfCmd& c) -> std::string {
            return "TradeEventLocations(event1=" + (char_ref_name(param(c,0)).empty() ? std::to_string(param(c,0)) : char_ref_name(param(c,0)))
                + ", event2=" + (char_ref_name(param(c,1)).empty() ? std::to_string(param(c,1)) : char_ref_name(param(c,1))) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            const Arg* e1 = find_arg_impl(s.args, 0, "event1");
            const Arg* e2 = find_arg_impl(s.args, 1, "event2");
            out.push_back(make_cmd(LcfCode::TradeEventLocations, ctx.indent,
                { e1 ? resolve_char_ref(*e1, "TradeEventLocations") : 1,
                  e2 ? resolve_char_ref(*e2, "TradeEventLocations") : 1 }));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::ChangeActorFace, "ChangeActorFace",
        [](const LcfCmd& c) -> std::string {
            return "ChangeActorFace(face=" + quote(str(c.string))
                + ", actor=" + std::to_string(param(c,0))
                + ", index=" + std::to_string(param(c,1)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::ChangeActorFace, ctx.indent,
                arg_str(s.args, 0, "face"),
                { arg_int(s.args, 0, "actor",  1),
                  arg_int(s.args, 1, "index",  0) }));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::SetVehicleLocation, "SetVehicleLocation",
        [](const LcfCmd& c) -> std::string {
            return "SetVehicleLocation(vehicle=" + std::to_string(param(c,0))
                + ", mode=" + std::to_string(param(c,1))
                + ", map=" + std::to_string(param(c,2))
                + ", x=" + std::to_string(param(c,3))
                + ", y=" + std::to_string(param(c,4)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::SetVehicleLocation, ctx.indent,
                { arg_int(s.args, 0, "vehicle", 0),
                  arg_int(s.args, 1, "mode",    0),
                  arg_int(s.args, 2, "map",     1),
                  arg_int(s.args, 3, "x",       0),
                  arg_int(s.args, 4, "y",       0) }));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::ChangeParameters, "ChangeParameters",
        [](const LcfCmd& c) -> std::string {
            return "ChangeParameters(who=" + std::to_string(param(c,0))
                + ", actor=" + std::to_string(param(c,1))
                + ", param=" + std::to_string(param(c,2))
                + ", op=" + std::to_string(param(c,3))
                + ", src=" + std::to_string(param(c,4))
                + ", val=" + std::to_string(param(c,5)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::ChangeParameters, ctx.indent,
                { arg_int(s.args, 0, "who",   0),
                  arg_int(s.args, 1, "actor", 1),
                  arg_int(s.args, 2, "param", 0),
                  arg_int(s.args, 3, "op",    0),
                  arg_int(s.args, 4, "src",   0),
                  arg_int(s.args, 5, "val",   0) }));
        }
    });

    // params: [action(0=add,1=remove), map, x, y, switch_on, switch_id]
    reg.register_cmd(CmdDef{ LcfCode::TeleportTargets, "TeleportTargets",
        [](const LcfCmd& c) -> std::string {
            return "TeleportTargets(action=" + std::to_string(param(c,0))
                + ", map=" + std::to_string(param(c,1))
                + ", x=" + std::to_string(param(c,2))
                + ", y=" + std::to_string(param(c,3))
                + ", switch_on=" + std::to_string(param(c,4))
                + ", switch_id=" + std::to_string(param(c,5)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::TeleportTargets, ctx.indent,
                { arg_int(s.args, 0, "action",    0),
                  arg_int(s.args, 1, "map",       1),
                  arg_int(s.args, 2, "x",         0),
                  arg_int(s.args, 3, "y",         0),
                  arg_int(s.args, 4, "switch_on", 0),
                  arg_int(s.args, 5, "switch_id", 1) }));
        }
    });

    // params: [map, x, y, switch_on, switch_id]
    reg.register_cmd(CmdDef{ LcfCode::EscapeTarget, "EscapeTarget",
        [](const LcfCmd& c) -> std::string {
            return "EscapeTarget(map=" + std::to_string(param(c,0))
                + ", x=" + std::to_string(param(c,1))
                + ", y=" + std::to_string(param(c,2))
                + ", switch_on=" + std::to_string(param(c,3))
                + ", switch_id=" + std::to_string(param(c,4)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::EscapeTarget, ctx.indent,
                { arg_int(s.args, 0, "map",       1),
                  arg_int(s.args, 1, "x",         0),
                  arg_int(s.args, 2, "y",         0),
                  arg_int(s.args, 3, "switch_on", 0),
                  arg_int(s.args, 4, "switch_id", 1) }));
        }
    });
    }); // std::call_once
}

} // namespace rm2k
