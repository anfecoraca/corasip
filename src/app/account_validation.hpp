// Account checks shared by the account dialog and the settings loader (M2).
#pragma once

#include "engine/engine.hpp"
#include "settings/settings.hpp"

#include <string>
#include <vector>

namespace corasip {

// Returns the list of problems. An empty list means the account can be saved.
std::vector<std::string> validateAccount(const AccountSettings& acc);

// Reads the single account from the settings file section [account]. Missing keys keep defaults.
void loadAccount(const Settings& s, AccountSettings& acc);
void saveAccount(Settings& s, const AccountSettings& acc, bool storePassword);

}  // namespace corasip
