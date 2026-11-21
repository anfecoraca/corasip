// Contacts in SQLite (M4). Uses the contacts table from schema v1.
// Favourites sort first, then by name. Number is required; name is optional (SPEC D21, D23).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct sqlite3;

namespace corasip {

struct Contact {
    std::int64_t id = 0;
    std::string name;
    std::string number;
    bool favourite = false;
    std::string info;             // notes about the contact, one line (M4, schema v2)
    std::int64_t createdAt = 0;   // unix seconds, UTC
};

class ContactStore {
public:
    ContactStore() = default;
    ~ContactStore();
    ContactStore(const ContactStore&) = delete;
    ContactStore& operator=(const ContactStore&) = delete;

    // Opens the database that History has already created (the contacts table must exist).
    bool open(const std::string& path, std::string& err);
    void close();

    // Adds a contact. Number is trimmed and must not be empty. Returns the new id in `id`.
    bool add(Contact contact, std::int64_t& id, std::string& err);
    bool update(const Contact& contact, std::string& err);
    bool remove(std::int64_t id, std::string& err);
    // All contacts, favourites first, then by name. A non-empty query matches name or number (case-insensitive).
    bool list(const std::string& query, std::vector<Contact>& out, std::string& err) const;

private:
    sqlite3* db_ = nullptr;
};

}  // namespace corasip
