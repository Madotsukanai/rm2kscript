#include "parser.h"
#include <cctype>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <algorithm>

namespace rm2k {

// ──────────────────────────────────────────────────────────────────────────────
// Lexer
// ──────────────────────────────────────────────────────────────────────────────

Lexer::Lexer(std::string_view src) : src_(src) {}

// <cctype> の isdigit/isalpha は char の負値 (UTF-8 の上位バイト) で UB になるため、ASCII 限定の自前実装を使う
static inline bool is_digit_c(char c) { return c >= '0' && c <= '9'; }
static inline bool is_alpha_c(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static inline bool is_alnum_c(char c) { return is_alpha_c(c) || is_digit_c(c); }

char Lexer::peek(int offset) const {
    size_t idx = pos_ + offset;
    return (idx < src_.size()) ? src_[idx] : '\0';
}

char Lexer::advance() {
    char c = src_[pos_++];
    if (c == '\n') { ++line_; col_ = 1; }
    else           { ++col_; }
    return c;
}

void Lexer::skip_ws_and_comments() {
    while (pos_ < src_.size()) {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { advance(); }
        else if (c == '/' && peek(1) == '/') {
            while (pos_ < src_.size() && peek() != '\n') advance();
        } else break;
    }
}

Token Lexer::read_string() {
    SourceLoc loc = here();
    advance(); // "
    std::string s;
    bool closed = false;
    while (pos_ < src_.size()) {
        char c = peek();
        if (c == '"') { advance(); closed = true; break; }
        if (c == '\\') {
            advance();
            if (pos_ >= src_.size()) break;   // 末尾の '\' (範囲外読み出し防止)
            char e = advance();
            switch (e) {
                case 'n':  s += '\n'; break;
                case 't':  s += '\t'; break;
                case 'r':  s += '\r'; break;
                case '\\': s += '\\'; break;
                case '"':  s += '"';  break;
                default:   s += '\\'; s += e; break;
            }
        } else { s += advance(); }
    }
    if (!closed) throw LexError("unterminated string literal", loc);
    Token t; t.kind = TokenKind::String; t.text = s; t.ival = 0; t.loc = loc;
    return t;
}

Token Lexer::read_number(bool negative) {
    SourceLoc loc = here();
    std::string s;
    if (negative) { advance(); s += '-'; } // consume '-'
    while (pos_ < src_.size() && is_digit_c(peek())) s += advance();
    int32_t v = 0;
    // std::stoi は int 幅なので INT_MIN/INT_MAX での動作が処理系依存。
    // std::stoll で long long に読んでから int32_t 範囲を明示チェックする。
    try {
        long long ll = std::stoll(s);
        if (ll < static_cast<long long>(std::numeric_limits<int32_t>::min()) ||
            ll > static_cast<long long>(std::numeric_limits<int32_t>::max()))
            throw std::out_of_range("out of int32 range");
        v = static_cast<int32_t>(ll);
    }
    catch (const std::exception&) { throw LexError("integer literal out of range: " + s, loc); }
    Token t; t.kind = TokenKind::Int; t.text = s; t.ival = v; t.loc = loc;
    return t;
}

static const std::unordered_map<std::string, TokenKind> kKeywords = {
    {"if",    TokenKind::Kw_if},
    {"else",  TokenKind::Kw_else},
    {"while", TokenKind::Kw_while},
    {"for",   TokenKind::Kw_for},
    {"loop",  TokenKind::Kw_loop},
    {"break", TokenKind::Kw_break},
    {"goto",  TokenKind::Kw_goto},
};

Token Lexer::read_ident_or_kw() {
    SourceLoc loc = here();
    std::string s;
    while (pos_ < src_.size()) {
        char c = peek();
        if (is_alnum_c(c) || c == '_') s += advance();
        else break;
    }
    Token t;
    auto it = kKeywords.find(s);
    t.kind = (it != kKeywords.end()) ? it->second : TokenKind::Ident;
    t.text = s; t.ival = 0; t.loc = loc;
    return t;
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> out;
    // UTF-8 BOM を読み飛ばす (Windows のエディタが付けることがある)
    if (src_.size() >= 3
        && static_cast<unsigned char>(src_[0]) == 0xEF
        && static_cast<unsigned char>(src_[1]) == 0xBB
        && static_cast<unsigned char>(src_[2]) == 0xBF)
        pos_ = 3;
    while (true) {
        skip_ws_and_comments();
        if (pos_ >= src_.size()) break;

        SourceLoc loc = here();
        char c = peek();

        auto sym = [&](TokenKind k, std::string text = "") {
            if (text.empty()) text = std::string(1, c);
            Token t; t.kind = k; t.text = text; t.ival = 0; t.loc = loc;
            out.push_back(t);
        };
        auto sym2 = [&](TokenKind k, std::string text) {
            advance(); advance();
            Token t; t.kind = k; t.text = text; t.ival = 0; t.loc = loc;
            out.push_back(t);
        };

        if (c == '"') { out.push_back(read_string()); }
        else if (is_digit_c(c)) { out.push_back(read_number(false)); }
        else if (c == '-' && is_digit_c(peek(1))) { out.push_back(read_number(true)); }
        else if (is_alpha_c(c) || c == '_') { out.push_back(read_ident_or_kw()); }
        else if (c == '(') { advance(); sym(TokenKind::LParen); }
        else if (c == ')') { advance(); sym(TokenKind::RParen); }
        else if (c == '{') { advance(); sym(TokenKind::LBrace); }
        else if (c == '}') { advance(); sym(TokenKind::RBrace); }
        else if (c == '[') { advance(); sym(TokenKind::LBracket); }
        else if (c == ']') { advance(); sym(TokenKind::RBracket); }
        else if (c == ',') { advance(); sym(TokenKind::Comma); }
        else if (c == ';') { advance(); sym(TokenKind::Semi); }
        else if (c == ':') { advance(); sym(TokenKind::Colon, ":"); }
        else if (c == '.') { advance(); sym(TokenKind::Dot); }
        else if (c == '@') { advance(); sym(TokenKind::At); }
        else if (c == '#') { advance(); sym(TokenKind::Hash); }
        else if (c == '+' && peek(1) == '=') { sym2(TokenKind::PlusEq,    "+="); }
        else if (c == '-' && peek(1) == '=') { sym2(TokenKind::MinusEq,   "-="); }
        else if (c == '*' && peek(1) == '=') { sym2(TokenKind::StarEq,    "*="); }
        else if (c == '/' && peek(1) == '=') { sym2(TokenKind::SlashEq,   "/="); }
        else if (c == '%' && peek(1) == '=') { sym2(TokenKind::PercentEq, "%="); }
        else if (c == '=' && peek(1) == '=') { sym2(TokenKind::EqEq,      "=="); }
        else if (c == '!' && peek(1) == '=') { sym2(TokenKind::Neq,       "!="); }
        else if (c == '<' && peek(1) == '=') { sym2(TokenKind::LtEq,      "<="); }
        else if (c == '>' && peek(1) == '=') { sym2(TokenKind::GtEq,      ">="); }
        else if (c == '<') { advance(); sym(TokenKind::Lt,     "<"); }
        else if (c == '>') { advance(); sym(TokenKind::Gt,     ">"); }
        else if (c == '=') { advance(); sym(TokenKind::Equals, "="); }
        else if (c == '+') { advance(); sym(TokenKind::Plus,   "+"); }
        else if (c == '-') { advance(); sym(TokenKind::Minus,  "-"); }
        else {
            if (static_cast<unsigned char>(c) >= 0x80)
                throw LexError("unexpected non-ASCII character outside of a string/comment "
                               "(full-width space or Japanese text must be inside \"...\" or after //)", loc);
            throw LexError(std::string("unexpected character: '") + c + "'", loc);
        }
    }
    Token eof; eof.kind = TokenKind::Eof; eof.text = ""; eof.ival = 0; eof.loc = here();
    out.push_back(eof);
    return out;
}

// ──────────────────────────────────────────────────────────────────────────────
// Parser helpers
// ──────────────────────────────────────────────────────────────────────────────

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

Token& Parser::peek(int offset) {
    size_t idx = pos_ + offset;
    if (idx >= tokens_.size()) return tokens_.back();
    return tokens_[idx];
}

Token Parser::consume() {
    if (pos_ < tokens_.size()) return tokens_[pos_++];
    return tokens_.back();
}

Token Parser::expect(TokenKind k, const char* what) {
    if (!check(k))
        throw ParseError(std::string("expected ") + what + ", got '" + peek().text + "'", peek().loc);
    return consume();
}

bool Parser::check(TokenKind k) const {
    if (pos_ >= tokens_.size()) return k == TokenKind::Eof;
    return tokens_[pos_].kind == k;
}

bool Parser::at_end() const { return check(TokenKind::Eof); }

// ──────────────────────────────────────────────────────────────────────────────
// op= / = 判定
// ──────────────────────────────────────────────────────────────────────────────

static std::string compound_op(TokenKind k) {
    switch (k) {
        case TokenKind::Equals:    return "=";
        case TokenKind::PlusEq:    return "+=";
        case TokenKind::MinusEq:   return "-=";
        case TokenKind::StarEq:    return "*=";
        case TokenKind::SlashEq:   return "/=";
        case TokenKind::PercentEq: return "%=";
        default: return "";
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// メタデータ キー=値 適用
// ──────────────────────────────────────────────────────────────────────────────

void Parser::apply_meta_kv(PageMeta& m, const std::string& key,
                            const std::string& val_s, int val_i, SourceLoc loc,
                            bool val_is_int) {
    // 整数を取るキーに整数以外 (識別子・文字列) を渡したとき、黙って 0 にならないようエラーにする
    static const char* const kIntKeys[] = {
        "sprite_index", "move_freq", "translucent", "overlap_forbidden",
        "cond_switch_a", "cond_switch_b", "cond_var", "cond_var_val", "cond_item",
        "cond_actor", "cond_timer", "cond_timer2", "cond_switch", "has_end",
    };
    if (!val_is_int) {
        for (const char* ik : kIntKeys)
            if (key == ik)
                throw ParseError("option '" + key + "' requires an integer value", loc);
    }
    if      (key == "trigger")          m.trigger = val_s;
    else if (key == "layer")             m.layer   = val_s;
    else if (key == "sprite")          { m.sprite = val_s; m.has_sprite = true; }
    else if (key == "sprite_index")      m.sprite_index = val_i;
    else if (key == "dir")               m.dir     = val_s;
    else if (key == "move_type")         m.move_type  = val_s;
    else if (key == "move_speed")        m.move_speed = val_s;
    else if (key == "move_freq")         m.move_freq  = val_i;
    else if (key == "anim_type")         m.anim_type  = val_s;
    else if (key == "translucent")       m.translucent = val_i;
    else if (key == "overlap_forbidden") m.overlap_forbidden = val_i;
    else if (key == "cond_switch_a")     m.cond_switch_a = val_i;
    else if (key == "cond_switch_b")     m.cond_switch_b = val_i;
    else if (key == "cond_var")          m.cond_var = val_i;
    else if (key == "cond_var_op")       m.cond_var_op = val_s;
    else if (key == "cond_var_val")      m.cond_var_val = val_i;
    else if (key == "cond_item")         m.cond_item = val_i;
    else if (key == "cond_actor")        m.cond_actor = val_i;
    else if (key == "cond_timer")        m.cond_timer = val_i;
    else if (key == "cond_timer2")       m.cond_timer2 = val_i;
    else if (key == "cond_switch")       m.cond_switch = val_i;
    else if (key == "has_end")           m.has_end = val_i;
    else { throw ParseError("unknown page option '" + key + "'", loc); }
}

// ──────────────────────────────────────────────────────────────────────────────
// トップレベル parse()
// ──────────────────────────────────────────────────────────────────────────────

MapDoc Parser::parse() {
    MapDoc doc;
    while (!at_end()) {
        // 新形式: event(...) { ... }
        if (check(TokenKind::Ident) && peek().text == "event") {
            // event(N, ...) { page(...){} ... }  を展開して複数 PageEntry に
            consume(); // event
            expect(TokenKind::LParen, "(");
            int event_id = expect(TokenKind::Int, "event id").ival;
            EventMeta em;
            // event(N, x=.., y=.., name="...") のオプション引数
            while (check(TokenKind::Comma)) {
                consume();
                if (!check(TokenKind::Ident)) break;
                Token k = consume();
                expect(TokenKind::Equals, "=");
                if (k.text == "x") em.x = expect(TokenKind::Int, "x").ival;
                else if (k.text == "y") em.y = expect(TokenKind::Int, "y").ival;
                else if (k.text == "name") { em.name = expect(TokenKind::String, "event name").text; em.has_name = true; }
                else throw ParseError("unknown event option '" + k.text + "'", k.loc);
            }
            expect(TokenKind::RParen, ")");
            expect(TokenKind::LBrace, "{");

            // { } 内に page(...) { } が並ぶ
            while (!check(TokenKind::RBrace) && !at_end()) {
                if (check(TokenKind::Ident) && peek().text == "page") {
                    SourceLoc page_loc = peek().loc;
                    consume(); // page
                    expect(TokenKind::LParen, "(");
                    int page_id = expect(TokenKind::Int, "page id").ival;
                    PageMeta pm;
                    pm.page_id = page_id;
                    while (check(TokenKind::Comma)) {
                        consume();
                        if (!check(TokenKind::Ident)) break;
                        Token k = consume();
                        expect(TokenKind::Equals, "=");
                        std::string val_s; int val_i = 0; bool val_is_int = false;
                        if (check(TokenKind::String)) { val_s = consume().text; }
                        else if (check(TokenKind::Int)) { Token t = consume(); val_i = t.ival; val_s = t.text; val_is_int = true; }
                        else if (check(TokenKind::Ident)) { val_s = consume().text; }
                        else throw ParseError("expected a value after '=' for option '" + k.text + "'", peek().loc);
                        apply_meta_kv(pm, k.text, val_s, val_i, k.loc, val_is_int);
                    }
                    expect(TokenKind::RParen, ")");

                    MapDoc::PageEntry entry;
                    entry.event_id  = event_id;
                    entry.page_id   = page_id;
                    entry.loc       = page_loc;
                    entry.event_meta = em;
                    entry.meta      = pm;
                    entry.body      = parse_block();
                    doc.pages.push_back(std::move(entry));
                } else {
                    throw ParseError("expected page(...) inside event, got '" + peek().text + "'", peek().loc);
                }
            }
            expect(TokenKind::RBrace, "}");

        } else if (check(TokenKind::At)) {
            // 旧形式: @event N @page M { }
            doc.pages.push_back(parse_page_entry_legacy());
        } else {
            throw ParseError("expected event(...) or @event", peek().loc);
        }
    }
    return doc;
}

// ──────────────────────────────────────────────────────────────────────────────
// 旧形式: @event N @page M { }
// ──────────────────────────────────────────────────────────────────────────────

MapDoc::PageEntry Parser::parse_page_entry_legacy() {
    MapDoc::PageEntry entry;
    entry.event_id = 0;
    entry.page_id  = 1;

    if (check(TokenKind::At) && peek(1).text == "event") {
        consume(); consume();
        entry.event_id = expect(TokenKind::Int, "event id").ival;
        while (!check(TokenKind::At) && !at_end()) {
            if (check(TokenKind::Ident) && peek(1).kind == TokenKind::Equals) {
                Token key = consume(); consume();
                Token val = consume();
                if (key.text == "x") entry.event_meta.x = val.ival;
                else if (key.text == "y") entry.event_meta.y = val.ival;
            } else break;
        }
    }

    expect(TokenKind::At, "@");
    expect(TokenKind::Ident, "page");
    entry.page_id = expect(TokenKind::Int, "page id").ival;
    entry.meta.page_id = entry.page_id;

    while (!check(TokenKind::LBrace) && !at_end()) {
        if (!check(TokenKind::Ident)) break;
        Token key = consume();
        expect(TokenKind::Equals, "=");
        std::string val_s; int val_i = 0; bool val_is_int = false;
        if (check(TokenKind::String))      { val_s = consume().text; }
        else if (check(TokenKind::Int))    { Token t = consume(); val_i = t.ival; val_s = t.text; val_is_int = true; }
        else if (check(TokenKind::Ident))  { val_s = consume().text; }
        else break;
        apply_meta_kv(entry.meta, key.text, val_s, val_i, key.loc, val_is_int);
    }

    entry.body = parse_block();
    return entry;
}

// ──────────────────────────────────────────────────────────────────────────────
// ブロック・文
// ──────────────────────────────────────────────────────────────────────────────

// EndBranch(p0=N,...) がブロック終端として来た場合に消費して params を返す (空なら通常終端)
std::vector<int32_t> Parser::consume_end_branch_if_present() {
    if (!check(TokenKind::Ident) || peek(0).text != "EndBranch") return {};
    if (peek(1).kind != TokenKind::LParen) return {};
    consume(); // EndBranch
    consume(); // (
    std::vector<int32_t> params;
    while (!check(TokenKind::RParen) && !at_end()) {
        if (check(TokenKind::Comma)) { consume(); continue; }
        if (check(TokenKind::Ident)) consume(); // pN key
        if (check(TokenKind::Equals)) consume();
        if (check(TokenKind::Int)) { params.push_back(peek(0).ival); consume(); }
        else if (!check(TokenKind::RParen)) consume(); // skip unknown
    }
    if (check(TokenKind::RParen)) consume();
    if (check(TokenKind::Semi))   consume();
    return params;
}

StmtBlock Parser::parse_block() {
    expect(TokenKind::LBrace, "{");
    StmtBlock block;
    // EndBranch(p0=N) も終端として受け付ける
    while (!check(TokenKind::RBrace)
           && !(check(TokenKind::Ident) && peek(0).text == "EndBranch"
                && peek(1).kind == TokenKind::LParen)
           && !at_end())
        block.stmts.push_back(parse_stmt());
    if (check(TokenKind::RBrace)) {
        consume();
    } else if (at_end()) {
        throw ParseError("unexpected end of file: missing '}'", peek().loc);
    }
    // EndBranch(p0=N) は parse_if で消費するのでここでは触れない
    return block;
}

StmtPtr Parser::parse_stmt() {
    if (check(TokenKind::Kw_if))    return parse_if();
    if (check(TokenKind::Kw_while)) return parse_while();
    if (check(TokenKind::Kw_for))   return parse_for();
    if (check(TokenKind::Kw_loop))  return parse_loop();
    if (check(TokenKind::Kw_break)) {
        BreakStmt s; s.loc = peek().loc;
        consume(); // break
        expect(TokenKind::Semi, ";");
        return s;
    }
    if (check(TokenKind::Kw_goto)) {
        GotoStmt s; s.loc = peek().loc;
        consume(); // goto
        s.label_id = expect(TokenKind::Int, "label id").ival;
        expect(TokenKind::Semi, ";");
        return s;
    }
    // LN:  (ラベル定義: 整数リテラルのあとにコロン)
    if (check(TokenKind::Int) && peek(1).kind == TokenKind::Colon) {
        LabelStmt s; s.loc = peek().loc;
        s.label_id = consume().ival; // N
        consume(); // :
        return s;
    }

    // switch[N] = ON/OFF/toggle  (直接代入)
    // switch["name"] = ON/OFF/toggle  (名前テーブル使用)
    // switch[var[N]] = ON/OFF/toggle  (変数インデックス)
    if (check(TokenKind::Ident) && peek().text == "switch"
        && peek(1).kind == TokenKind::LBracket) {
        SwitchAssign sa;
        sa.loc = peek().loc;
        consume(); // switch
        consume(); // [
        // switch[var[N]] 形式の判定
        if (check(TokenKind::Ident) && peek().text == "var"
            && peek(1).kind == TokenKind::LBracket) {
            consume(); // var
            consume(); // [
            sa.id = expect(TokenKind::Int, "var id").ival;
            expect(TokenKind::RBracket, "]");
            sa.is_var = true;
        } else if (check(TokenKind::String)) {
            // switch["名前"] 形式 (または範囲 switch["a".."b"])
            sa.id_label = consume().text;
            if (check(TokenKind::Dot) && peek(1).kind == TokenKind::Dot) {
                consume(); consume();
                if (check(TokenKind::String)) {
                    sa.id2_label = consume().text;
                } else {
                    sa.id2 = expect(TokenKind::Int, "switch id2").ival;
                }
                sa.is_range = true;
            }
        } else {
            sa.id = expect(TokenKind::Int, "switch id").ival;
            // 範囲: [N..M] または ["name".."name"]
            if (check(TokenKind::Dot) && peek(1).kind == TokenKind::Dot) {
                consume(); consume();
                if (check(TokenKind::String)) {
                    sa.id2_label = consume().text;
                } else {
                    sa.id2 = expect(TokenKind::Int, "switch id2").ival;
                }
                sa.is_range = true;
            }
        }
        expect(TokenKind::RBracket, "]");
        expect(TokenKind::Equals, "=");
        sa.value = expect(TokenKind::Ident, "ON/OFF/toggle").text;
        if (check(TokenKind::Semi)) consume();
        return sa;
    }

    // var[N] op= value  (直接代入)
    // var["name"] op= value  (名前テーブル使用)
    if (check(TokenKind::Ident) && peek().text == "var"
        && peek(1).kind == TokenKind::LBracket) {
        VarAssign va;
        va.loc = peek().loc;
        consume(); // var
        consume(); // [
        if (check(TokenKind::String)) {
            // var["名前"] 形式 (範囲 var["a".."b"] / var["a"..N] も可)
            va.id_label = consume().text;
            if (check(TokenKind::Dot) && peek(1).kind == TokenKind::Dot) {
                consume(); consume();
                if (check(TokenKind::String)) {
                    va.id2_label = consume().text;
                } else {
                    va.id2 = expect(TokenKind::Int, "var id2").ival;
                }
                va.is_range = true;
            }
        } else {
            va.id = expect(TokenKind::Int, "var id").ival;
            if (check(TokenKind::Dot) && peek(1).kind == TokenKind::Dot) {
                consume(); consume();
                if (check(TokenKind::String)) {
                    va.id2_label = consume().text;
                } else {
                    va.id2 = expect(TokenKind::Int, "var id2").ival;
                }
                va.is_range = true;
            }
        }
        expect(TokenKind::RBracket, "]");
        va.op = compound_op(peek().kind);
        if (va.op.empty())
            throw ParseError("expected = or op= after var[N]", peek().loc);
        consume(); // op
        va.rhs = parse_arg_val();
        if (check(TokenKind::Semi)) consume();
        return va;
    }

    Token name = expect(TokenKind::Ident, "command name");

    // choice("A","B") { when(...){} ... }
    if (name.text == "choice" && check(TokenKind::LParen))
        return parse_choice(name);

    return parse_cmd(name);
}

IfStmt Parser::parse_if() {
    IfStmt s;
    s.loc = peek().loc;
    consume(); // if
    expect(TokenKind::LParen, "(");
    s.cond_args = parse_cond_args();
    expect(TokenKind::RParen, ")");
    s.then_body = std::make_unique<StmtBlock>(parse_block());
    if (check(TokenKind::Kw_else)) {
        consume();
        s.else_body = std::make_unique<StmtBlock>(parse_block());
    }
    // EndBranch(p0=N,...) が then/else ブロックの終端として来た場合に params を回収
    s.end_branch_params = consume_end_branch_if_present();
    return s;
}

WhileStmt Parser::parse_while() {
    WhileStmt s;
    s.loc = peek().loc;
    consume(); // while
    expect(TokenKind::LParen, "(");
    s.cond_args = parse_cond_args();
    expect(TokenKind::RParen, ")");
    s.body = std::make_unique<StmtBlock>(parse_block());
    return s;
}

LoopStmt Parser::parse_loop() {
    LoopStmt s;
    s.loc = peek().loc;
    consume(); // loop
    s.body = std::make_unique<StmtBlock>(parse_block());
    return s;
}

// for (var[N] = init; cond; var[N] op= step) { body }
// または for (var["name"] = init; cond; var["name"] op= step) { body }
ForStmt Parser::parse_for() {
    ForStmt s;
    s.loc = peek().loc;
    consume(); // for
    expect(TokenKind::LParen, "(");

    // init: var[N] = val または var["name"] = val
    if (!(check(TokenKind::Ident) && peek().text == "var"
          && peek(1).kind == TokenKind::LBracket))
        throw ParseError("for: expected var[N] in init", peek().loc);
    consume(); // var
    consume(); // [
    if (check(TokenKind::String)) {
        s.var_label = consume().text;
    } else {
        s.var_id = expect(TokenKind::Int, "var id").ival;
    }
    expect(TokenKind::RBracket, "]");
    if (!check(TokenKind::Equals))
        throw ParseError("for: expected = after var[N]", peek().loc);
    consume(); // =
    s.init_val = parse_arg_val();
    expect(TokenKind::Semi, ";");

    // cond
    s.cond_args = parse_cond_args();
    expect(TokenKind::Semi, ";");

    // step: var[N] op= val （同じ変数）
    if (!(check(TokenKind::Ident) && peek().text == "var"
          && peek(1).kind == TokenKind::LBracket))
        throw ParseError("for: expected var[N] in step", peek().loc);
    consume(); // var
    consume(); // [
    // init と step が同じ変数を指すか検証。
    // init が名前指定 (var_label 非空) なら step も同じ名前でなければならない。
    // init が数値指定 (var_label 空)   なら step も同じ数値でなければならない。
    // 名前と数値の混在はエラー。
    if (check(TokenKind::String)) {
        Token step_tok = consume();
        if (!s.var_label.empty()) {
            if (step_tok.text != s.var_label)
                throw ParseError("for: step must use the same variable as init", step_tok.loc);
        } else {
            // init が数値なのに step が名前 → 混在
            throw ParseError("for: step must use the same variable as init (init used a number, step uses a name)", step_tok.loc);
        }
    } else {
        Token step_id = expect(TokenKind::Int, "var id");
        if (!s.var_label.empty()) {
            // init が名前なのに step が数値 → 混在
            throw ParseError("for: step must use the same variable as init (init used a name, step uses a number)", step_id.loc);
        }
        if (step_id.ival != s.var_id)
            throw ParseError("for: step must use the same variable as init", step_id.loc);
    }
    expect(TokenKind::RBracket, "]");
    // op=
    s.step_op = compound_op(peek().kind);
    if (s.step_op.empty())
        throw ParseError("for: expected op= in step", peek().loc);
    consume();
    s.step_val = parse_arg_val();
    expect(TokenKind::RParen, ")");

    s.body = std::make_unique<StmtBlock>(parse_block());
    return s;
}

// choice("A", "B", cancel=2) { when("A"){...} when("B"){...} }
ChoiceStmt Parser::parse_choice(Token name_tok) {
    ChoiceStmt cs;
    cs.loc = name_tok.loc;
    consume(); // (
    while (!check(TokenKind::RParen) && !at_end()) {
        if (check(TokenKind::Comma)) { consume(); continue; }
        if (check(TokenKind::Ident) && peek().text == "cancel"
            && peek(1).kind == TokenKind::Equals) {
            consume(); consume();
            cs.cancel = expect(TokenKind::Int, "cancel value").ival;
        } else if (check(TokenKind::String)) {
            cs.options.push_back(consume().text);
        } else {
            throw ParseError("unexpected '" + peek().text + "' in choice(...)", peek().loc);
        }
    }
    expect(TokenKind::RParen, ")");

    // オプション: { when(...){} ... }
    if (check(TokenKind::LBrace)) {
        consume();
        while (!check(TokenKind::RBrace) && !at_end()) {
            if (check(TokenKind::Ident) && peek().text == "when") {
                consume(); // when
                expect(TokenKind::LParen, "(");
                WhenClause wc;
                wc.index = -1; // デフォルト: パラメータなし（古い形式）
                if (check(TokenKind::String)) wc.label = consume().text;
                else if (check(TokenKind::Ident)) wc.label = consume().text;
                // オプション: , index=N
                while (check(TokenKind::Comma)) {
                    consume();
                    if (check(TokenKind::Ident) && peek().text == "index"
                        && peek(1).kind == TokenKind::Equals) {
                        consume(); consume();
                        wc.index = expect(TokenKind::Int, "index value").ival;
                    } else if (!check(TokenKind::RParen)) {
                        // 未知のオプションを黙って捨てない (docs の「未知の引数はエラー」と一致させる)
                        throw ParseError("unknown option '" + peek().text + "' in when(...) (only index=N is allowed)", peek().loc);
                    }
                }
                expect(TokenKind::RParen, ")");
                wc.body = std::make_unique<StmtBlock>(parse_block());
                cs.whens.push_back(std::move(wc));
            } else {
                throw ParseError("expected when(...) inside choice, got '" + peek().text + "'", peek().loc);
            }
        }
        expect(TokenKind::RBrace, "}");
    }
    if (check(TokenKind::Semi)) consume();
    return cs;
}

CmdStmt Parser::parse_cmd(Token name_tok) {
    CmdStmt s;
    s.name = name_tok.text;
    s.loc  = name_tok.loc;
    if (check(TokenKind::LParen)) {
        consume();
        if (!check(TokenKind::RParen)) s.args = parse_arglist();
        expect(TokenKind::RParen, ")");
    }
    if (check(TokenKind::Semi)) consume();
    // Loop() { ... }  などのブロック付きコマンド: ボディを body フィールドに格納
    if (check(TokenKind::LBrace)) {
        s.body = std::make_unique<StmtBlock>(parse_block());
    }
    return s;
}

// ──────────────────────────────────────────────────────────────────────────────
// 引数パース
// ──────────────────────────────────────────────────────────────────────────────

std::vector<Arg> Parser::parse_arglist() {
    std::vector<Arg> args;
    args.push_back(parse_one_arg());

    auto do_compound = [&]() {
        if (args.empty() || args.back().val.kind != ArgVal::Indexed) return;
        std::string op = compound_op(peek().kind);
        if (op.empty()) return;
        consume();
        Arg op_arg; op_arg.key = "op"; op_arg.val.kind = ArgVal::Ident; op_arg.val.sval = op;
        args.push_back(op_arg);
        Arg val_arg; val_arg.val = parse_arg_val();
        args.push_back(val_arg);
    };
    do_compound();

    while (check(TokenKind::Comma)) {
        consume();
        // 文字列の連結: "A" + "B"
        if (!check(TokenKind::RParen)) {
            args.push_back(parse_one_arg());
            do_compound();
        }
    }
    return args;
}

std::vector<Arg> Parser::parse_cond_args() {
    std::vector<Arg> args;
    while (!check(TokenKind::RParen) && !check(TokenKind::Semi) && !at_end()) {
        // 比較演算子トークンをすべて op= として処理
        {
            std::string cmp_op;
            if      (check(TokenKind::EqEq)) cmp_op = "==";
            else if (check(TokenKind::Neq))  cmp_op = "!=";
            else if (check(TokenKind::GtEq)) cmp_op = ">=";
            else if (check(TokenKind::LtEq)) cmp_op = "<=";
            else if (check(TokenKind::Gt))   cmp_op = ">";
            else if (check(TokenKind::Lt))   cmp_op = "<";
            if (!cmp_op.empty()) {
                Arg a; a.key = "op"; a.val.kind = ArgVal::Ident; a.val.sval = cmp_op;
                args.push_back(a); consume(); continue;
            }
        }
        if (check(TokenKind::Comma)) { consume(); continue; }

        // Ident '(' ... ')' -> 関数呼び出し形式
        if (check(TokenKind::Ident) && peek(1).kind == TokenKind::LParen) {
            Token fn = consume(); consume();
            Arg fn_arg; fn_arg.key = fn.text; fn_arg.val.kind = ArgVal::Ident; fn_arg.val.sval = fn.text;
            args.push_back(fn_arg);
            while (!check(TokenKind::RParen) && !at_end()) {
                if (check(TokenKind::Comma)) { consume(); continue; }
                args.push_back(parse_one_arg());
            }
            if (check(TokenKind::RParen)) consume();
            continue;
        }

        args.push_back(parse_one_arg());

        // Indexed の後の = / op= / 比較演算子 を処理
        if (!args.empty() && args.back().val.kind == ArgVal::Indexed) {
            std::string op;
            if      (check(TokenKind::EqEq))  { op = "=="; }
            else if (check(TokenKind::Neq))   { op = "!="; }
            else if (check(TokenKind::GtEq))  { op = ">="; }
            else if (check(TokenKind::LtEq))  { op = "<="; }
            else if (check(TokenKind::Gt))    { op = ">"; }
            else if (check(TokenKind::Lt))    { op = "<"; }
            else if (check(TokenKind::Equals)) { op = "="; }
            else op = compound_op(peek().kind);
            if (!op.empty()) {
                consume();
                Arg op_arg; op_arg.key = "op"; op_arg.val.kind = ArgVal::Ident; op_arg.val.sval = op;
                args.push_back(op_arg);
                Arg val_arg; val_arg.val = parse_arg_val();
                args.push_back(val_arg);
            }
        }
    }
    return args;
}

Arg Parser::parse_one_arg() {
    Arg a;
    if (check(TokenKind::Ident) && peek(1).kind == TokenKind::Equals) {
        a.key = consume().text;
        consume(); // =
        a.val = parse_arg_val();
        return a;
    }
    a.val = parse_arg_val();
    return a;
}

ArgVal Parser::parse_arg_val() {
    ArgVal v;
    // リスト: [elem1, elem2, ...]
    // 各要素は識別子、整数、または "name(N)" 形式（switch_on(3) 等）
    if (check(TokenKind::LBracket)) {
        consume(); // [
        v.kind = ArgVal::List;
        while (!check(TokenKind::RBracket) && !check(TokenKind::Eof)) {
            Token t = consume();
            if (t.kind == TokenKind::Int) {
                v.list.push_back(std::to_string(t.ival));
            } else if (t.kind == TokenKind::Ident || t.kind == TokenKind::String) {
                std::string elem = t.text;
                // "name(N)" 形式: switch_on(3) / switch_off(5)
                if (check(TokenKind::LParen)) {
                    consume(); // (
                    Token id = expect(TokenKind::Int, "switch id");
                    expect(TokenKind::RParen, ")");
                    elem += "(" + std::to_string(id.ival) + ")";
                }
                v.list.push_back(elem);
            } else {
                throw ParseError("unexpected '" + t.text + "' in list", t.loc);
            }
            if (check(TokenKind::Comma)) consume();
        }
        expect(TokenKind::RBracket, "]");
        return v;
    }
    // 連結文字列: "a" + "b" + ...
    if (check(TokenKind::String)) {
        std::string s = consume().text;
        while (check(TokenKind::Plus) && peek(1).kind == TokenKind::String) {
            consume(); // +
            s += consume().text;
        }
        v.kind = ArgVal::Str; v.sval = s;
        return v;
    }
    if (check(TokenKind::Int)) {
        Token t = consume(); v.kind = ArgVal::Int; v.ival = t.ival; return v;
    }
    // 単項マイナス: -var[N] や -ident
    if (check(TokenKind::Minus)) {
        consume(); // -
        // Int は Lexer が -N を一つのトークンにするが、-var[N] はここで処理
        if (check(TokenKind::Ident)) {
            Token t = consume();
            if (check(TokenKind::LBracket)) {
                consume();
                Token idx = expect(TokenKind::Int, "index");
                expect(TokenKind::RBracket, "]");
                // -var[N] → key="neg_var", sval="var", ival=N と表現するか
                // 簡単のため: Indexed で sval="neg_var" として区別
                v.kind = ArgVal::Indexed; v.sval = "-" + t.text; v.ival = idx.ival;
                return v;
            }
            // -ident → ival=0, sval="-ident" 風
            v.kind = ArgVal::Ident; v.sval = "-" + t.text;
            return v;
        }
        // 数字の場合: -数字
        if (check(TokenKind::Int)) {
            Token t = consume();
            v.kind = ArgVal::Int; v.ival = -t.ival;
            return v;
        }
    }
    if (check(TokenKind::Ident)) {
        Token t = consume();
        if (check(TokenKind::LBracket)) {
            consume();
            if (check(TokenKind::String)) {
                // var["名前"] / switch["名前"] 形式
                std::string name = consume().text;
                expect(TokenKind::RBracket, "]");
                v.kind = ArgVal::Indexed; v.sval = t.text; v.ival = 0; v.sidx = name;
                return v;
            }
            Token idx = expect(TokenKind::Int, "index");
            expect(TokenKind::RBracket, "]");
            v.kind = ArgVal::Indexed; v.sval = t.text; v.ival = idx.ival;
            return v;
        }
        v.kind = ArgVal::Ident; v.sval = t.text;
        return v;
    }
    throw ParseError("expected value (int, string, or identifier)", peek().loc);
}

} // namespace rm2k
