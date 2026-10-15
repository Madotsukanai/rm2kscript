#pragma once
#include "ast.h"
#include "nametable.h"
#include <lcf/rpg/map.h>
#include <lcf/rpg/database.h>
#include <string>
#include <stdexcept>

namespace rm2k {

// LMUファイル / LCFコマンド列 → 独自言語テキスト
class Decoder {
public:
    // 名前テーブルを設定 (オプション)
    void set_name_table(const NameTable* nt) { names_ = nt; }

    // マップ全体をテキストに変換
    std::string decode_map(const lcf::rpg::Map& map,
                           const std::string& encoding = "UTF-8");

    // コモンイベントをテキストに変換
    std::string decode_common_events(const lcf::rpg::Database& db,
                                     const std::string& encoding = "UTF-8");

private:
    const NameTable* names_ = nullptr;

    std::string decode_event_commands(
        const std::vector<lcf::rpg::EventCommand>& cmds,
        int base_indent = 0);
};

// エンコード時バリデーションエラー

class CompileError : public std::runtime_error {
public:
    CompileError(const std::string& msg, SourceLoc loc)
        : std::runtime_error(msg), loc_(loc) {}
    SourceLoc loc() const { return loc_; }
private:
    SourceLoc loc_;
};

// エンコーダー（テキスト → LCF）
class Encoder {
public:
    // 名前テーブルを設定 (オプション)
    void set_name_table(const NameTable* nt) { names_ = nt; }

    // データベースを設定 (バリデーション用 — オプション)
    void set_database(const lcf::rpg::Database* db) { db_ = db; }

    // true のとき、スクリプトに現れないイベント/ページをマップから削除する (encode_to_map のみ)
    void set_prune(bool v) { prune_ = v; }

    // テキストをパースしてLMUファイルに適用
    // target_map はすでに読み込まれたマップ; テキストで上書きする
    void encode_to_map(const std::string& source,
                       lcf::rpg::Map& target_map,
                       const std::string& encoding = "UTF-8");

    // コモンイベントとして適用
    void encode_to_database(const std::string& source,
                             lcf::rpg::Database& db,
                             const std::string& encoding = "UTF-8");

private:
    const NameTable*          names_ = nullptr;
    const lcf::rpg::Database* db_    = nullptr;
    bool                      prune_ = false;

    std::vector<lcf::rpg::EventCommand> encode_stmtblock(
        const StmtBlock& block,
        int32_t base_indent = 0,
        bool append_end = true);

    void encode_stmt(const StmtPtr& stmt,
                     int32_t indent,
                     std::vector<lcf::rpg::EventCommand>& out);

    void encode_while(const WhileStmt& s,
                      int32_t indent,
                      std::vector<lcf::rpg::EventCommand>& out);

    void encode_for(const ForStmt& s,
                    int32_t indent,
                    std::vector<lcf::rpg::EventCommand>& out);

    void encode_if(const IfStmt& s,
                   int32_t indent,
                   std::vector<lcf::rpg::EventCommand>& out);

    void encode_choice(const ChoiceStmt& s,
                       int32_t indent,
                       std::vector<lcf::rpg::EventCommand>& out);

    void encode_switch_assign(const SwitchAssign& s,
                              int32_t indent,
                              std::vector<lcf::rpg::EventCommand>& out);

    void encode_var_assign(const VarAssign& s,
                           int32_t indent,
                           std::vector<lcf::rpg::EventCommand>& out);
};

} // namespace rm2k
