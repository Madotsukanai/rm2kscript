#pragma once
#include <string>

namespace rm2k {

// 文字列を .rs2k のダブルクォートリテラルに変換する。
// Lexer::read_string が復元できるエスケープ (\" \\ \n \r \t) のみを使う。
// decoder / commands / nametable で共通利用すること (以前は3箇所に重複していた)。
inline std::string quote_string(const std::string& s) {
    std::string r = "\"";
    for (char c : s) {
        switch (c) {
            case '"':  r += "\\\""; break;
            case '\\': r += "\\\\"; break;
            case '\n': r += "\\n";  break;
            case '\r': r += "\\r";  break;
            case '\t': r += "\\t";  break;
            default:   r += c;      break;
        }
    }
    r += '"';
    return r;
}

} // namespace rm2k
