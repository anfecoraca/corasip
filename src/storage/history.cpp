#include "storage/history.hpp"

#include <sys/stat.h>

#include <sqlite3.h>

namespace corasip {

namespace {

bool exec(sqlite3* db, const char* sql, std::string& err) {
    char* msg = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &msg) != SQLITE_OK) {
        err = msg ? msg : "sqlite error";
        sqlite3_free(msg);
        return false;
    }
    return true;
}

}  // namespace

History::~History() { close(); }

bool History::open(const std::string& path, std::string& err) {
    close();
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        err = std::string("cannot open ") + path + ": " + sqlite3_errmsg(db_);
        close();
        return false;
    }
    // The history and contacts hold private data: only the owner may read or write the file (SPEC security).
    ::chmod(path.c_str(), 0600);
    if (!exec(db_, "CREATE TABLE IF NOT EXISTS schema_version (version INTEGER NOT NULL);", err)) return false;

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT COALESCE(MAX(version), 0) FROM schema_version;", -1, &st, nullptr) != SQLITE_OK) {
        err = sqlite3_errmsg(db_);
        return false;
    }
    const int found = sqlite3_step(st) == SQLITE_ROW ? sqlite3_column_int(st, 0) : 0;
    sqlite3_finalize(st);

    if (found > kSchemaVersion) {
        err = "history file was written by a newer version of corasip (schema " + std::to_string(found) + ")";
        close();
        return false;
    }
    if (found == 0) {
        const char* schema =
            "CREATE TABLE call_history ("
            "  id INTEGER PRIMARY KEY,"
            "  account TEXT NOT NULL,"
            "  direction TEXT NOT NULL CHECK (direction IN ('in','out')),"
            "  remote TEXT NOT NULL,"
            "  display_name TEXT,"
            "  started_at INTEGER NOT NULL,"
            "  duration_s INTEGER NOT NULL DEFAULT 0,"
            "  outcome TEXT NOT NULL,"
            "  sip_code INTEGER NOT NULL DEFAULT 0"
            ");"
            "CREATE TABLE contacts ("
            "  id INTEGER PRIMARY KEY,"
            "  name TEXT NOT NULL,"
            "  number TEXT NOT NULL,"
            "  favourite INTEGER NOT NULL DEFAULT 0,"
            "  created_at INTEGER NOT NULL,"
            "  info TEXT NOT NULL DEFAULT ''"
            ");"
            "INSERT INTO schema_version (version) VALUES (1);"
            "INSERT INTO schema_version (version) VALUES (2);"
            "INSERT INTO schema_version (version) VALUES (3);";
        if (!exec(db_, schema, err)) return false;
    } else if (found == 1) {
        // Version 1 to 2: contacts gain a notes field (Information). Existing rows get an empty note.
        if (!exec(db_, "ALTER TABLE contacts ADD COLUMN info TEXT NOT NULL DEFAULT '';"
                       "INSERT INTO schema_version (version) VALUES (2);"
                       "ALTER TABLE call_history ADD COLUMN sip_code INTEGER NOT NULL DEFAULT 0;"
                       "INSERT INTO schema_version (version) VALUES (3);", err)) return false;
    } else if (found == 2) {
        // Version 2 to 3: calls keep the last SIP final status, shown in the Logs Information column.
        if (!exec(db_, "ALTER TABLE call_history ADD COLUMN sip_code INTEGER NOT NULL DEFAULT 0;"
                       "INSERT INTO schema_version (version) VALUES (3);", err)) return false;
    }
    return true;
}

void History::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool History::add(const CallRecord& rec, std::string& err) {
    if (!db_) { err = "history not open"; return false; }
    sqlite3_stmt* st = nullptr;
    const char* sql =
        "INSERT INTO call_history (account, direction, remote, display_name, started_at, duration_s, outcome, sip_code)"
        " VALUES (?,?,?,?,?,?,?,?);";
    if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) { err = sqlite3_errmsg(db_); return false; }
    sqlite3_bind_text(st, 1, rec.account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, rec.incoming ? "in" : "out", -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 3, rec.remote.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, rec.displayName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 5, rec.startedAt);
    sqlite3_bind_int64(st, 6, rec.durationSeconds);
    sqlite3_bind_text(st, 7, rec.outcome.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 8, rec.sipCode);
    const bool ok = sqlite3_step(st) == SQLITE_DONE;
    if (!ok) err = sqlite3_errmsg(db_);
    sqlite3_finalize(st);
    return ok;
}

bool History::list(std::size_t limit, std::vector<CallRecord>& out, std::string& err) const {
    out.clear();
    if (!db_) { err = "history not open"; return false; }
    sqlite3_stmt* st = nullptr;
    const char* sql =
        "SELECT id, account, direction, remote, display_name, started_at, duration_s, outcome, sip_code"
        " FROM call_history ORDER BY started_at DESC, id DESC LIMIT ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) { err = sqlite3_errmsg(db_); return false; }
    sqlite3_bind_int64(st, 1, static_cast<sqlite3_int64>(limit));
    auto text = [st](int col) {
        const unsigned char* t = sqlite3_column_text(st, col);
        return t ? std::string(reinterpret_cast<const char*>(t)) : std::string();
    };
    while (sqlite3_step(st) == SQLITE_ROW) {
        CallRecord r;
        r.id = sqlite3_column_int64(st, 0);
        r.account = text(1);
        r.incoming = text(2) == "in";
        r.remote = text(3);
        r.displayName = text(4);
        r.startedAt = sqlite3_column_int64(st, 5);
        r.durationSeconds = sqlite3_column_int64(st, 6);
        r.outcome = text(7);
        r.sipCode = sqlite3_column_int(st, 8);
        out.push_back(r);
    }
    sqlite3_finalize(st);
    return true;
}

bool History::lastDialled(std::string& remote, std::string& err) const {
    remote.clear();
    if (!db_) { err = "history not open"; return false; }
    sqlite3_stmt* st = nullptr;
    const char* sql = "SELECT remote FROM call_history WHERE direction='out' ORDER BY started_at DESC, id DESC LIMIT 1;";
    if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) { err = sqlite3_errmsg(db_); return false; }
    if (sqlite3_step(st) == SQLITE_ROW) {
        const unsigned char* t = sqlite3_column_text(st, 0);
        remote = t ? reinterpret_cast<const char*>(t) : "";
    }
    sqlite3_finalize(st);
    return true;
}

}  // namespace corasip
