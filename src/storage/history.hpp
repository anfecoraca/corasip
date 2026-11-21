// Call history in SQLite (M3). Schema v1. Files written by a newer version are refused.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct sqlite3;

namespace corasip {

struct CallRecord {
    std::int64_t id = 0;
    std::string account;
    bool incoming = false;        // direction: "in" or "out"
    std::string remote;           // SIP URI or number
    std::string displayName;
    std::int64_t startedAt = 0;   // unix seconds, UTC
    std::int64_t durationSeconds = 0;
    std::string outcome;          // answered | missed | rejected | failed | dialled
    int sipCode = 0;              // last SIP final status of the call (200, 486, 487, 603 ...); 0 = none (schema v3)
};

class History {
public:
    History() = default;
    ~History();
    History(const History&) = delete;
    History& operator=(const History&) = delete;

    // Opens (and creates, if new) the database. Refuses a file with a newer schema version.
    bool open(const std::string& path, std::string& err);
    void close();

    bool add(const CallRecord& rec, std::string& err);
    // Most recent first.
    bool list(std::size_t limit, std::vector<CallRecord>& out, std::string& err) const;
    // The remote of the most recent outgoing call, for redial ("Last Call").
    bool lastDialled(std::string& remote, std::string& err) const;

    static constexpr int kSchemaVersion = 3;

private:
    sqlite3* db_ = nullptr;
};

}  // namespace corasip
