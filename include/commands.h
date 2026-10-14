#pragma once
#include "ast.h"
#include <lcf/rpg/eventcommand.h>
#include <vector>
#include <string>
#include <functional>
#include <unordered_map>

namespace rm2k {

using LcfCmd  = lcf::rpg::EventCommand;
using LcfCode = lcf::rpg::EventCommand::Code;
using CmdVec  = std::vector<LcfCmd>;

// ──────────────────────────────────────────────────────────────────────────────
// コマンド変換インターフェース
// ──────────────────────────────────────────────────────────────────────────────

struct EncodeCtx {
    int32_t indent = 0;
};

// LCFコマンド列 → テキスト行
using DecodeFn = std::function<std::string(const LcfCmd&)>;

// テキストAST → LCFコマンド列
using EncodeFn = std::function<void(const CmdStmt&, EncodeCtx&, CmdVec&)>;

struct CmdDef {
    LcfCode    code;
    std::string name;       // テキスト上のコマンド名
    DecodeFn   decode;
    EncodeFn   encode;
};

// ──────────────────────────────────────────────────────────────────────────────
// コマンドレジストリ
// ──────────────────────────────────────────────────────────────────────────────

class CommandRegistry {
public:
    static CommandRegistry& instance();

    void register_cmd(CmdDef def, bool alias_only = false);

    // コマンド名 → 定義
    const CmdDef* find_by_name(const std::string& name) const;

    // LCFコード → 定義
    const CmdDef* find_by_code(LcfCode code) const;

    // すべての登録コマンド
    const std::vector<CmdDef>& all() const { return defs_; }

private:
    std::vector<CmdDef> defs_;
    std::unordered_map<std::string, size_t> by_name_;
    std::unordered_map<int32_t,     size_t> by_code_;
};

// 登録エントリポイント (commands.cpp から呼ぶ)
void register_all_commands();

// ──────────────────────────────────────────────────────────────────────────────
// ヘルパー: LCFコマンドビルダー
// ──────────────────────────────────────────────────────────────────────────────

inline LcfCmd make_cmd(LcfCode code, int32_t indent,
                       std::string str,
                       std::vector<int32_t> params) {
    LcfCmd c;
    c.code       = static_cast<int32_t>(code);
    c.indent     = indent;
    c.string     = lcf::DBString(str);
    c.parameters = lcf::DBArray<int32_t>(params.begin(), params.end());
    return c;
}

inline LcfCmd make_cmd(LcfCode code, int32_t indent,
                       std::vector<int32_t> params = {}) {
    return make_cmd(code, indent, "", params);
}

// ──────────────────────────────────────────────────────────────────────────────
// 引数アクセスヘルパー
// ──────────────────────────────────────────────────────────────────────────────

// Arg を検索
const Arg* find_arg(const std::vector<Arg>& args, size_t pos, const std::string& key = "");

// Arg から整数を取得
int32_t arg_int(const std::vector<Arg>& args, size_t pos, const std::string& key = "", int32_t def = 0);

// Arg から文字列を取得
std::string arg_str(const std::vector<Arg>& args, size_t pos, const std::string& key = "", std::string def = "");

// Arg から識別子を取得
std::string arg_ident(const std::vector<Arg>& args, size_t pos, const std::string& key = "", std::string def = "");

// LCF params から安全に取得
inline int32_t param(const LcfCmd& c, size_t idx, int32_t def = 0) {
    if (idx < c.parameters.size()) return c.parameters[idx];
    return def;
}

} // namespace rm2k
