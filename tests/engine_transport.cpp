// M1 step 6: one registration over one transport, per process. Used by tests/transport/run.sh.
// Usage: engine_transport <udp|tcp|tls> <host:port> <ca-file | ->
// Exit 0: registered. Exit 2: not registered (expected for the negative TLS test). Exit 1: error.
#include "engine/engine.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace {

class Listener : public corasip::EngineEvents {
public:
    void onRegistration(bool registered, int sipCode, const std::string& reason) override {
        std::lock_guard<std::mutex> lock(mu_);
        got_ = true;
        registered_ = registered;
        code_ = sipCode;
        reason_ = reason;
        cv_.notify_all();
    }
    void onCallState(int, const std::string&) override {}
    void onIncomingCall(int, const std::string&) override {}

    bool wait(std::chrono::seconds timeout) {
        std::unique_lock<std::mutex> lock(mu_);
        return cv_.wait_for(lock, timeout, [this] { return got_; });
    }
    bool registered() { std::lock_guard<std::mutex> lock(mu_); return registered_; }
    int code() { std::lock_guard<std::mutex> lock(mu_); return code_; }
    std::string reason() { std::lock_guard<std::mutex> lock(mu_); return reason_; }

private:
    std::mutex mu_;
    std::condition_variable cv_;
    bool got_ = false;
    bool registered_ = false;
    int code_ = 0;
    std::string reason_;
};


// Lab runs supply credentials through the environment, so passwords never appear on a command line.
const char* envOr(const char* name, const char* fallback) {
    const char* v = std::getenv(name);
    return (v != nullptr && *v != '\0') ? v : fallback;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: %s <udp|tcp|tls> <host:port> <ca-file|->\n", argv[0]);
        return 1;
    }
    corasip::Transport transport;
    if (std::strcmp(argv[1], "udp") == 0) transport = corasip::Transport::Udp;
    else if (std::strcmp(argv[1], "tcp") == 0) transport = corasip::Transport::Tcp;
    else if (std::strcmp(argv[1], "tls") == 0) transport = corasip::Transport::Tls;
    else { std::fprintf(stderr, "unknown transport %s\n", argv[1]); return 1; }

    corasip::EngineConfig cfg;
    cfg.nullAudio = true;
    cfg.logLevel = 1;
    if (std::strcmp(argv[3], "-") != 0) cfg.tlsCaFile = argv[3];

    Listener events;
    corasip::Engine engine(events);
    std::string err;
    if (!engine.start(cfg, err)) { std::printf("ERROR start: %s\n", err.c_str()); return 1; }

    corasip::AccountSettings acc;
    acc.user = envOr("CORASIP_USER", "transport-test");
    acc.domain = envOr("CORASIP_DOMAIN", "127.0.0.1");
    acc.password = envOr("CORASIP_PASSWORD", "not-a-real-password");
    acc.server = argv[2];
    acc.transport = transport;
    acc.srtp = false;
    if (!engine.registerAccount(acc, err)) { std::printf("ERROR register: %s\n", err.c_str()); engine.stop(); return 1; }

    int rc = 1;
    if (!events.wait(std::chrono::seconds(30))) {
        std::printf("ERROR no registration event within 30s\n");
    } else if (events.registered()) {
        std::printf("REGISTERED %s %s (code %d)\n", argv[1], argv[2], events.code());
        rc = 0;
    } else {
        std::printf("NOT REGISTERED %s %s (code %d, %s)\n", argv[1], argv[2], events.code(), events.reason().c_str());
        rc = 2;
    }
    engine.stop();
    return rc;
}
