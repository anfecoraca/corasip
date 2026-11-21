#include "storage/contacts.hpp"

#include <sqlite3.h>

#include <chrono>

namespace corasip {

namespace {

std::string trimmed(const std::string& s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

// Escapes LIKE wildcards so a search for "50%" matches the text, not everything.
std::string likePattern(const std::string& query) {
    std::string out = "%";
    for (char c : query) {
        if (c == '%' || c == '_' || c == '\\') out += '\\';
        out += c;
    }
    return out + "%";
}

}  // namespace

ContactStore::~ContactStore() { close(); }

bool ContactStore::open(const std::string& path, std::string& err) {
    close();
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        err = std::string("cannot open ") + path + ": " + sqlite3_errmsg(db_);
        close();
        return false;
    }
    sqlite3_stmt* st = nullptr;
    const char* probe = "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'contacts';";
    if (sqlite3_prepare_v2(db_, probe, -1, &st, nullptr) != SQLITE_OK) {
        err = sqlite3_errmsg(db_);
        close();
        return false;
    }
    const bool found = sqlite3_step(st) == SQLITE_ROW;
    sqlite3_finalize(st);
    if (!found) {
        err = "contacts table missing: open the history store first";
        close();
        return false;
    }
    return true;
}

void ContactStore::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool ContactStore::add(Contact contact, std::int64_t& id, std::string& err) {
    if (!db_) { err = "contacts not open"; return false; }
    contact.number = trimmed(contact.number);
    if (contact.number.empty()) { err = "a contact needs a number"; return false; }
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch()).count();
    sqlite3_stmt* st = nullptr;
    const char* sql = "INSERT INTO contacts (name, number, favourite, created_at, info) VALUES (?, ?, ?, ?, ?);";
    if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) { err = sqlite3_errmsg(db_); return false; }
    sqlite3_bind_text(st, 1, contact.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, contact.number.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 3, contact.favourite ? 1 : 0);
    sqlite3_bind_int64(st, 4, static_cast<sqlite3_int64>(now));
    sqlite3_bind_text(st, 5, contact.info.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(st) == SQLITE_DONE;
    if (!ok) err = sqlite3_errmsg(db_);
    sqlite3_finalize(st);
    if (ok) id = sqlite3_last_insert_rowid(db_);
    return ok;
}

bool ContactStore::update(const Contact& contact, std::string& err) {
    if (!db_) { err = "contacts not open"; return false; }
    const std::string number = trimmed(contact.number);
    if (number.empty()) { err = "a contact needs a number"; return false; }
    sqlite3_stmt* st = nullptr;
    const char* sql = "UPDATE contacts SET name = ?, number = ?, favourite = ?, info = ? WHERE id = ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) { err = sqlite3_errmsg(db_); return false; }
    sqlite3_bind_text(st, 1, contact.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, number.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 3, contact.favourite ? 1 : 0);
    sqlite3_bind_text(st, 4, contact.info.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 5, static_cast<sqlite3_int64>(contact.id));
    const bool ok = sqlite3_step(st) == SQLITE_DONE;
    if (!ok) err = sqlite3_errmsg(db_);
    sqlite3_finalize(st);
    if (ok && sqlite3_changes(db_) == 0) {
        err = "no such contact";
        return false;
    }
    return ok;
}

bool ContactStore::remove(std::int64_t id, std::string& err) {
    if (!db_) { err = "contacts not open"; return false; }
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_, "DELETE FROM contacts WHERE id = ?;", -1, &st, nullptr) != SQLITE_OK) {
        err = sqlite3_errmsg(db_);
        return false;
    }
    sqlite3_bind_int64(st, 1, static_cast<sqlite3_int64>(id));
    const bool ok = sqlite3_step(st) == SQLITE_DONE;
    if (!ok) err = sqlite3_errmsg(db_);
    sqlite3_finalize(st);
    return ok;
}

bool ContactStore::list(const std::string& query, std::vector<Contact>& out, std::string& err) const {
    out.clear();
    if (!db_) { err = "contacts not open"; return false; }
    const std::string q = trimmed(query);
    sqlite3_stmt* st = nullptr;
    const char* sql =
        "SELECT id, name, number, favourite, created_at, info FROM contacts "
        "WHERE ? = '' OR name LIKE ? ESCAPE '\\' OR number LIKE ? ESCAPE '\\' "
        "ORDER BY favourite DESC, name COLLATE NOCASE, number;";
    if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) { err = sqlite3_errmsg(db_); return false; }
    const std::string pattern = likePattern(q);
    sqlite3_bind_text(st, 1, q.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, pattern.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, pattern.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW) {
        Contact c;
        c.id = sqlite3_column_int64(st, 0);
        c.name = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
        c.number = reinterpret_cast<const char*>(sqlite3_column_text(st, 2));
        c.favourite = sqlite3_column_int(st, 3) != 0;
        c.createdAt = sqlite3_column_int64(st, 4);
        const unsigned char* info = sqlite3_column_text(st, 5);
        c.info = info ? reinterpret_cast<const char*>(info) : "";
        out.push_back(c);
    }
    const bool ok = sqlite3_errcode(db_) == SQLITE_OK || sqlite3_errcode(db_) == SQLITE_DONE;
    if (!ok) err = sqlite3_errmsg(db_);
    sqlite3_finalize(st);
    return ok;
}

}  // namespace corasip
