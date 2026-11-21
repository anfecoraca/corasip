// M4 transfer test helper (dev only). The transferor calls party B, then either:
//   blind:    sends B to party C (transferor leaves the call), or
//   attended: holds B, calls C, then connects B to C (transferor leaves both).
// Usage: transfer_call <host:port> <blind|attended> <B uri> <C uri>
// Environment: CORASIP_NULL_AUDIO=1 for tests; CORASIP_USER, CORASIP_DOMAIN, CORASIP_PASSWORD as engine_call.
#include "engine/engine.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>

namespace {

class Listener : public corasip::EngineEvents {
public:
    void onRegistration(bool registered, int code, const std::string&) override {
        std::printf("registration: %s (code %d)\n", registered ? "ok" : "failed", code);
        std::fflush(stdout);
    }
    void onCallState(int callId, const std::string& state) override {
        std::printf("call %d: %s\n", callId, state.c_str());
        std::fflush(stdout);
        std::lock_guard<std::mutex> lock(mu_);
        if (state == "confirmed") confirmed_.insert(callId);
        if (state == "disconnected") disconnected_.insert(callId);
        cv_.notify_all();
    }
    // The transferor places its own calls; an incoming call is not expected, so it is only logged.
    void onIncomingCall(int callId, const std::string& from) override {
        std::printf("incoming call %d from %s (not expected)\n", callId, from.c_str());
        std::fflush(stdout);
    }

    bool waitConfirmed(int id, std::chrono::seconds timeout) {
        std::unique_lock<std::mutex> lock(mu_);
        return cv_.wait_for(lock, timeout, [&] { return confirmed_.count(id) > 0; });
    }
    bool waitDisconnected(int id, std::chrono::seconds timeout) {
        std::unique_lock<std::mutex> lock(mu_);
        return cv_.wait_for(lock, timeout, [&] { return disconnected_.count(id) > 0; });
    }

private:
    std::mutex mu_;
    std::condition_variable cv_;
    std::set<int> confirmed_;
    std::set<int> disconnected_;
};

const char* envOr(const char* name, const char* fallback) {
    const char* v = std::getenv(name);
    return (v != nullptr && *v != '\0') ? v : fallback;
}

int finish(corasip::Engine& engine, int rc) {
    engine.stop();
    return rc;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: %s <host:port> <blind|attended> <B uri> <C uri>\n", argv[0]);
        return 1;
    }
    const std::string kind = argv[2];
    if (kind != "blind" && kind != "attended") { std::fprintf(stderr, "kind must be blind or attended\n"); return 1; }

    corasip::EngineConfig cfg;
    cfg.nullAudio = envOr("CORASIP_NULL_AUDIO", "0")[0] == '1';
    cfg.codecs = {"PCMU/8000/1"};

    Listener events;
    corasip::Engine engine(events);
    std::string err;
    if (!engine.start(cfg, err)) { std::printf("ERROR start: %s\n", err.c_str()); return 1; }

    corasip::AccountSettings acc;
    acc.user = envOr("CORASIP_USER", "transferor");
    acc.domain = envOr("CORASIP_DOMAIN", "127.0.0.1");
    acc.password = envOr("CORASIP_PASSWORD", "not-a-real-password");
    acc.server = argv[1];
    acc.transport = corasip::Transport::Udp;
    acc.srtp = false;
    if (!engine.registerAccount(acc, err)) { std::printf("ERROR register: %s\n", err.c_str()); return finish(engine, 1); }
    std::this_thread::sleep_for(std::chrono::seconds(2));

    int callB = -1;
    if (!engine.makeCall(argv[3], callB, err)) { std::printf("ERROR call B: %s\n", err.c_str()); return finish(engine, 1); }
    if (!events.waitConfirmed(callB, std::chrono::seconds(30))) { std::printf("ERROR B not confirmed\n"); return finish(engine, 1); }

    if (kind == "blind") {
        if (!engine.transferBlind(callB, argv[4], err)) { std::printf("ERROR blind transfer: %s\n", err.c_str()); return finish(engine, 1); }
        std::printf("transfer: blind sent\n");
        if (!events.waitDisconnected(callB, std::chrono::seconds(20))) {
            std::printf("ERROR transferor still in the call after blind transfer\n");
            return finish(engine, 1);
        }
        std::printf("PASS transfer blind (transferor left the call)\n");
        return finish(engine, 0);
    }

    // Attended: hold B, call C, connect B to C.
    if (!engine.setHold(callB, true, err)) { std::printf("ERROR hold B: %s\n", err.c_str()); return finish(engine, 1); }
    int callC = -1;
    if (!engine.makeCall(argv[4], callC, err)) { std::printf("ERROR call C: %s\n", err.c_str()); return finish(engine, 1); }
    if (!events.waitConfirmed(callC, std::chrono::seconds(30))) { std::printf("ERROR C not confirmed\n"); return finish(engine, 1); }
    if (!engine.transferAttended(callB, callC, err)) { std::printf("ERROR attended transfer: %s\n", err.c_str()); return finish(engine, 1); }
    std::printf("transfer: attended sent\n");
    if (!events.waitDisconnected(callB, std::chrono::seconds(20))) {
        std::printf("ERROR transferor still in call B after attended transfer\n");
        return finish(engine, 1);
    }
    std::printf("PASS transfer attended (transferor left both calls)\n");
    return finish(engine, 0);
}
