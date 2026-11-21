#include "settings/settings.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace corasip {

namespace {

const char kSep = '\x1f';

std::string keyOf(const std::string& section, const std::string& key) {
    return section + kSep + key;
}

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r");
    if (b == std::string::npos) return "";
    const auto e = s.find_last_not_of(" \t\r");
    return s.substr(b, e - b + 1);
}

bool makeDirs(const std::string& dir, std::string& err) {
    std::string path;
    std::istringstream parts(dir);
    std::string part;
    if (!dir.empty() && dir[0] == '/') path = "";
    while (std::getline(parts, part, '/')) {
        if (part.empty()) continue;
        path += "/" + part;
        if (::mkdir(path.c_str(), 0700) != 0 && errno != EEXIST) {
            err = "cannot create " + path + ": " + std::strerror(errno);
            return false;
        }
    }
    return true;
}

}  // namespace

bool Settings::load(const std::string& path, std::string& err) {
    values_.clear();
    std::ifstream in(path);
    if (!in) {
        if (::access(path.c_str(), F_OK) != 0) return true;  // no file yet: defaults apply
        err = "cannot read " + path;
        return false;
    }
    std::string line;
    std::string section;
    while (std::getline(in, line)) {
        const std::string t = trim(line);
        if (t.empty() || t[0] == ';' || t[0] == '#') continue;
        if (t.front() == '[' && t.back() == ']') {
            section = trim(t.substr(1, t.size() - 2));
            continue;
        }
        const auto eq = t.find('=');
        if (eq == std::string::npos) continue;  // malformed line: ignored, not fatal
        values_[keyOf(section, trim(t.substr(0, eq)))] = trim(t.substr(eq + 1));
    }
    return true;
}

bool Settings::save(const std::string& path, std::string& err) const {
    const auto slash = path.rfind('/');
    if (slash != std::string::npos && slash > 0 && !makeDirs(path.substr(0, slash), err)) return false;

    std::string body = "; corasip settings. Passwords are stored here only when the account asks for it.\n";
    std::string lastSection;
    for (const auto& kv : values_) {
        const auto sep = kv.first.find(kSep);
        const std::string section = kv.first.substr(0, sep);
        const std::string key = kv.first.substr(sep + 1);
        if (section != lastSection) {
            body += "\n[" + section + "]\n";
            lastSection = section;
        }
        body += key + "=" + kv.second + "\n";
    }

    const std::string tmp = path + ".tmp";
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        err = "cannot write " + tmp + ": " + std::strerror(errno);
        return false;
    }
    const bool ok = ::write(fd, body.data(), body.size()) == static_cast<ssize_t>(body.size()) && ::fsync(fd) == 0;
    ::close(fd);
    if (!ok || ::rename(tmp.c_str(), path.c_str()) != 0) {
        err = "cannot save " + path + ": " + std::strerror(errno);
        ::unlink(tmp.c_str());
        return false;
    }
    ::chmod(path.c_str(), 0600);
    return true;
}

std::string Settings::get(const std::string& section, const std::string& key, const std::string& fallback) const {
    const auto it = values_.find(keyOf(section, key));
    return it == values_.end() ? fallback : it->second;
}

void Settings::set(const std::string& section, const std::string& key, const std::string& value) {
    values_[keyOf(section, key)] = trim(value);
}

bool Settings::has(const std::string& section, const std::string& key) const {
    return values_.count(keyOf(section, key)) != 0;
}

void Settings::erase(const std::string& section, const std::string& key) {
    values_.erase(keyOf(section, key));
}

std::string defaultSettingsPath() {
    if (const char* env = std::getenv("CORASIP_CONFIG"); env && *env) return env;
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) return std::string(xdg) + "/corasip/corasip.ini";
    if (const char* home = std::getenv("HOME"); home && *home) return std::string(home) + "/.config/corasip/corasip.ini";
    return "corasip.ini";
}

}  // namespace corasip
