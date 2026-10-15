#pragma once
#include "ast.h"
#include "nametable.h"
#include <lcf/rpg/eventcommand.h>
#include <vector>
#include <string>
#include <functional>
#include <unordered_map>

namespace rm2k {

using LcfCmd  = lcf::rpg::EventCommand;
using LcfCode = lcf::rpg::EventCommand::Code;
using CmdVec  = std::vector<LcfCmd>;

// コマンド変換インターフェース

struct EncodeCtx {
    int32_t indent = 0;
    const NameTable* names = nullptr;  // 変数/スイッチ名解決用（nullptr = 使用しない）
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

// コマンドレジストリ

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

// ヘルパー: LCFコマンドビルダー

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

// パラメータ列の末尾の連続する 0 を省略するヘルパー。
// EasyRPG は CmdSetup<..., N> で実行時に resize(N, 0) するため、
// バイナリ保存時に末尾0は不要。元のファイルと同じ最小限のパラメータ数を保つために使う。
// min_size: この個数を下回らないように末尾0を保持する（デフォルト=1）
inline std::vector<int32_t> trim_trailing_zeros(std::vector<int32_t> v,
                                                  size_t min_size = 1) {
    while (v.size() > min_size && v.back() == 0)
        v.pop_back();
    return v;
}

// 引数アクセスヘルパー

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

// キャラクター/イベント指定 (RPG_RT / EasyRPG Editor の定数)
//   10001=主人公, 10002=ボート, 10003=船, 10004=飛行船, 10005=このイベント, それ以外=イベントID

constexpr int32_t kCharPlayer    = 10001;
constexpr int32_t kCharBoat      = 10002;
constexpr int32_t kCharShip      = 10003;
constexpr int32_t kCharAirship   = 10004;
constexpr int32_t kCharThisEvent = 10005;

// 予約IDなら player/boat/ship/airship/this、そうでなければ空文字列
std::string char_ref_name(int32_t id);

// 引数 (整数 または player/boat/ship/airship/this) → ID。解釈できなければ std::runtime_error
int32_t resolve_char_ref(const Arg& a, const char* what);

} // namespace rm2k
