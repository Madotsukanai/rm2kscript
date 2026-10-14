#pragma once
#include "ast.h"
#include <lcf/rpg/map.h>
#include <lcf/rpg/database.h>
#include <string>

namespace rm2k {

// LMUファイル / LCFコマンド列 → 独自言語テキスト
class Decoder {
public:
    // マップ全体をテキストに変換
    std::string decode_map(const lcf::rpg::Map& map,
                           const std::string& encoding = "UTF-8");

    // コモンイベントをテキストに変換
    std::string decode_common_events(const lcf::rpg::Database& db,
                                     const std::string& encoding = "UTF-8");

private:
    std::string decode_event_commands(
        const std::vector<lcf::rpg::EventCommand>& cmds,
        int base_indent = 0);

    std::string decode_one_cmd(const lcf::rpg::EventCommand& cmd,
                               int visual_indent);
};

// エンコーダー（テキスト → LCF）
class Encoder {
public:
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
    std::vector<lcf::rpg::EventCommand> encode_stmtblock(
        const StmtBlock& block,
        int32_t base_indent = 0,
        bool append_end = true);

    void encode_stmt(const StmtPtr& stmt,
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
