#pragma once
#include "ast.h"
#include <string>
#include <vector>
#include <stdexcept>

namespace rm2k {

// ──────────────────────────────────────────────────────────────────────────────
// Lexer
// ──────────────────────────────────────────────────────────────────────────────

class LexError : public std::runtime_error {
public:
    LexError(const std::string& msg, SourceLoc loc)
        : std::runtime_error(msg), loc_(loc) {}
    SourceLoc loc() const { return loc_; }
private:
    SourceLoc loc_;
};

class Lexer {
public:
    explicit Lexer(std::string_view src);
    std::vector<Token> tokenize();

private:
    std::string_view src_;
    size_t pos_ = 0;
    int    line_ = 1;
    int    col_  = 1;

    char peek(int offset = 0) const;
    char advance();
    void skip_ws_and_comments();
    Token read_string();
    Token read_number(bool negative);
    Token read_ident_or_kw();
    SourceLoc here() const { return {line_, col_}; }
};

// ──────────────────────────────────────────────────────────────────────────────
// Parser
// ──────────────────────────────────────────────────────────────────────────────

class ParseError : public std::runtime_error {
public:
    ParseError(const std::string& msg, SourceLoc loc)
        : std::runtime_error(msg), loc_(loc) {}
    SourceLoc loc() const { return loc_; }
private:
    SourceLoc loc_;
};

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);
    MapDoc parse();

private:
    std::vector<Token> tokens_;
    size_t pos_ = 0;

    Token& peek(int offset = 0);
    Token  consume();
    Token  expect(TokenKind k, const char* what);
    bool   check(TokenKind k) const;
    bool   at_end() const;

    // トップレベル
    MapDoc::PageEntry parse_event_entry();   // event(...) { page(...){} ... }
    MapDoc::PageEntry parse_page_entry_legacy(); // 旧 @event/@page 形式

    // ページメタデータ
    void apply_meta_kv(PageMeta& m, const std::string& key, const std::string& val_s, int val_i);

    // ブロック・文
    StmtBlock        parse_block();
    StmtPtr          parse_stmt();
    IfStmt           parse_if();
    ChoiceStmt       parse_choice(Token name_tok);
    CmdStmt          parse_cmd(Token name_tok);

    // 引数
    std::vector<Arg> parse_arglist();
    std::vector<Arg> parse_cond_args();
    Arg              parse_one_arg();
    ArgVal           parse_arg_val();
};

} // namespace rm2k
