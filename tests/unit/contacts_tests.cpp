// Contacts store (M4): CRUD, search, favourites first, number rules. Uses a temporary history file.
#include "storage/contacts.hpp"
#include "storage/contacts_csv.hpp"
#include "storage/history.hpp"

#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <fstream>
#include <sstream>

int main() {
    const char* dir = std::getenv("CORASIP_TEST_TMP");
    const std::string path = std::string(dir ? dir : "/tmp") + "/contacts-test-" + std::to_string(getpid()) + ".db";
    std::string err;
    corasip::History history;
    if (!history.open(path, err)) { std::printf("FAIL open history: %s\n", err.c_str()); return 1; }
    history.close();

    corasip::ContactStore store;
    if (!store.open(path, err)) { std::printf("FAIL open contacts: %s\n", err.c_str()); return 1; }

    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) { std::printf("FAIL: %s (%s)\n", what, err.c_str()); ++failures; }
        err.clear();
    };

    // Number is required; name is optional (SPEC D21).
    std::int64_t id = 0;
    corasip::Contact noNumber;
    noNumber.name = "Nobody";
    check(!store.add(noNumber, id, err), "a contact without a number is refused");
    corasip::Contact blank;
    blank.number = "   ";
    check(!store.add(blank, id, err), "a blank number is refused");

    corasip::Contact bob;
    bob.name = "Bob";
    bob.number = " 1091 ";
    check(store.add(bob, id, err) && id > 0, "add a contact with a name");
    const std::int64_t bobId = id;
    corasip::Contact noName;
    noName.number = "1090";
    check(store.add(noName, id, err), "add a contact without a name");
    corasip::Contact alice;
    alice.name = "alice";
    alice.number = "2001";
    alice.favourite = true;
    check(store.add(alice, id, err), "add a favourite");

    std::vector<corasip::Contact> all;
    check(store.list("", all, err) && all.size() == 3, "list returns all three");
    check(!all.empty() && all[0].favourite && all[0].name == "alice", "favourites sort first");
    check(all.size() == 3 && all[1].name.empty() && all[1].number == "1090", "a nameless contact sorts before names");
    check(all.size() == 3 && all[2].name == "Bob" && all[2].number == "1091", "number is trimmed");

    std::vector<corasip::Contact> hits;
    check(store.list("bo", hits, err) && hits.size() == 1 && hits[0].name == "Bob", "search by name, case-insensitive");
    check(store.list("109", hits, err) && hits.size() == 2, "search by number");
    check(store.list("50%", hits, err) && hits.empty(), "a % in the search is literal");

    corasip::Contact edit = bob;
    edit.id = bobId;
    edit.name = "Bob Two";
    edit.favourite = true;
    check(store.update(edit, err), "update a contact");
    check(store.list("", all, err) && all[0].name == "alice" && all[1].name == "Bob Two" && all[2].name.empty(), "update moves Bob to the favourites");

    corasip::Contact missing;
    missing.id = 999999;
    missing.number = "1";
    check(!store.update(missing, err), "updating a missing contact fails");

    check(store.remove(bobId, err), "delete a contact");
    check(store.list("", all, err) && all.size() == 2, "delete removes the row");

    // Round trip (SPEC D21): export, import into a fresh store, same contacts; re-import adds nothing.
    corasip::Contact tricky;
    tricky.name = "Smith, \"Jo\"";
    tricky.number = "3001";
    tricky.favourite = true;
    check(store.add(tricky, id, err), "add a name with a comma and quotes");
    const std::string csv = path + ".csv";
    check(corasip::exportContactsCsv(store, csv, err), "export to CSV");
    store.close();

    const std::string path2 = path + ".2.db";
    corasip::History history2;
    check(history2.open(path2, err), "open a second store");
    history2.close();
    corasip::ContactStore fresh;
    check(fresh.open(path2, err), "open the fresh store");
    corasip::CsvImportResult res;
    check(corasip::importContactsCsv(fresh, csv, res, err) && res.added == 3 && res.skippedDuplicate == 0,
          "import adds all three contacts");
    std::vector<corasip::Contact> back;
    check(fresh.list("", back, err) && back.size() == 3, "fresh store holds three contacts");
    bool trickyOk = false;
    for (const auto& c : back) {
        if (c.number == "3001" && c.name == "Smith, \"Jo\"" && c.favourite) trickyOk = true;
    }
    check(trickyOk, "a comma and quotes survive the round trip, favourite kept");
    corasip::CsvImportResult again;
    check(corasip::importContactsCsv(fresh, csv, again, err) && again.added == 0 && again.skippedDuplicate == 3,
          "re-import adds nothing and skips duplicates");
    fresh.close();
    unlink(path2.c_str());
    unlink(csv.c_str());

    // Notes (Information): store, update, and the CSV export carries them.
    corasip::History notesHistory;
    check(notesHistory.open(path, err), "reopen history for the notes checks");
    corasip::ContactStore notesStore;
    check(notesStore.open(path, err), "reopen contacts for the notes checks");
    corasip::Contact noted;
    noted.name = "Noted";
    noted.number = "1093";
    noted.info = "Ops team, ask for Ana";
    check(notesStore.add(noted, id, err), "add a contact with notes");
    std::vector<corasip::Contact> rowsNoted;
    check(notesStore.list("Noted", rowsNoted, err) && rowsNoted.size() == 1 && rowsNoted[0].info == "Ops team, ask for Ana",
          "notes are stored and read back");
    if (!rowsNoted.empty()) {
        corasip::Contact changed = rowsNoted[0];
        changed.info = "Updated note";
        check(notesStore.update(changed, err), "update the notes");
    }
    std::vector<corasip::Contact> notesAgain;
    check(notesStore.list("Noted", notesAgain, err) && notesAgain.size() == 1 && notesAgain[0].info == "Updated note",
          "updated notes read back");
    const std::string csvPath = path + ".notes.csv";
    check(corasip::exportContactsCsv(notesStore, csvPath, err), "export with notes");
    std::ifstream csvIn(csvPath);
    std::stringstream csvText;
    csvText << csvIn.rdbuf();
    check(csvText.str().rfind("name,number,favourite,info", 0) == 0 && csvText.str().find("Updated note") != std::string::npos,
          "CSV carries the notes column");
    std::remove(csvPath.c_str());

    store.close();
    unlink(path.c_str());
    if (failures == 0) std::printf("PASS: contacts store and CSV round trip\n");
    return failures == 0 ? 0 : 1;
}
