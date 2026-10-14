#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace rm2k {

// ──────────────────────────────────────────────────────────────────────────────
// NameTable — スイッチ・変数の ID ↔ 名前 変換テーブル
// ──────────────────────────────────────────────────────────────────────────────
//
// decode 時: switch[N] → switch["名前"] (名前がある場合)
// encode 時: switch["名前"] → switch[N]  (逆引き)
//
// RPG_RT.ldb から構築する。

struct NameTable {
    // id → name
    std::unordered_map<int32_t, std::string> sw_names;  // スイッチ
    std::unordered_map<int32_t, std::string> var_names; // 変数

    // name → id (エンコード用)
    std::unordered_map<std::string, int32_t> sw_ids;
    std::unordered_map<std::string, int32_t> var_ids;

    bool empty() const { return sw_names.empty() && var_names.empty(); }

private:
    // 名前を安全なダブルクォート形式に変換 (内部の " と \ をエスケープ)
    static std::string quote_name(const std::string& s) {
        std::string r = "\"";
        for (char c : s) {
            if      (c == '"')  r += "\\\"";
            else if (c == '\\') r += "\\\\";
            else                r += c;
        }
        r += '"';
        return r;
    }

public:
    // スイッチ ID → 表示用文字列
    // 名前が一意である（sw_ids に逆引きエントリがある）場合のみ "名前" 形式で返す
    // 名前が重複している場合は数値文字列を返す（ラウンドトリップ保証のため）
    std::string sw_label(int32_t id) const {
        auto it = sw_names.find(id);
        if (it != sw_names.end() && !it->second.empty()) {
            // 一意な名前かどうか確認
            auto inv = sw_ids.find(it->second);
            if (inv != sw_ids.end() && inv->second == id)
                return quote_name(it->second);
        }
        return std::to_string(id);
    }

    // 変数 ID → 表示用文字列
    std::string var_label(int32_t id) const {
        auto it = var_names.find(id);
        if (it != var_names.end() && !it->second.empty()) {
            auto inv = var_ids.find(it->second);
            if (inv != var_ids.end() && inv->second == id)
                return quote_name(it->second);
        }
        return std::to_string(id);
    }

    // 名前文字列またはIDリテラル文字列 → スイッチ ID
    // label が引用符付き ("\"名前\"") の場合も、素の名前文字列の場合も両方受け付ける
    int32_t sw_id(const std::string& label, int32_t fallback = 0) const {
        if (!label.empty() && label.front() == '"') {
            // 引用符付き形式: "名前"
            std::string name = label.substr(1, label.size() > 1 ? label.size() - 2 : 0);
            auto it = sw_ids.find(name);
            if (it != sw_ids.end()) return it->second;
        }
        // 素の名前文字列でも試みる
        auto it = sw_ids.find(label);
        if (it != sw_ids.end()) return it->second;
        try { return std::stoi(label); } catch (...) { return fallback; }
    }

    // 名前文字列またはIDリテラル文字列 → 変数 ID
    int32_t var_id(const std::string& label, int32_t fallback = 0) const {
        if (!label.empty() && label.front() == '"') {
            std::string name = label.substr(1, label.size() > 1 ? label.size() - 2 : 0);
            auto it = var_ids.find(name);
            if (it != var_ids.end()) return it->second;
        }
        // 素の名前文字列でも試みる
        auto it = var_ids.find(label);
        if (it != var_ids.end()) return it->second;
        try { return std::stoi(label); } catch (...) { return fallback; }
    }
};

} // namespace rm2k
