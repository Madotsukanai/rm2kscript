#include "commands.h"
#include <sstream>
#include <stdexcept>
#include <algorithm>

namespace rm2k {

// ──────────────────────────────────────────────────────────────────────────────
// CommandRegistry
// ──────────────────────────────────────────────────────────────────────────────

CommandRegistry& CommandRegistry::instance() {
    static CommandRegistry reg;
    return reg;
}

void CommandRegistry::register_cmd(CmdDef def, bool alias_only) {
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

// ──────────────────────────────────────────────────────────────────────────────
// 引数ヘルパー (内部)
// ──────────────────────────────────────────────────────────────────────────────

// find_arg は commands.h で宣言されていないので static にする
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

// ──────────────────────────────────────────────────────────────────────────────
// エンコード用ヘルパー: party/actor の解析
// ──────────────────────────────────────────────────────────────────────────────

// "party" or "actor=N" を [who_type, actor_id] にデコード
// who_type: 0=actor, 1=party
static std::pair<int32_t,int32_t> parse_who(const std::vector<Arg>& args) {
    // 位置0の識別子が "party" なら party
    const Arg* pos0 = find_arg_impl(args, 0, "");
    if (pos0 && pos0->val.kind == ArgVal::Ident && pos0->val.sval == "party")
        return {1, 0};
    // "actor" キーが存在すれば actor=N
    const Arg* act = find_arg_by_key(args, "actor");
    if (act) return {0, act->val.ival};
    // デフォルト: actor=1
    return {0, 1};
}

// ──────────────────────────────────────────────────────────────────────────────
// 余分なパラメータ保持ヘルパー
// ──────────────────────────────────────────────────────────────────────────────

// encode: s.args から "pN" キーの引数を pos=known_count から読み取り params に追加
static void append_extra_params(const CmdStmt& s, size_t known_count,
                                 std::vector<int32_t>& params) {
    // pN キーの引数を順に読み取る (p0, p1, p2 ...)
    for (size_t i = known_count; ; ++i) {
        std::string key = "p" + std::to_string(i);
        const Arg* a = nullptr;
        for (const auto& arg : s.args)
            if (arg.key == key) { a = &arg; break; }
        if (!a) break;
        params.push_back(a->val.ival);
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// デコード用ユーティリティ
// ──────────────────────────────────────────────────────────────────────────────

static std::string quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else                out += c;
    }
    out += '"';
    return out;
}

static inline std::string str(const lcf::DBString& ds) {
    return std::string(ds.begin(), ds.end());
}

// ──────────────────────────────────────────────────────────────────────────────
// register_all_commands
// ──────────────────────────────────────────────────────────────────────────────

void register_all_commands() {
    auto& reg = CommandRegistry::instance();
    if (!reg.all().empty()) return;

    // ── 文章表示 ─────────────────────────────────────────────────────────────
    // message("line1\nline2") → ShowMessage + ShowMessage_2... エンコード
    auto encode_message_fn = [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
        std::string text = arg_str(s.args, 0, "");
        // \n で行分割
        std::vector<std::string> lines;
        std::string cur;
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '\n') { lines.push_back(cur); cur.clear(); }
            else                  cur += text[i];
        }
        lines.push_back(cur);
        // 最初の行: ShowMessage
        if (lines.empty()) lines.push_back("");
        out.push_back(make_cmd(LcfCode::ShowMessage, ctx.indent, lines[0], {}));
        // 続く行: ShowMessage_2
        for (size_t i = 1; i < lines.size(); ++i)
            out.push_back(make_cmd(LcfCode::ShowMessage_2, ctx.indent, lines[i], {}));
    };

    reg.register_cmd(CmdDef{ LcfCode::ShowMessage, "ShowMessage",
        [](const LcfCmd& c) -> std::string {
            return "ShowMessage(" + quote(str(c.string)) + ")";
        },
        encode_message_fn
    });

    // "message" は ShowMessage のエイリアス（新形式デコーダーが出力する名前）
    // code は ShowMessage と同じだが name が異なるため別エントリが必要
    // → CommandRegistry は name→code の逆引きも持つため、コード重複は問題なし
    {
        CmdDef d;
        d.code   = LcfCode::ShowMessage;
        d.name   = "message";
        d.decode = nullptr; // デコードは ShowMessage が担当
        d.encode = encode_message_fn;
        reg.register_cmd(std::move(d), /*alias_only=*/true);
    }

    reg.register_cmd(CmdDef{ LcfCode::ShowMessage_2, "ShowMessage_2",
        [](const LcfCmd& c) -> std::string {
            return "ShowMessage_2(" + quote(str(c.string)) + ")";
        },
        [](const CmdStmt&, EncodeCtx&, CmdVec&) {}
    });

