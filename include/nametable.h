#pragma once
#include "textutil.h"
#include <lcf/rpg/database.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <optional>
#include <cstdint>

namespace rm2k {

// ──────────────────────────────────────────────────────────────────────────────
// NameTable — スイッチ・変数の ID ↔ 名前 変換テーブル
// ──────────────────────────────────────────────────────────────────────────────
//
// decode 時: switch[N] → switch["名前"] (名前がある場合)
// encode 時: switch["名前"] → switch[N]  (逆引き)
//
// RPG_RT.ldb から構築する (from_database)。

struct NameTable {
    // id → name
    std::unordered_map<int32_t, std::string> sw_names;  // スイッチ
    std::unordered_map<int32_t, std::string> var_names; // 変数

    // name → id (エンコード用。一意な名前のみ登録される)
    std::unordered_map<std::string, int32_t> sw_ids;
    std::unordered_map<std::string, int32_t> var_ids;

    bool empty() const { return sw_names.empty() && var_names.empty(); }

    // データベースから構築する。
    // 同じ名前が複数のIDに割り当てられている場合は逆引きを登録しない
    // (登録すると最初のIDが返り、ラウンドトリップが壊れるため。その場合は数値表記になる)。
    static NameTable from_database(const lcf::rpg::Database& db) {
        NameTable nt;
        std::unordered_map<std::string, int> sw_count, var_count;
        for (const auto& sw : db.switches) {
            std::string name(sw.name.begin(), sw.name.end());
            if (!name.empty()) sw_count[name]++;
        }
        for (const auto& vr : db.variables) {
            std::string name(vr.name.begin(), vr.name.end());
            if (!name.empty()) var_count[name]++;
        }
        for (const auto& sw : db.switches) {
            std::string name(sw.name.begin(), sw.name.end());
            if (name.empty()) continue;
            nt.sw_names[sw.ID] = name;
            if (sw_count[name] == 1) nt.sw_ids[name] = sw.ID;
        }
        for (const auto& vr : db.variables) {
            std::string name(vr.name.begin(), vr.name.end());
            if (name.empty()) continue;
            nt.var_names[vr.ID] = name;
            if (var_count[name] == 1) nt.var_ids[name] = vr.ID;
        }
        return nt;
    }

    // スイッチ ID → 表示用文字列
    // 名前が一意である (sw_ids に逆引きエントリがある) 場合のみ "名前" 形式で返す。
    // 重複している場合は数値文字列を返す (ラウンドトリップ保証のため)。
    std::string sw_label(int32_t id) const {
        auto it = sw_names.find(id);
        if (it != sw_names.end() && !it->second.empty()) {
            auto inv = sw_ids.find(it->second);
            if (inv != sw_ids.end() && inv->second == id)
                return quote_string(it->second);
        }
        return std::to_string(id);
    }

    // 変数 ID → 表示用文字列
    std::string var_label(int32_t id) const {
        auto it = var_names.find(id);
        if (it != var_names.end() && !it->second.empty()) {
            auto inv = var_ids.find(it->second);
            if (inv != var_ids.end() && inv->second == id)
                return quote_string(it->second);
        }
        return std::to_string(id);
    }

    // 名前 (またはID数値文字列) → スイッチ ID。見つからなければ nullopt
    std::optional<int32_t> find_sw(const std::string& label) const {
        return find_in(sw_ids, label);
    }

    // 名前 (またはID数値文字列) → 変数 ID。見つからなければ nullopt
    std::optional<int32_t> find_var(const std::string& label) const {
        return find_in(var_ids, label);
    }

    // 互換用: 見つからない場合は fallback を返す。
    // 新しいコードでは find_sw / find_var を使い、失敗をエラーにすること。
    int32_t sw_id(const std::string& label, int32_t fallback = 0) const {
        auto r = find_sw(label);
        return r ? *r : fallback;
    }
    int32_t var_id(const std::string& label, int32_t fallback = 0) const {
        auto r = find_var(label);
        return r ? *r : fallback;
    }

private:
    static std::optional<int32_t> parse_int(const std::string& s) {
        if (s.empty()) return std::nullopt;
        size_t i = (s[0] == '-') ? 1 : 0;
        if (i >= s.size()) return std::nullopt;
        for (size_t k = i; k < s.size(); ++k)
            if (s[k] < '0' || s[k] > '9') return std::nullopt;
        try { return static_cast<int32_t>(std::stoi(s)); }
        catch (...) { return std::nullopt; }
    }

    static std::optional<int32_t> find_in(
        const std::unordered_map<std::string, int32_t>& table,
        const std::string& label)
    {
        auto it = table.find(label);
        if (it != table.end()) return it->second;
        // 引用符付き形式 ("名前") も受け付ける
        if (label.size() >= 2 && label.front() == '"' && label.back() == '"') {
            it = table.find(label.substr(1, label.size() - 2));
            if (it != table.end()) return it->second;
        }
        return parse_int(label);
    }
};

} // namespace rm2k
