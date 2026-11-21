#include "app/account_validation.hpp"

#include "settings/settings.hpp"

namespace corasip {

namespace {

bool hasSpace(const std::string& s) { return s.find_first_of(" \t\r\n") != std::string::npos; }

}  // namespace

std::vector<std::string> validateAccount(const AccountSettings& acc) {
    std::vector<std::string> errors;
    // SPEC D13: SRTP keys travel in SDP, so SRTP needs TLS signalling. Over UDP or TCP the call would fail.
    if (acc.srtp && acc.transport != Transport::Tls) {
        errors.push_back("Encrypted media (SRTP) needs TLS. Choose TLS as the transport, or turn SRTP off.");
    }
    if (acc.user.empty()) errors.push_back("Username is required.");
    if (hasSpace(acc.user)) errors.push_back("Username cannot contain spaces.");
    if (acc.domain.empty()) errors.push_back("Domain is required.");
    if (hasSpace(acc.domain)) errors.push_back("Domain cannot contain spaces.");
    if (acc.server.empty()) errors.push_back("SIP server is required.");
    if (hasSpace(acc.server)) errors.push_back("SIP server cannot contain spaces.");
    if (acc.password.empty()) errors.push_back("Password is required.");
    return errors;
}

void loadAccount(const Settings& s, AccountSettings& acc) {
    acc.user = s.get("account", "username", acc.user);
    acc.domain = s.get("account", "domain", acc.domain);
    acc.server = s.get("account", "server", acc.server);
    acc.password = s.get("account", "password", acc.password);
    const std::string t = s.get("account", "transport", "udp");
    acc.transport = t == "tcp" ? Transport::Tcp : (t == "tls" ? Transport::Tls : Transport::Udp);
    acc.srtp = s.get("account", "srtp", "0") == "1";
    acc.srtpRequireSecureSignaling = s.get("account", "srtp_require_secure_signaling", "1") == "1";
}

void saveAccount(Settings& s, const AccountSettings& acc, bool storePassword) {
    s.set("account", "username", acc.user);
    s.set("account", "domain", acc.domain);
    s.set("account", "server", acc.server);
    const char* t = acc.transport == Transport::Tcp ? "tcp" : (acc.transport == Transport::Tls ? "tls" : "udp");
    s.set("account", "transport", t);
    s.set("account", "srtp", acc.srtp ? "1" : "0");
    s.set("account", "srtp_require_secure_signaling", acc.srtpRequireSecureSignaling ? "1" : "0");
    if (storePassword) {
        s.set("account", "password", acc.password);
    } else {
        s.erase("account", "password");
    }
}

}  // namespace corasip
