#pragma once
#include <string>
#include <vector>
#include <memory>
#include <variant>
#include <cstdint>

namespace rm2k {

// ─── ソース位置 ──────────────────────────────────────────────────────────────

struct SourceLoc {
    int line = 0;
    int col  = 0;
};

// ─── トークン ────────────────────────────────────────────────────────────────

enum class TokenKind {
    Ident, Int, String,
    LParen, RParen, LBrace, RBrace, LBracket, RBracket,
    Comma, Semi, Dot,
    Equals, EqEq, Neq,
    Lt, LtEq, Gt, GtEq,                          // < <= > >=
    PlusEq, MinusEq, StarEq, SlashEq, PercentEq,
    Plus, Minus,        // 単項/二項演算子
    At, Hash,
    Kw_if, Kw_else,
    Eof,
};

struct Token {
    TokenKind   kind;
    std::string text;
    int32_t     ival = 0;
    SourceLoc   loc;
};

// ─── 引数値 ──────────────────────────────────────────────────────────────────

struct ArgVal {
    enum Kind { Int, Str, Ident, Indexed, List } kind = Int;
    int32_t     ival = 0;
    std::string sval;
    // Indexed: sval=識別子, ival=インデックス  例: var[3]
    // Indexed で文字列ラベル指定の場合: sidx に名前を格納し ival=0
    // 例: switch["マップ移動中"] → sval="switch", sidx="マップ移動中", ival=0
    std::string sidx; // 空の場合は ival を使用
    // List: 識別子/整数の並び  例: [face_left, speed_down, up]
    std::vector<std::string> list; // 各要素は識別子名または数値文字列
};

struct Arg {
    std::string key;   // "" = 位置引数
    ArgVal      val;
};

// ─── AST ノード ──────────────────────────────────────────────────────────────

struct StmtBlock;

// コマンド文: name(args...) { body }
// body は Loop() { ... } などでブロックを持つ場合に使用
struct CmdStmt {
    std::string                    name;
    std::vector<Arg>               args;
    std::unique_ptr<StmtBlock>     body;   // nullptr if no body
    SourceLoc                      loc;
};

// if (...) { } else { }
struct IfStmt {
    std::vector<Arg>               cond_args;
    std::unique_ptr<StmtBlock>     then_body;
    std::unique_ptr<StmtBlock>     else_body;       // nullptr if no else
    std::vector<int32_t>           end_branch_params; // EndBranch(p0=N) の params
    SourceLoc                      loc;
};

// choice("A","B") { when("A"){} when("B"){} }
struct WhenClause {
    std::string                    label;      // "" = cancel
    int                            index = -1; // ShowChoiceOption params[0] (選択肢インデックス)
    std::unique_ptr<StmtBlock>     body;
};

struct ChoiceStmt {
    std::vector<std::string>       options;
    int                            cancel = 0; // cancel= 引数
    std::vector<WhenClause>        whens;
    SourceLoc                      loc;
};

// switch[N] = ON/OFF/toggle  (直接代入文)
struct SwitchAssign {
    int32_t     id      = 0;
    std::string id_label;        // 名前テーブル使用時の名前 (空なら id を使用)
    bool        is_range = false;
    int32_t     id2     = 0;    // range end
    std::string id2_label;
    bool        is_var   = false; // switch[var[N]]
    std::string value;            // ON / OFF / toggle
    SourceLoc   loc;
};

// var[N] op= value  (直接代入文)
struct VarAssign {
    int32_t     id   = 0;
    std::string id_label;        // 名前テーブル使用時の名前 (空なら id を使用)
    bool        is_range = false;
    int32_t     id2  = 0;
    std::string id2_label;
    std::string op;   // = += -= *= /= %=
    ArgVal      rhs;
    SourceLoc   loc;
};

using StmtPtr = std::variant<CmdStmt, IfStmt, ChoiceStmt, SwitchAssign, VarAssign>;

struct StmtBlock {
    std::vector<StmtPtr> stmts;
};

// ─── ページメタデータ ─────────────────────────────────────────────────────────

struct PageMeta {
    int page_id = 1;

    std::string trigger;
    std::string layer;

    std::string sprite;
    int sprite_index = -1;
    std::string dir;

    std::string move_type;
    std::string move_speed;
    int move_freq = -1;

    std::string anim_type;

    int translucent       = -1;
    int overlap_forbidden = -1;
    int has_end           = -1;  // -1=未指定(デフォルト=true), 0=なし, 1=あり

    int cond_switch_a = 0;
    int cond_switch_b = 0;
    int cond_var      = 0;
    std::string cond_var_op;
    int cond_var_val  = 0;
    int cond_item     = 0;
    int cond_actor    = 0;
    int cond_timer    = -1;
    int cond_timer2   = -1;
    int cond_switch   = 0;   // コモンイベント用

    std::vector<std::pair<std::string, std::string>> extra;
};

struct EventMeta {
    int x = 0;
    int y = 0;
};

// ─── ドキュメント ─────────────────────────────────────────────────────────────

struct MapDoc {
    struct PageEntry {
        int         event_id = 0;
        int         page_id  = 1;
        EventMeta   event_meta;
        PageMeta    meta;
        StmtBlock   body;
    };
    std::vector<PageEntry> pages;
};

} // namespace rm2k
