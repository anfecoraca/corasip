// Contact import and export in standard CSV: header "name,number,favourite" (SPEC D21).
// Fields follow RFC 4180 (quotes around fields with commas, quotes or newlines; "" inside quotes).
#pragma once

#include <cstddef>
#include <string>

namespace corasip {

class ContactStore;

struct CsvImportResult {
    std::size_t added = 0;
    std::size_t skippedDuplicate = 0;   // a contact with the same number already exists; never overwritten
    std::size_t skippedInvalid = 0;     // no number, or favourite not 0/1
};

// Writes every contact to path via a temporary file and a rename, so a failed write leaves no partial file.
bool exportContactsCsv(const ContactStore& store, const std::string& path, std::string& err);

// Adds the contacts in path. Duplicates (same number as an existing contact) are skipped, not overwritten.
bool importContactsCsv(ContactStore& store, const std::string& path, CsvImportResult& result, std::string& err);

}  // namespace corasip
