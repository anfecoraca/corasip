// Settings file (M2): a flat, human-readable key/value file with [section] headers.
// Written atomically (temp file, then rename). Files that may hold credentials are created with mode 0600.
#pragma once

#include <map>
#include <string>

namespace corasip {

class Settings {
public:
    // Load from path. A missing file is not an error: the defaults apply.
    bool load(const std::string& path, std::string& err);
    // Save atomically. The directory is created with mode 0700 when missing.
    bool save(const std::string& path, std::string& err) const;

    std::string get(const std::string& section, const std::string& key, const std::string& fallback = "") const;
    void set(const std::string& section, const std::string& key, const std::string& value);
    bool has(const std::string& section, const std::string& key) const;
    void erase(const std::string& section, const std::string& key);

private:
    // Keys are stored as "section\x1fkey" so section and key can never collide.
    std::map<std::string, std::string> values_;
};

// Default location: $CORASIP_CONFIG, else $XDG_CONFIG_HOME/corasip/corasip.ini, else ~/.config/corasip/corasip.ini.
std::string defaultSettingsPath();

}  // namespace corasip
