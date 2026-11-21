// Unit tests for the M3 call history (SQLite).
#include "storage/history.hpp"

#include <sqlite3.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {
int failures = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                 \
        }                                                               \
    } while (0)
}  // namespace

int main() {
    const char* base = std::getenv("CORASIP_TEST_TMP");
    const std::string path = std::string(base ? base : "/tmp") + "/history-unit-" + std::to_string(getpid()) + ".db";
    unlink(path.c_str());
    std::string err;

    {
        corasip::History h;
        CHECK(h.open(path, err));
        corasip::CallRecord out1{0, "1090", false, "sip:1091@lab", "Ext 1091", 1000, 42, "answered"};
        corasip::CallRecord in1{0, "1090", true, "sip:1002@lab", "", 2000, 0, "missed"};
        corasip::CallRecord out2{0, "1090", false, "sip:10@lab", "", 3000, 5, "dialled"};
        CHECK(h.add(out1, err));
        CHECK(h.add(in1, err));
        CHECK(h.add(out2, err));

        std::vector<corasip::CallRecord> rows;
        CHECK(h.list(10, rows, err));
        CHECK(rows.size() == 3);
        if (rows.size() == 3) {
            CHECK(rows[0].remote == "sip:10@lab");        // most recent first
            CHECK(rows[1].incoming && rows[1].outcome == "missed");
            CHECK(rows[2].durationSeconds == 42);
        }
        std::string last;
        CHECK(h.lastDialled(last, err));
        CHECK(last == "sip:10@lab");  // redial target: most recent outgoing call
    }
    {
        corasip::History h;  // reopen: data survives a restart
        CHECK(h.open(path, err));
        std::vector<corasip::CallRecord> rows;
        CHECK(h.list(10, rows, err));
        CHECK(rows.size() == 3);
    }
    {
        // A file written by a newer version is refused.
        sqlite3* raw = nullptr;
        sqlite3_open(path.c_str(), &raw);
        sqlite3_exec(raw, "INSERT INTO schema_version (version) VALUES (99);", nullptr, nullptr, nullptr);
        sqlite3_close(raw);
        corasip::History h;
        CHECK(!h.open(path, err));
        CHECK(err.find("newer version") != std::string::npos);
    }
    {
        // An existing version 1 file gains the notes column on open (schema version 2).
        const std::string v1 = path + ".v1";
        unlink(v1.c_str());
        sqlite3* raw = nullptr;
        sqlite3_open(v1.c_str(), &raw);
        sqlite3_exec(raw,
            "CREATE TABLE schema_version (version INTEGER NOT NULL);"
            "INSERT INTO schema_version (version) VALUES (1);"
            "CREATE TABLE call_history (id INTEGER PRIMARY KEY, account TEXT NOT NULL,"
            "  direction TEXT NOT NULL, remote TEXT NOT NULL, display_name TEXT, started_at INTEGER NOT NULL,"
            "  duration_s INTEGER NOT NULL DEFAULT 0, outcome TEXT NOT NULL);"
            "CREATE TABLE contacts (id INTEGER PRIMARY KEY, name TEXT NOT NULL, number TEXT NOT NULL,"
            "  favourite INTEGER NOT NULL DEFAULT 0, created_at INTEGER NOT NULL);"
            "INSERT INTO contacts (name, number, favourite, created_at) VALUES ('Old', '1091', 0, 1);",
            nullptr, nullptr, nullptr);
        sqlite3_close(raw);
        {
            corasip::History h;
            CHECK(h.open(v1, err));
        }
        sqlite3* check2 = nullptr;
        sqlite3_open(v1.c_str(), &check2);
        int ver = 0, blankNotes = -1;
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(check2, "SELECT MAX(version) FROM schema_version;", -1, &st, nullptr) == SQLITE_OK) {
            if (sqlite3_step(st) == SQLITE_ROW) ver = sqlite3_column_int(st, 0);
            sqlite3_finalize(st);
        }
        if (sqlite3_prepare_v2(check2, "SELECT COUNT(*) FROM contacts WHERE info = '';", -1, &st, nullptr) == SQLITE_OK) {
            if (sqlite3_step(st) == SQLITE_ROW) blankNotes = sqlite3_column_int(st, 0);
            sqlite3_finalize(st);
        }
        sqlite3_close(check2);
        CHECK(ver == 3);  // version 3 adds the SIP status column to call history
        CHECK(blankNotes == 1);  // the existing contact gets an empty note
        unlink(v1.c_str());
    }

    unlink(path.c_str());
    if (failures == 0) std::printf("PASS: history unit tests\n");
    return failures == 0 ? 0 : 1;
}
