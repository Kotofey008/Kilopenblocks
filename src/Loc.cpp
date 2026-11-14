#include "Loc.h"
#include "Assets.h"
#include <sstream>
#include <iostream>

// Minimal CSV: Key,"Value with
// possible newlines"
// Values may be quoted and contain commas/newlines until closing quote.

bool Localization::LoadLanguage(const char* langCode) {
    if (!langCode || !*langCode) return false;
    std::string path = std::string("loc/") + langCode + ".csv";
    std::string text;
    if (!Assets::ReadText(path.c_str(), text)) {
        std::cout << "Localization: failed to load " << path << std::endl;
        return false;
    }
    table_.clear();
    lang_ = langCode;

    // Strip UTF-8 BOM
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF)
        text = text.substr(3);

    size_t i = 0;
    const size_t n = text.size();
    while (i < n) {
        // skip whitespace/newlines
        while (i < n && (text[i] == '\r' || text[i] == '\n' || text[i] == ' ' || text[i] == '\t'))
            ++i;
        if (i >= n) break;

        // key until comma
        size_t keyStart = i;
        while (i < n && text[i] != ',' && text[i] != '\r' && text[i] != '\n')
            ++i;
        std::string key = text.substr(keyStart, i - keyStart);
        // trim key
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
        if (i < n && text[i] == ',') ++i;

        std::string val;
        if (i < n && text[i] == '"') {
            ++i;
            while (i < n) {
                if (text[i] == '"') {
                    if (i + 1 < n && text[i + 1] == '"') { // escaped quote
                        val.push_back('"');
                        i += 2;
                        continue;
                    }
                    ++i; // closing
                    break;
                }
                val.push_back(text[i++]);
            }
            // consume trailing junk until newline
            while (i < n && text[i] != '\r' && text[i] != '\n') ++i;
        } else {
            size_t v0 = i;
            while (i < n && text[i] != '\r' && text[i] != '\n') ++i;
            val = text.substr(v0, i - v0);
        }
        if (!key.empty())
            table_[key] = val;
    }
    std::cout << "Localization: loaded " << table_.size() << " strings from " << path << std::endl;
    return !table_.empty();
}

std::string Localization::Localize(const std::string& textOrKey) const {
    if (textOrKey.empty()) return {};

    std::string key, fallback;
    if (textOrKey[0] == '/') {
        // "/Key/Fallback label"  (binary Localize strips first '/' segment)
        size_t mid = textOrKey.find('/', 1);
        if (mid != std::string::npos) {
            key = textOrKey.substr(1, mid - 1);
            fallback = textOrKey.substr(mid + 1);
        } else {
            key = textOrKey.substr(1);
        }
    } else {
        key = textOrKey;
        fallback = textOrKey;
    }

    auto it = table_.find(key);
    if (it != table_.end() && !it->second.empty())
        return it->second;
    if (!fallback.empty())
        return fallback;
    return key;
}


bool Localization::InitFromSystem(const char* systemLang) {
    // Try device language (2-letter), then en. Matches binary: load loc/<lang>.csv,
    // on failure fall back (en).
    if (systemLang && systemLang[0]) {
        char code[8] = {};
        // AConfiguration_getLanguage returns 2 chars, not always NUL-terminated
        code[0] = systemLang[0];
        code[1] = systemLang[1] ? systemLang[1] : '\0';
        code[2] = '\0';
        // lowercase
        for (int i = 0; code[i]; ++i) {
            if (code[i] >= 'A' && code[i] <= 'Z') code[i] = (char)(code[i] - 'A' + 'a');
        }
        if (code[0] && LoadLanguage(code)) {
            std::cout << "Localization: loaded " << code << " (" << table_.size() << " keys)\n";
            return true;
        }
        std::cout << "Localization: no loc/" << code << ".csv, trying en\n";
    }
    if (LoadLanguage("en")) {
        std::cout << "Localization: loaded en (" << table_.size() << " keys)\n";
        return true;
    }
    return false;
}