    // ── コメント ──────────────────────────────────────────────────────────────
    // comment("text") → Comment + Comment_2... (行分割)
    {
        auto encode_comment_fn = [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            std::string text = arg_str(s.args, 0, "");
            // \n で行分割
            std::vector<std::string> lines;
            std::string cur;
            for (size_t i = 0; i < text.size(); ++i) {
                if (text[i] == '\n') { lines.push_back(cur); cur.clear(); }
                else                  cur += text[i];
            }
            lines.push_back(cur);
            if (lines.empty()) lines.push_back("");
            out.push_back(make_cmd(LcfCode::Comment, ctx.indent, lines[0], {}));
            for (size_t i = 1; i < lines.size(); ++i)
                out.push_back(make_cmd(LcfCode::Comment_2, ctx.indent, lines[i], {}));
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
                return "comment(" + quote(str(c.string)) + ")";
            },
            [](const CmdStmt&, EncodeCtx&, CmdVec&) {}
        });
    }

    // ── 選択肢 ───────────────────────────────────────────────────────────────
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

    // ── メッセージオプション ──────────────────────────────────────────────────
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

    // ── フェイスグラフィック ─────────────────────────────────────────────────
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

    // ── スイッチ操作 ──────────────────────────────────────────────────────────
    reg.register_cmd(CmdDef{ LcfCode::ControlSwitches, "ControlSwitches",
        [](const LcfCmd& c) -> std::string {
            int32_t type = param(c,0);
            int32_t id1  = param(c,1);
            int32_t id2  = param(c,2);
            int32_t val  = param(c,3);
            std::string op = (val==0?"OFF": val==1?"ON":"toggle");
            if (type == 0)
                return "ControlSwitches(switch[" + std::to_string(id1) + "] = " + op + ")";
            if (type == 1)
                return "ControlSwitches(switch[" + std::to_string(id1) + ".."
                     + std::to_string(id2) + "] = " + op + ")";
            return "ControlSwitches(switch[var[" + std::to_string(id1) + "]] = " + op + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            const Arg* sw = find_arg(s.args, 0, "switch");
            if (!sw) throw std::runtime_error("ControlSwitches: missing switch arg");
            int32_t id1 = sw->val.ival;
            int32_t id2 = id1;
            const Arg* to = find_arg(s.args, 1, "to");
            if (to) id2 = to->val.ival;
            int32_t type = (id1 == id2) ? 0 : 1;
            std::string op_s = arg_ident(s.args, 1, "op", "ON");
            int32_t op_i = (op_s=="OFF"?0: op_s=="ON"?1:2);
            out.push_back(make_cmd(LcfCode::ControlSwitches, ctx.indent, {type, id1, id2, op_i}));
        }
    });

    // ── 変数操作 ──────────────────────────────────────────────────────────────
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
            // src==0 (constant): var[N] op val
            // src==1 (var):      var[N] op var[M]
            // src>=2 (other):    var[N] = value, src=N, src_val=M  (keyword form)
            if (src <= 1) {
                std::string rhs = (src == 0) ? std::to_string(val) : "var[" + std::to_string(val) + "]";
                if (type == 0)
                    return "ControlVars(var[" + std::to_string(id1) + "] " + op_s + " " + rhs + ")";
                return "ControlVars(var[" + std::to_string(id1) + ".." + std::to_string(id2) + "] " + op_s + " " + rhs + ")";
            }
            // 複雑なソースはキー=値形式で出力 (opはop_code番号で表現)
            // p[6] が存在する場合は src_extra として追加
            std::string r = "ControlVars(var=" + std::to_string(id1) + ", op_code=" + std::to_string(op)
                 + ", src=" + std::to_string(src) + ", src_val=" + std::to_string(val);
            if (c.parameters.size() > 6)
                r += ", src_extra=" + std::to_string(param(c,6));
            r += ")";
            return r;
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            // キー形式: ControlVars(var=N, type=T, id2=M, op_code=op, src=src, src_val=val)
            // または簡易形式: ControlVars(var[N] += value)
            int32_t id1 = 1, val = 0, src = 0, op_i = 0;
            const Arg* va = find_arg(s.args, 0, "var");
            if (va) {
                // キー形式
                id1  = va->val.ival;
                int32_t type_v = arg_int(s.args, 0, "type", 0);
                int32_t id2_v  = arg_int(s.args, 0, "id2", id1);
                val  = arg_int(s.args, 0, "src_val", 0);
                src  = arg_int(s.args, 0, "src", 0);
                op_i = arg_int(s.args, 0, "op_code", 0);
                std::string op_s = arg_ident(s.args, 0, "op", "");
                if      (op_s == "+=") op_i = 1;
                else if (op_s == "-=") op_i = 2;
                else if (op_s == "*=") op_i = 3;
                else if (op_s == "/=") op_i = 4;
                else if (op_s == "%=") op_i = 5;
                std::vector<int32_t> params = {type_v, id1, id2_v, op_i, src, val};
                const Arg* extra_a = nullptr;
                for (const auto& a : s.args)
                    if (a.key == "src_extra") { extra_a = &a; break; }
                if (extra_a) params.push_back(extra_a->val.ival);
                out.push_back(make_cmd(LcfCode::ControlVars, ctx.indent, params));
                return;
            } else {
                // Indexed形式: var[N] は位置引数0
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
            out.push_back(make_cmd(LcfCode::ControlVars, ctx.indent, {0, id1, id1, op_i, src, val}));
        }
    });

    // ── タイマー ─────────────────────────────────────────────────────────────
    // params: [op, val, p2, p3, p4]  (実データは常に5個)
    // op=0:set, op=1:start, op=2:stop
    reg.register_cmd(CmdDef{ LcfCode::TimerOperation, "TimerOperation",
        [](const LcfCmd& c) -> std::string {
            int32_t op = param(c,0);
            std::string r;
            if (op == 0) r = "TimerOperation(set=" + std::to_string(param(c,1));
            else if (op == 1) r = "TimerOperation(start, p1=" + std::to_string(param(c,1));
            else r = "TimerOperation(stop, p1=" + std::to_string(param(c,1));
            r += ", p2=" + std::to_string(param(c,2))
               + ", p3=" + std::to_string(param(c,3))
               + ", p4=" + std::to_string(param(c,4)) + ")";
            return r;
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
            out.push_back(make_cmd(LcfCode::TimerOperation, ctx.indent, {op_i, val, p2, p3, p4}));
        }
    });

    // ── 金 ───────────────────────────────────────────────────────────────────
    reg.register_cmd(CmdDef{ LcfCode::ChangeGold, "ChangeGold",
        [](const LcfCmd& c) -> std::string {
            int32_t op  = param(c,0);
            int32_t src = param(c,1);
            int32_t val = param(c,2);
            std::string v = src ? "var[" + std::to_string(val) + "]" : std::to_string(val);
            return "ChangeGold(delta=" + std::string(op?"-":"") + v + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            // delta= の引数を取得: -var[N] の場合は sval="-var", ival=N
            const Arg* da = find_arg_impl(s.args, 0, "delta");
            if (da && da->val.kind == ArgVal::Indexed && da->val.sval == "-var") {
                // delta=-var[N]
                out.push_back(make_cmd(LcfCode::ChangeGold, ctx.indent,
                    {1, 1, da->val.ival}));  // op=subtract, src=var, val=N
            } else if (da && da->val.kind == ArgVal::Indexed && da->val.sval == "var") {
                // delta=var[N]
                out.push_back(make_cmd(LcfCode::ChangeGold, ctx.indent,
                    {0, 1, da->val.ival}));
            } else {
                int32_t delta = da ? da->val.ival : 0;
                out.push_back(make_cmd(LcfCode::ChangeGold, ctx.indent,
                    {delta < 0 ? 1 : 0, 0, std::abs(delta)}));
            }
        }
    });

    // ── アイテム ─────────────────────────────────────────────────────────────
    // params: [item_id, op(0=+,1=-), src(0=const,1=var), amount, p4]  (常に5個)
    reg.register_cmd(CmdDef{ LcfCode::ChangeItems, "ChangeItems",
        [](const LcfCmd& c) -> std::string {
            std::string r = "ChangeItems(item=" + std::to_string(param(c,0))
                 + ", op=" + std::to_string(param(c,1))
                 + ", src=" + std::to_string(param(c,2))
                 + ", amount=" + std::to_string(param(c,3))
                 + ", p4=" + std::to_string(param(c,4));
            for (size_t i = 5; i < c.parameters.size(); ++i)
                r += ", p" + std::to_string(i) + "=" + std::to_string(c.parameters[i]);
            r += ")";
            return r;
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            int32_t item   = arg_int(s.args,0,"item",1);
            int32_t op_v   = arg_int(s.args,0,"op",0);
            int32_t src_v  = arg_int(s.args,0,"src",0);
            int32_t amount = arg_int(s.args,0,"amount",0);
            int32_t p4     = arg_int(s.args,0,"p4",0);
            std::vector<int32_t> params = {item, op_v, src_v, amount, p4};
            for (size_t i = 5; ; ++i) {
                std::string key = "p" + std::to_string(i);
                const Arg* a = nullptr;
                for (const auto& arg : s.args) if (arg.key == key) { a = &arg; break; }
                if (!a) break;
                params.push_back(a->val.ival);
            }
            out.push_back(make_cmd(LcfCode::ChangeItems, ctx.indent, params));
        }
    });

    // ── パーティ ─────────────────────────────────────────────────────────────
    // params: [op(0=add,1=remove), src(0=const,1=var), actor_id]  (常に3個)
    reg.register_cmd(CmdDef{ LcfCode::ChangePartyMembers, "ChangePartyMembers",
        [](const LcfCmd& c) -> std::string {
            const char* op = param(c,0) ? "remove" : "add";
            std::string r = std::string("ChangePartyMembers(") + op
                + ", src=" + std::to_string(param(c,1))
                + ", actor=" + std::to_string(param(c,2));
            for (size_t i = 3; i < c.parameters.size(); ++i)
                r += ", p" + std::to_string(i) + "=" + std::to_string(c.parameters[i]);
            r += ")";
            return r;
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            int32_t op    = arg_ident(s.args,0) == "remove" ? 1 : 0;
            int32_t src   = arg_int(s.args,0,"src",0);
            int32_t actor = arg_int(s.args,0,"actor",1);
            std::vector<int32_t> params = {op, src, actor};
            for (size_t i = 3; ; ++i) {
                std::string key = "p" + std::to_string(i);
                const Arg* a = nullptr;
                for (const auto& arg : s.args) if (arg.key == key) { a = &arg; break; }
                if (!a) break;
                params.push_back(a->val.ival);
            }
            out.push_back(make_cmd(LcfCode::ChangePartyMembers, ctx.indent, params));
        }
    });

    // ── EXP/レベル ───────────────────────────────────────────────────────────
    reg.register_cmd(CmdDef{ LcfCode::ChangeExp, "ChangeExp",
        [](const LcfCmd& c) -> std::string {
            // params: [who(0=actor,1=party), actor_id, op(0=+,1=-), src(0=const,1=var), val, show_levelup]
            int32_t who = param(c,0); int32_t id = param(c,1);
            int32_t op = param(c,2); int32_t src = param(c,3); int32_t val = param(c,4);
            int32_t show = param(c,5);
            std::string t = (who==0) ? "actor="+std::to_string(id)
                                     : "party, actor="+std::to_string(id);
            std::string rhs = (src == 0) ? std::to_string(val) : "var["+std::to_string(val)+"]";
            return "ChangeExp(" + t + ", op=" + std::to_string(op)
                 + ", src=" + std::to_string(src) + ", val=" + rhs
                 + ", show=" + std::to_string(show) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            auto [who, actor] = parse_who(s.args);
            const Arg* act_a = nullptr;
            for (const auto& a : s.args)
                if (a.key == "actor") { act_a = &a; break; }
            if (act_a) actor = act_a->val.ival;
            int32_t op   = arg_int(s.args,0,"op",0);
            int32_t src  = arg_int(s.args,0,"src",0);
            // val: var[N] or const
            int32_t val  = 0;
            const Arg* va = find_arg_impl(s.args, 0, "val");
            if (va) {
                if (va->val.kind == ArgVal::Indexed && va->val.sval == "var") {
                    src = 1; val = va->val.ival;
                } else {
                    val = va->val.ival;
                }
            }
            int32_t show = arg_int(s.args,0,"show",0);
            out.push_back(make_cmd(LcfCode::ChangeExp, ctx.indent,
                {who, actor, op, src, val, show}));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::ChangeLevel, "ChangeLevel",
        [](const LcfCmd& c) -> std::string {
            int32_t who = param(c,0); int32_t id = param(c,1);
            int32_t op = param(c,2); int32_t src = param(c,3); int32_t val = param(c,4);
            std::string t = (who==0) ? "actor="+std::to_string(id)
                                     : "party, actor="+std::to_string(id);
            std::string r = "ChangeLevel(" + t
                + ", op=" + std::to_string(op)
                + ", src=" + std::to_string(src)
                + ", val=" + std::to_string(val);
            if (c.parameters.size() >= 6)
                r += ", show=" + std::to_string(param(c,5));
            r += ")";
            return r;
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            auto [who, actor] = parse_who(s.args);
            const Arg* act_a = nullptr;
            for (const auto& a : s.args)
                if (a.key == "actor") { act_a = &a; break; }
            if (act_a) actor = act_a->val.ival;
            std::vector<int32_t> params = {
                who, actor,
                arg_int(s.args,0,"op",0),
                arg_int(s.args,1,"src",0),
                arg_int(s.args,2,"val",0)
            };
            const Arg* sh = find_arg(s.args, 3, "show");
            if (sh) params.push_back(sh->val.ival);
            out.push_back(make_cmd(LcfCode::ChangeLevel, ctx.indent, params));
        }
    });

    // ── HP/SP ────────────────────────────────────────────────────────────────
    reg.register_cmd(CmdDef{ LcfCode::ChangeHP, "ChangeHP",
        [](const LcfCmd& c) -> std::string {
            int32_t who = param(c,0); int32_t id = param(c,1);
            int32_t op = param(c,2); int32_t val = param(c,4);
            std::string t = (who==0) ? "actor="+std::to_string(id) : "party";
            return "ChangeHP(" + t + ", delta=" + std::string(op?"-":"") + std::to_string(val) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            auto [who, actor] = parse_who(s.args);
            int32_t delta = arg_int(s.args,1,"delta",0);
            int32_t die   = arg_int(s.args,2,"die_allowed",0);
            out.push_back(make_cmd(LcfCode::ChangeHP, ctx.indent,
                {who, actor, delta<0?1:0, 0, std::abs(delta), die}));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::ChangeSP, "ChangeSP",
        [](const LcfCmd& c) -> std::string {
            int32_t who = param(c,0); int32_t id = param(c,1);
            int32_t op = param(c,2); int32_t val = param(c,4);
            std::string t = (who==0) ? "actor="+std::to_string(id) : "party";
            return "ChangeSP(" + t + ", delta=" + std::string(op?"-":"") + std::to_string(val) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            auto [who, actor] = parse_who(s.args);
            int32_t delta = arg_int(s.args,1,"delta",0);
            out.push_back(make_cmd(LcfCode::ChangeSP, ctx.indent,
                {who, actor, delta<0?1:0, 0, std::abs(delta)}));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::FullHeal, "FullHeal",
        [](const LcfCmd& c) -> std::string {
            int32_t who = param(c,0); int32_t id = param(c,1);
            std::string t = (who==0) ? "actor="+std::to_string(id) : "party";
            return "FullHeal(" + t + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            auto [who, actor] = parse_who(s.args);
            out.push_back(make_cmd(LcfCode::FullHeal, ctx.indent, {who, actor}));
        }
    });

    // ── スキル/状態 ──────────────────────────────────────────────────────────
    // params: [who(0=actor,1=party), actor_id, op(0=learn,1=forget), src(0=const,1=var), skill_id]
    reg.register_cmd(CmdDef{ LcfCode::ChangeSkills, "ChangeSkills",
        [](const LcfCmd& c) -> std::string {
            int32_t who = param(c,0); int32_t id = param(c,1);
            int32_t op = param(c,2); int32_t src = param(c,3); int32_t sk = param(c,4);
            std::string t = (who==0) ? "actor="+std::to_string(id) : "party";
            std::string rhs = (src==0) ? std::to_string(sk) : "var["+std::to_string(sk)+"]";
            return "ChangeSkills(" + t + ", op=" + (op?"forget":"learn") + ", src=" + std::to_string(src) + ", skill=" + rhs + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            auto [who, actor] = parse_who(s.args);
            std::string op_s = arg_ident(s.args,1,"op","learn");
            int32_t src = arg_int(s.args,0,"src",0);
            // skill= は var[N] 形式または数値
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
            std::string t = (who==0) ? "actor="+std::to_string(id) : "party";
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

    // params: [who(0=actor,1=party), actor_id, op(0=equip,1=unequip), src, item_id]
    // src=0: item_id は直接値; src!=0: item_id は変数ID (src の値はそのまま保持)
    reg.register_cmd(CmdDef{ LcfCode::ChangeEquipment, "ChangeEquipment",
        [](const LcfCmd& c) -> std::string {
            int32_t who  = param(c,0); int32_t actor = param(c,1);
            int32_t op   = param(c,2); int32_t src   = param(c,3); int32_t item = param(c,4);
            std::string t = (who==0) ? "actor="+std::to_string(actor) : "party";
            std::string rhs = (src==0) ? std::to_string(item) : "var["+std::to_string(item)+"]";
            return "ChangeEquipment(" + t + ", op=" + std::to_string(op)
                 + ", src=" + std::to_string(src) + ", item=" + rhs + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            auto [who, actor] = parse_who(s.args);
            int32_t op  = arg_int(s.args,0,"op",0);
            int32_t src = arg_int(s.args,0,"src",0);  // src は数値のまま保持
            int32_t item = 0;
            const Arg* ia = find_arg_impl(s.args, 0, "item");
            if (ia) {
                item = ia->val.ival;  // var[N] でも数値でも ival に値が入る
            }
            out.push_back(make_cmd(LcfCode::ChangeEquipment, ctx.indent,
                {who, actor, op, src, item}));
        }
    });

    // ── 名前/肩書き ──────────────────────────────────────────────────────────
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
            return "EnterHeroName(actor=" + std::to_string(param(c,0))
                 + ", max=" + std::to_string(param(c,1)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::EnterHeroName, ctx.indent, {
                arg_int(s.args,0,"actor",1), arg_int(s.args,1,"max",6), 0
            }));
        }
    });

    // ── テレポート ────────────────────────────────────────────────────────────
    reg.register_cmd(CmdDef{ LcfCode::Teleport, "Teleport",
        [](const LcfCmd& c) -> std::string {
            // params: [map, x, y] or [map, x, y, dir, fade]
            std::string r = "Teleport(map=" + std::to_string(param(c,0))
                 + ", x=" + std::to_string(param(c,1))
                 + ", y=" + std::to_string(param(c,2));
            if (c.parameters.size() > 3)
                r += ", dir=" + std::to_string(param(c,3))
                   + ", fade=" + std::to_string(param(c,4));
            r += ")";
            return r;
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            std::vector<int32_t> params = {
                arg_int(s.args,0,"map",1), arg_int(s.args,1,"x",0), arg_int(s.args,2,"y",0)
            };
            // dir/fade が指定されていれば追加
            bool has_dir = false;
            for (const auto& a : s.args) if (a.key == "dir" || a.key == "fade") { has_dir = true; break; }
            if (has_dir) {
                params.push_back(arg_int(s.args,0,"dir",0));
                params.push_back(arg_int(s.args,0,"fade",0));
            }
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

    // ── ストア ────────────────────────────────────────────────────────────────
    // params: [src(0=direct,1=var), x, y, var]  (常に4個)
    // src=0: x,y は座標値;  src=1: x,y は変数ID
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

    // params: [src(0=direct,1=var), x, y, var]  (常に4個)
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

    // ── 画面効果 ──────────────────────────────────────────────────────────────
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
            return "FlashScreen(r=" + std::to_string(param(c,0))
                 + ", g=" + std::to_string(param(c,1)) + ", b=" + std::to_string(param(c,2))
                 + ", power=" + std::to_string(param(c,3)) + ", frames=" + std::to_string(param(c,4))
                 + ", wait=" + std::to_string(param(c,5)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::FlashScreen, ctx.indent, {
                arg_int(s.args,0,"r",255), arg_int(s.args,1,"g",255),
                arg_int(s.args,2,"b",255), arg_int(s.args,3,"power",31),
                arg_int(s.args,4,"frames",20), arg_int(s.args,5,"wait",1)
            }));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::ShakeScreen, "ShakeScreen",
        [](const LcfCmd& c) -> std::string {
            return "ShakeScreen(power=" + std::to_string(param(c,0))
                 + ", speed=" + std::to_string(param(c,1)) + ", frames=" + std::to_string(param(c,2))
                 + ", wait=" + std::to_string(param(c,3)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::ShakeScreen, ctx.indent, {
                arg_int(s.args,0,"power",5), arg_int(s.args,1,"speed",5),
                arg_int(s.args,2,"frames",20), arg_int(s.args,3,"wait",1)
            }));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::PanScreen, "PanScreen",
        [](const LcfCmd& c) -> std::string {
            // 常に全パラメータを出力 (ラウンドトリップのため)
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

    // ── ピクチャ ─────────────────────────────────────────────────────────────
    reg.register_cmd(CmdDef{ LcfCode::ShowPicture, "ShowPicture",
        [](const LcfCmd& c) -> std::string {
            // 全パラメータを pN= 形式で保存 (ラウンドトリップのため)
            std::string r = "ShowPicture(file=" + quote(str(c.string));
            for (size_t i = 0; i < c.parameters.size(); ++i)
                r += ", p" + std::to_string(i) + "=" + std::to_string(c.parameters[i]);
            r += ")";
            return r;
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            std::vector<int32_t> params;
            for (size_t i = 0; ; ++i) {
                std::string key = "p" + std::to_string(i);
                bool found = false;
                for (const auto& arg : s.args)
                    if (arg.key == key) { params.push_back(arg.val.ival); found = true; break; }
                if (!found) break;
            }
            out.push_back(make_cmd(LcfCode::ShowPicture, ctx.indent,
                arg_str(s.args, 0, "file"), params));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::ErasePicture, "ErasePicture",
        [](const LcfCmd& c) -> std::string {
            return "ErasePicture(id=" + std::to_string(param(c,0)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::ErasePicture, ctx.indent, {arg_int(s.args,0,"id",1)}));
        }
    });

    // ── バトルアニメ ──────────────────────────────────────────────────────────
    reg.register_cmd(CmdDef{ LcfCode::ShowBattleAnimation, "ShowBattleAnimation",
        [](const LcfCmd& c) -> std::string {
            int32_t who = param(c,1);
            std::string w = (who == 10001) ? "this" : "event="+std::to_string(who);
            std::string r = "ShowBattleAnimation(anim=" + std::to_string(param(c,0))
                 + ", " + w + ", wait=" + std::to_string(param(c,2));
            // 4番目のパラメータ (p3) が存在する場合は出力
            if (c.parameters.size() >= 4)
                r += ", p3=" + std::to_string(param(c,3));
            r += ")";
            return r;
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            int32_t who_i = 10001;
            const Arg* ea = find_arg_by_key(s.args, "event");
            if (ea) {
                who_i = ea->val.ival;
            } else {
                std::string who_s = arg_ident(s.args,1,"","this");
                if (who_s != "this") {
                    try { who_i = std::stoi(who_s); } catch (...) {}
                }
            }
            std::vector<int32_t> params = {
                arg_int(s.args,0,"anim",1), who_i, arg_int(s.args,2,"wait",1)
            };
            const Arg* p3 = find_arg(s.args, 3, "p3");
            if (p3) params.push_back(p3->val.ival);
            out.push_back(make_cmd(LcfCode::ShowBattleAnimation, ctx.indent, params));
        }
    });

    // ── プレイヤー表示 ────────────────────────────────────────────────────────
    reg.register_cmd(CmdDef{ LcfCode::PlayerVisibility, "PlayerVisibility",
        [](const LcfCmd& c) -> std::string {
            return "PlayerVisibility(visible=" + std::to_string(param(c,0)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::PlayerVisibility, ctx.indent, {arg_int(s.args,0,"visible",1)}));
        }
    });

    // ── イベント移動 ──────────────────────────────────────────────────────────
    reg.register_cmd(CmdDef{ LcfCode::MoveEvent, "MoveEvent",
        [](const LcfCmd& c) -> std::string {
            int32_t who = param(c,0);
            std::string w = (who==-1)?"player":(who==10001?"this":"event="+std::to_string(who));
            // 移動ルートデータ (p1以降) を pN= 形式で保存
            std::string r = "MoveEvent(" + w;
            for (size_t i = 1; i < c.parameters.size(); ++i)
                r += ", p" + std::to_string(i) + "=" + std::to_string(c.parameters[i]);
            r += ")";
            return r;
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            // event=N キーが指定されている場合
            const Arg* ea = find_arg_by_key(s.args, "event");
            int32_t who;
            if (ea) {
                who = ea->val.ival;
            } else {
                std::string who_s = arg_ident(s.args,0,"","this");
                if      (who_s == "player") who = -1;
                else if (who_s == "this")   who = 10001;
                else who = 10001; // デフォルト
            }
            std::vector<int32_t> params = {who};
            append_extra_params(s, 1, params);
            out.push_back(make_cmd(LcfCode::MoveEvent, ctx.indent, params));
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

    // ── 待機 ─────────────────────────────────────────────────────────────────
    reg.register_cmd(CmdDef{ LcfCode::Wait, "Wait",
        [](const LcfCmd& c) -> std::string {
            return "Wait(frames=" + std::to_string(param(c,0)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::Wait, ctx.indent, {arg_int(s.args,0,"frames",30)}));
        }
    });

    // ── BGM ──────────────────────────────────────────────────────────────────
    reg.register_cmd(CmdDef{ LcfCode::PlayBGM, "PlayBGM",
        [](const LcfCmd& c) -> std::string {
            // params: [event_id, volume, tempo, balance?]
            std::string r = "PlayBGM(" + quote(str(c.string))
                 + ", event=" + std::to_string(param(c,0))
                 + ", volume=" + std::to_string(param(c,1))
                 + ", tempo=" + std::to_string(param(c,2));
            if (c.parameters.size() > 3)
                r += ", balance=" + std::to_string(param(c,3));
            r += ")";
            return r;
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            int32_t ev  = arg_int(s.args,0,"event",0);
            int32_t vol = arg_int(s.args,0,"volume",100);
            int32_t tpo = arg_int(s.args,0,"tempo",100);
            std::vector<int32_t> params = {ev, vol, tpo};
            // balance が指定されていれば追加
            const Arg* bal = nullptr;
            for (const auto& a : s.args) if (a.key == "balance") { bal = &a; break; }
            if (bal) params.push_back(bal->val.ival);
            out.push_back(make_cmd(LcfCode::PlayBGM, ctx.indent, arg_str(s.args,0,"name"), params));
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

    // ── SE ───────────────────────────────────────────────────────────────────
    reg.register_cmd(CmdDef{ LcfCode::PlaySound, "PlaySound",
        [](const LcfCmd& c) -> std::string {
            // params: [p0, p1?, volume?, tempo?, balance?] — パラメータ数がバリアブル
            // 最もシンプルな処理: 全パラメータを pN= 形式で保存
            std::string r = "PlaySound(" + quote(str(c.string));
            for (size_t i = 0; i < c.parameters.size(); ++i)
                r += ", p" + std::to_string(i) + "=" + std::to_string(c.parameters[i]);
            r += ")";
            return r;
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            std::vector<int32_t> params;
            append_extra_params(s, 0, params);
            out.push_back(make_cmd(LcfCode::PlaySound, ctx.indent, arg_str(s.args,0,"name"), params));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::PlayMovie, "PlayMovie",
        [](const LcfCmd& c) -> std::string {
            return "PlayMovie(file=" + quote(str(c.string))
                 + ", x=" + std::to_string(param(c,0)) + ", y=" + std::to_string(param(c,1))
                 + ", w=" + std::to_string(param(c,2)) + ", h=" + std::to_string(param(c,3)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::PlayMovie, ctx.indent,
                arg_str(s.args,0,"file"),
                {arg_int(s.args,1,"x",0), arg_int(s.args,2,"y",0),
                 arg_int(s.args,3,"w",320), arg_int(s.args,4,"h",240)}));
        }
    });

    // ── キー入力 ─────────────────────────────────────────────────────────────
    reg.register_cmd(CmdDef{ LcfCode::KeyInputProc, "KeyInputProc",
        [](const LcfCmd& c) -> std::string {
            return "KeyInputProc(var=" + std::to_string(param(c,0))
                 + ", wait=" + std::to_string(param(c,1)) + ", dir=" + std::to_string(param(c,3))
                 + ", act=" + std::to_string(param(c,4)) + ", cancel=" + std::to_string(param(c,5)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::KeyInputProc, ctx.indent, {
                arg_int(s.args,0,"var",1), arg_int(s.args,1,"wait",1), 0,
                arg_int(s.args,2,"dir",1), arg_int(s.args,3,"act",1), arg_int(s.args,4,"cancel",1)
            }));
        }
    });

    // ── 戦闘 ─────────────────────────────────────────────────────────────────
    reg.register_cmd(CmdDef{ LcfCode::EnemyEncounter, "EnemyEncounter",
        [](const LcfCmd& c) -> std::string {
            return "EnemyEncounter(troop=" + std::to_string(param(c,0))
                 + ", type=" + std::to_string(param(c,1))
                 + ", can_escape=" + std::to_string(param(c,4))
                 + ", can_lose=" + std::to_string(param(c,5)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::EnemyEncounter, ctx.indent, {
                arg_int(s.args,0,"troop",1), arg_int(s.args,1,"type",0), 0, 0,
                arg_int(s.args,2,"can_escape",1), arg_int(s.args,3,"can_lose",0)
            }));
        }
    });

    // ── ショップ/宿屋 ─────────────────────────────────────────────────────────
    reg.register_cmd(CmdDef{ LcfCode::OpenShop, "OpenShop",
        [](const LcfCmd& c) -> std::string {
            std::string r = "OpenShop(type=" + std::to_string(param(c,0));
            for (size_t i = 1; i < c.parameters.size(); ++i)
                r += ", item" + std::to_string(i) + "=" + std::to_string(c.parameters[i]);
            return r + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            std::vector<int32_t> ps = {arg_int(s.args,0,"type",0)};
            // item1=N, item2=N, ... のキー引数を収集
            // キー名は "item1", "item2", ... で番号順に収集
            int idx = 1;
            while (true) {
                std::string key = "item" + std::to_string(idx);
                bool found = false;
                for (const auto& a : s.args) {
                    if (a.key == key) { ps.push_back(a.val.ival); found = true; break; }
                }
                if (!found) {
                    // 位置引数としても試みる
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
            return "ShowInn(type=" + std::to_string(param(c,0)) + ", cost=" + std::to_string(param(c,1)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::ShowInn, ctx.indent,
                {arg_int(s.args,0,"type",0), arg_int(s.args,1,"cost",100)}));
        }
    });

    // ── ラベル/ジャンプ/ループ ─────────────────────────────────────────────
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

    // ── フロー制御 ────────────────────────────────────────────────────────────
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

    reg.register_cmd(CmdDef{ LcfCode::CallEvent, "CallEvent",
        [](const LcfCmd& c) -> std::string {
            int32_t type = param(c,0);
            // 常に全パラメータを出力してラウンドトリップを保証
            return "CallEvent(type=" + std::to_string(type)
                + ", id=" + std::to_string(param(c,1))
                + ", page=" + std::to_string(param(c,2)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            int32_t type = arg_int(s.args,0,"type",0);
            int32_t id   = arg_int(s.args,1,"id",0);
            int32_t page = arg_int(s.args,2,"page",0);
            // 旧形式: event=N, page=M
            const Arg* ev_a = find_arg(s.args,0,"event");
            if (ev_a) { type = 0; id = ev_a->val.ival; page = arg_int(s.args,1,"page",0); }
            // 旧形式: common=N
            const Arg* co_a = find_arg(s.args,0,"common");
            if (co_a) { type = 1; id = co_a->val.ival; page = arg_int(s.args,1,"page",0); }
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

    // ── ゲームオーバー/タイトル ──────────────────────────────────────────────
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

    // ── セーブ/メニュー ─────────────────────────────────────────────────────
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

    // ── アクセス制御 ─────────────────────────────────────────────────────────
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

    // ── マップシステム ────────────────────────────────────────────────────────
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

    // ── システム設定 ────────────────────────────────────────────────────────
    reg.register_cmd(CmdDef{ LcfCode::ChangeSystemBGM, "ChangeSystemBGM",
        [](const LcfCmd& c) -> std::string {
            // params: [event_type, volume?, tempo?, balance?, p4?]
            std::string r = "ChangeSystemBGM(event=" + std::to_string(param(c,0))
                 + ", bgm=" + quote(str(c.string));
            if (c.parameters.size() > 1)
                r += ", volume=" + std::to_string(param(c,1))
                   + ", tempo=" + std::to_string(param(c,2))
                   + ", balance=" + std::to_string(param(c,3));
            if (c.parameters.size() > 4)
                r += ", p4=" + std::to_string(param(c,4));
            r += ")";
            return r;
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            int32_t ev  = arg_int(s.args,0,"event",0);
            std::vector<int32_t> params = {ev};
            const Arg* vol = nullptr;
            for (const auto& a : s.args) if (a.key == "volume") { vol = &a; break; }
            if (vol) {
                params.push_back(vol->val.ival);
                params.push_back(arg_int(s.args,0,"tempo",100));
                params.push_back(arg_int(s.args,0,"balance",50));
                append_extra_params(s, 4, params);
            }
            out.push_back(make_cmd(LcfCode::ChangeSystemBGM, ctx.indent,
                arg_str(s.args,0,"bgm"), params));
        }
    });

    reg.register_cmd(CmdDef{ LcfCode::ChangeSystemGraphics, "ChangeSystemGraphics",
        [](const LcfCmd& c) -> std::string {
            return "ChangeSystemGraphics(system=" + quote(str(c.string)) + ", opacity=" + std::to_string(param(c,0)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::ChangeSystemGraphics, ctx.indent,
                arg_str(s.args,0,"system"), {arg_int(s.args,1,"opacity",0)}));
        }
    });

    // ── 入力数字 ─────────────────────────────────────────────────────────────
    reg.register_cmd(CmdDef{ LcfCode::InputNumber, "InputNumber",
        [](const LcfCmd& c) -> std::string {
            return "InputNumber(digits=" + std::to_string(param(c,0)) + ", var=" + std::to_string(param(c,1)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::InputNumber, ctx.indent,
                {arg_int(s.args,0,"digits",4), arg_int(s.args,1,"var",1)}));
        }
    });

    // ── ChangeSpriteAssociation (10630): str=sprite_name, params=[actor_id, sprite_index, ?]
    reg.register_cmd(CmdDef{ LcfCode::ChangeSpriteAssociation, "ChangeSpriteAssociation",
        [](const LcfCmd& c) -> std::string {
            return "ChangeSpriteAssociation(actor=" + std::to_string(param(c,0))
                 + ", sprite=" + quote(str(c.string))
                 + ", sprite_index=" + std::to_string(param(c,1))
                 + ", p2=" + std::to_string(param(c,2)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::ChangeSpriteAssociation, ctx.indent,
                arg_str(s.args,0,"sprite"),
                { arg_int(s.args,0,"actor",1),
                  arg_int(s.args,1,"sprite_index",0),
                  arg_int(s.args,2,"p2",0) }));
        }
    });

    // ── バトル関連 ────────────────────────────────────────────────────────────
    // VictoryHandler / EscapeHandler / EndBattle: パラメータなし
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
    reg.register_cmd(CmdDef{ LcfCode::EndBattle, "EndBattle",
        [](const LcfCmd&) -> std::string { return "EndBattle()"; },
        [](const CmdStmt&, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::EndBattle, ctx.indent));
        }
    });

    // ── ChangeSystemSFX (10670): str=sfx_name, params=[type,vol,tempo,balance]
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

    // ── ChangeScreenTransitions (10690): params=[type1, type2]
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

    // ── MovePicture (11120): params=[p0, p1, ..., p15]
    reg.register_cmd(CmdDef{ LcfCode::MovePicture, "MovePicture",
        [](const LcfCmd& c) -> std::string {
            // 全パラメータを pN= 形式で保存
            std::string r = "MovePicture(";
            for (size_t i = 0; i < c.parameters.size(); ++i) {
                if (i) r += ", ";
                r += "p" + std::to_string(i) + "=" + std::to_string(c.parameters[i]);
            }
            r += ")";
            return r;
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            std::vector<int32_t> params;
            for (size_t i = 0; ; ++i) {
                std::string key = "p" + std::to_string(i);
                bool found = false;
                for (const auto& arg : s.args)
                    if (arg.key == key) { params.push_back(arg.val.ival); found = true; break; }
                if (!found) break;
            }
            out.push_back(make_cmd(LcfCode::MovePicture, ctx.indent, params));
        }
    });

    // ── FlashSprite (11320): params=[event_id, r, g, b, chroma, duration, wait]
    reg.register_cmd(CmdDef{ LcfCode::FlashSprite, "FlashSprite",
        [](const LcfCmd& c) -> std::string {
            std::string who = (param(c,0) == 10001) ? "this" : std::to_string(param(c,0));
            return "FlashSprite(event=" + who
                 + ", r=" + std::to_string(param(c,1))
                 + ", g=" + std::to_string(param(c,2))
                 + ", b=" + std::to_string(param(c,3))
                 + ", chroma=" + std::to_string(param(c,4))
                 + ", duration=" + std::to_string(param(c,5))
                 + ", wait=" + std::to_string(param(c,6)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            std::string ev_s = arg_ident(s.args, 0, "event");
            int32_t ev_id = (ev_s == "this") ? 10001 : arg_int(s.args, 0, "event", 10001);
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

    // ── END ──────────────────────────────────────────────────────────────────
    reg.register_cmd(CmdDef{ LcfCode::END, "END",
        [](const LcfCmd&) -> std::string { return ""; },
        [](const CmdStmt&, EncodeCtx&, CmdVec&) {}
    });

    // ══════════════════════════════════════════════════════════════════════════
    // ゆめ2っき対応コマンド
    // ══════════════════════════════════════════════════════════════════════════

    // ── ChangeEventLocation (10860) ────────────────────────────────────────
    // params=[event_id, mode(0=direct,1=var), x, y]  (4個 or 5個)
    // mode=0: x,y は座標値直接
    // mode=1: x,y は変数ID
    reg.register_cmd(CmdDef{ LcfCode::ChangeEventLocation, "ChangeEventLocation",
        [](const LcfCmd& c) -> std::string {
            std::string r = "ChangeEventLocation(event=" + std::to_string(param(c,0))
                + ", mode=" + std::to_string(param(c,1))
                + ", x=" + std::to_string(param(c,2))
                + ", y=" + std::to_string(param(c,3));
            if (c.parameters.size() >= 5)
                r += ", p4=" + std::to_string(param(c,4));
            r += ")";
            return r;
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            std::vector<int32_t> params = {
                arg_int(s.args, 0, "event", 1),
                arg_int(s.args, 1, "mode",  0),
                arg_int(s.args, 2, "x",     0),
                arg_int(s.args, 3, "y",     0)
            };
            const Arg* p4 = find_arg(s.args, 4, "p4");
            if (p4) params.push_back(p4->val.ival);
            out.push_back(make_cmd(LcfCode::ChangeEventLocation, ctx.indent, params));
        }
    });

    // ── TileSubstitution (11750) ───────────────────────────────────────────
    // params=[chip_set_id, src_tile, dst_tile]
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

    // ── ChangePBG (11720) ──────────────────────────────────────────────────
    // str=panorama_name, params=[p0..p5] (通常すべて0)
    reg.register_cmd(CmdDef{ LcfCode::ChangePBG, "ChangePBG",
        [](const LcfCmd& c) -> std::string {
            std::string r = "ChangePBG(name=" + quote(str(c.string));
            for (size_t i = 0; i < c.parameters.size(); ++i)
                r += ", p" + std::to_string(i) + "=" + std::to_string(c.parameters[i]);
            r += ")";
            return r;
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            std::vector<int32_t> params;
            for (size_t i = 0; ; ++i) {
                std::string key = "p" + std::to_string(i);
                bool found = false;
                for (const auto& arg : s.args) {
                    if (arg.key == key) { params.push_back(arg.val.ival); found = true; break; }
                }
                if (!found) break;
            }
            out.push_back(make_cmd(LcfCode::ChangePBG, ctx.indent,
                arg_str(s.args, 0, "name"), params));
        }
    });

    // ── TradeEventLocations (10870) ────────────────────────────────────────
    // params=[event1, event2]
    reg.register_cmd(CmdDef{ LcfCode::TradeEventLocations, "TradeEventLocations",
        [](const LcfCmd& c) -> std::string {
            return "TradeEventLocations(event1=" + std::to_string(param(c,0))
                + ", event2=" + std::to_string(param(c,1)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::TradeEventLocations, ctx.indent,
                { arg_int(s.args, 0, "event1", 1),
                  arg_int(s.args, 1, "event2", 1) }));
        }
    });

    // ── ChangeActorFace (10640) ────────────────────────────────────────────
    // str=face_name, params=[actor_id, face_index]
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

    // ── SetVehicleLocation (10850) ─────────────────────────────────────────
    // params=[vehicle_type, mode(0=direct,1=var), map_id, x, y]
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

    // ── ChangeParameters (10430) ───────────────────────────────────────────
    // params=[who(0=actor,1=party), actor_id, param_id, op, src(0=const,1=var), val]
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

    // ── TeleportTargets (11810) ────────────────────────────────────────────
    // params=[action(0=add,1=remove), map_id, x, y, p4, p5]
    reg.register_cmd(CmdDef{ LcfCode::TeleportTargets, "TeleportTargets",
        [](const LcfCmd& c) -> std::string {
            return "TeleportTargets(action=" + std::to_string(param(c,0))
                + ", map=" + std::to_string(param(c,1))
                + ", x=" + std::to_string(param(c,2))
                + ", y=" + std::to_string(param(c,3))
                + ", p4=" + std::to_string(param(c,4))
                + ", p5=" + std::to_string(param(c,5)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::TeleportTargets, ctx.indent,
                { arg_int(s.args, 0, "action", 0),
                  arg_int(s.args, 1, "map",    1),
                  arg_int(s.args, 2, "x",      0),
                  arg_int(s.args, 3, "y",      0),
                  arg_int(s.args, 4, "p4",     0),
                  arg_int(s.args, 5, "p5",     1) }));
        }
    });

    // ── EscapeTarget (11830) ───────────────────────────────────────────────
    // params=[map_id, x, y, p3, p4]
    reg.register_cmd(CmdDef{ LcfCode::EscapeTarget, "EscapeTarget",
        [](const LcfCmd& c) -> std::string {
            return "EscapeTarget(map=" + std::to_string(param(c,0))
                + ", x=" + std::to_string(param(c,1))
                + ", y=" + std::to_string(param(c,2))
                + ", p3=" + std::to_string(param(c,3))
                + ", p4=" + std::to_string(param(c,4)) + ")";
        },
        [](const CmdStmt& s, EncodeCtx& ctx, CmdVec& out) {
            out.push_back(make_cmd(LcfCode::EscapeTarget, ctx.indent,
                { arg_int(s.args, 0, "map", 1),
                  arg_int(s.args, 1, "x",   0),
                  arg_int(s.args, 2, "y",   0),
                  arg_int(s.args, 3, "p3",  0),
                  arg_int(s.args, 4, "p4",  1) }));
        }
    });
}

} // namespace rm2k
