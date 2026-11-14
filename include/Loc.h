#pragma once
#include <string>
#include <map>

// GLApplication::Localize (0x6fd50 / 0x7fd50)
// Keys in UI Text fields are "/Key/fallback". Lookup is CSV column 0 → column 1
// from assets/loc/<lang>.csv (en.csv, pl.csv). Leading "//" or single "/" key forms
// also accepted. If the key is missing, the fallback label after the second '/' is used.

class Localization {
public:
    static Localization& Instance() {
        static Localization inst;
        return inst;
    }

    bool LoadLanguage(const char* langCode); // "en", "pl"
    const std::string& CurrentLang() const { return lang_; }

    // Resolve "/Key/Fallback" or "Key" → translated string (or fallback/key).
    std::string Localize(const std::string& textOrKey) const;

private:
    Localization() = default;
    std::string lang_ = "en";
    std::map<std::string, std::string> table_;
};
