#include "storage/contacts_csv.hpp"

#include "storage/contacts.hpp"

#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <vector>

namespace corasip {

namespace {

// Spreadsheets run a cell that starts with = + - @ (or a tab or carriage return) as a formula. Such a cell gets a
// leading apostrophe on export, so a contact name cannot run code when the file is opened (CSV injection).
std::string quoteField(const std::string& input) {
    std::string s = input;
    if (!s.empty() && std::string("=+-@\t\r").find(s[0]) != std::string::npos) s = "'" + s;
    if (s.find_first_of(",\"\r\n") == std::string::npos) return s;
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += '"';
        out += c;
    }
    return out + "\"";
}

// Splits one CSV record into fields. Handles quoted fields with embedded commas and doubled quotes.
// Returns false on an unterminated quote.
bool parseRecord(std::istream& in, std::vector<std::string>& fields) {
    fields.clear();
    std::string field;
    bool quoted = false;
    bool any = false;
    for (int ch = in.get(); ch != EOF; ch = in.get()) {
        any = true;
        const char c = static_cast<char>(ch);
        if (quoted) {
            if (c == '"') {
                if (in.peek() == '"') { in.get(); field += '"'; }
                else quoted = false;
            } else {
                field += c;
            }
        } else if (c == '"') {
            quoted = true;
        } else if (c == ',') {
            fields.push_back(field);
            field.clear();
        } else if (c == '\n') {
            break;
        } else if (c != '\r') {
            field += c;
        }
    }
    if (quoted) return false;
    if (!any) return false;
    fields.push_back(field);
    return true;
}

}  // namespace

bool exportContactsCsv(const ContactStore& store, const std::string& path, std::string& err) {
    std::vector<Contact> all;
    if (!store.list("", all, err)) return false;
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) { err = "cannot write " + tmp; return false; }
        out << "name,number,favourite,info\n";
        for (const Contact& c : all) {
            out << quoteField(c.name) << ',' << quoteField(c.number) << ',' << (c.favourite ? 1 : 0) << ',' << quoteField(c.info) << '\n';
        }
        out.flush();
        if (!out) { err = "write failed for " + tmp; std::remove(tmp.c_str()); return false; }
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        err = "cannot replace " + path;
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

bool importContactsCsv(ContactStore& store, const std::string& path, CsvImportResult& result, std::string& err) {
    result = CsvImportResult();
    std::ifstream in(path);
    if (!in) { err = "cannot read " + path; return false; }

    // Numbers already present, so a re-import does not add duplicates.
    std::vector<Contact> existing;
    if (!store.list("", existing, err)) return false;
    std::set<std::string> numbers;
    for (const Contact& c : existing) numbers.insert(c.number);

    std::vector<std::string> fields;
    bool header = true;
    while (in.peek() != EOF) {
        if (!parseRecord(in, fields)) {
            if (in.peek() == EOF) break;
            ++result.skippedInvalid;
            continue;
        }
        if (header) {
            header = false;
            if (fields.size() >= 2 && fields[0] == "name" && fields[1] == "number") continue;
        }
        if (fields.size() < 2 || fields[1].empty()) { ++result.skippedInvalid; continue; }
        bool fav = false;
        if (fields.size() >= 3) {
            if (fields[2] == "1") fav = true;
            else if (fields[2] != "0" && !fields[2].empty()) { ++result.skippedInvalid; continue; }
        }
        Contact c;
        c.name = fields[0];
        c.number = fields[1];
        c.favourite = fav;
        if (fields.size() >= 4) c.info = fields[3];  // notes; older three-column files have none
        if (numbers.count(c.number) > 0) { ++result.skippedDuplicate; continue; }
        std::int64_t id = 0;
        std::string addErr;
        if (!store.add(c, id, addErr)) { ++result.skippedInvalid; continue; }
        numbers.insert(c.number);
        ++result.added;
    }
    return true;
}

}  // namespace corasip
