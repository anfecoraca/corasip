// Unit tests for the M2 core: settings file and account rules. No display needed.
#include "app/account_validation.hpp"
#include "settings/settings.hpp"

#include <sys/stat.h>
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

std::string scratchDir() {
    const char* base = std::getenv("CORASIP_TEST_TMP");
    std::string dir = std::string(base ? base : "/tmp") + "/corasip-unit-" + std::to_string(::getpid());
    return dir;
}

void testRoundTripAndMode(const std::string& dir) {
    const std::string path = dir + "/corasip.ini";
    corasip::Settings s;
    s.set("account", "username", "alice");
    s.set("account", "password", "secret-value");
    s.set("general", "language", "en");
    std::string err;
    CHECK(s.save(path, err));
    CHECK(err.empty());

    struct stat st {};
    CHECK(::stat(path.c_str(), &st) == 0);
    CHECK((st.st_mode & 0777) == 0600);  // credentials file is private

    corasip::Settings r;
    CHECK(r.load(path, err));
    CHECK(r.get("account", "username") == "alice");
    CHECK(r.get("account", "password") == "secret-value");
    CHECK(r.get("general", "language") == "en");
    CHECK(r.get("account", "missing", "dflt") == "dflt");
}

void testMissingFileIsDefaults(const std::string& dir) {
    corasip::Settings s;
    std::string err;
    CHECK(s.load(dir + "/does-not-exist.ini", err));
    CHECK(s.get("account", "username", "none") == "none");
}

void testAccountNotStoredWithoutPassword(const std::string& dir) {
    const std::string path = dir + "/nopw.ini";
    corasip::AccountSettings acc;
    acc.user = "bob";
    acc.domain = "example.test";
    acc.server = "sip.example.test";
    acc.password = "do-not-store";
    corasip::Settings s;
    corasip::saveAccount(s, acc, /*storePassword=*/false);
    std::string err;
    CHECK(s.save(path, err));
    corasip::Settings r;
    CHECK(r.load(path, err));
    CHECK(!r.has("account", "password"));
    CHECK(r.get("account", "username") == "bob");
}

void testValidation() {
    corasip::AccountSettings ok;
    ok.user = "alice";
    ok.domain = "sip.example.test";
    ok.server = "sip.example.test";
    ok.password = "x";
    CHECK(corasip::validateAccount(ok).empty());

    corasip::AccountSettings bad;
    CHECK(corasip::validateAccount(bad).size() == 4);  // user, domain, server, password

    corasip::AccountSettings spaced = ok;
    spaced.user = "a b";
    CHECK(!corasip::validateAccount(spaced).empty());

    // A server address with a space is refused with its own message, and nothing else is reported.
    corasip::AccountSettings spacedServer = ok;
    spacedServer.server = "sip example.test";
    const auto serverErrors = corasip::validateAccount(spacedServer);
    CHECK(serverErrors.size() == 1);
    CHECK(serverErrors.size() == 1 && serverErrors[0] == "SIP server cannot contain spaces.");

    // M5D: SRTP needs TLS signalling (SPEC D13). Over UDP or TCP the account is refused.
    corasip::AccountSettings srtpUdp = ok;
    srtpUdp.srtp = true;
    srtpUdp.transport = corasip::Transport::Udp;
    CHECK(!corasip::validateAccount(srtpUdp).empty());
    corasip::AccountSettings srtpTls = srtpUdp;
    srtpTls.transport = corasip::Transport::Tls;
    CHECK(corasip::validateAccount(srtpTls).empty());
}

}  // namespace

int main() {
    const std::string dir = scratchDir();
    ::mkdir(dir.c_str(), 0700);
    testRoundTripAndMode(dir);
    testMissingFileIsDefaults(dir);
    testAccountNotStoredWithoutPassword(dir);
    testValidation();
    if (failures == 0) std::printf("PASS: core unit tests\n");
    return failures == 0 ? 0 : 1;
}
