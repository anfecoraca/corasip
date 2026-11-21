// M1 step 5 smoke test. No SIP server needed: start the engine with a null audio device,
// register against an address where nothing listens, and check that a registration event
// arrives and that bad calls fail cleanly without crashing.
#include "engine/engine.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>

namespace {

class Listener : public corasip::EngineEvents {
public:
    void onRegistration(bool registered, int sipCode, const std::string& reason) override {
        std::lock_guard<std::mutex> lock(mu_);
        regEvents_++;
        lastRegistered_ = registered;
        lastCode_ = sipCode;
        lastReason_ = reason;
        cv_.notify_all();
    }
    void onCallState(int, const std::string&) override {}
    void onIncomingCall(int, const std::string&) override {}

    bool waitForRegistration(std::chrono::seconds timeout) {
        std::unique_lock<std::mutex> lock(mu_);
        return cv_.wait_for(lock, timeout, [this] { return regEvents_ > 0; });
    }
    bool lastRegistered() {
        std::lock_guard<std::mutex> lock(mu_);
        return lastRegistered_;
    }
    int lastCode() {
        std::lock_guard<std::mutex> lock(mu_);
        return lastCode_;
    }

private:
    std::mutex mu_;
    std::condition_variable cv_;
    int regEvents_ = 0;
    bool lastRegistered_ = false;
    int lastCode_ = 0;
    std::string lastReason_;
};

int fail(const char* what, const std::string& err) {
    std::printf("FAIL: %s%s%s\n", what, err.empty() ? "" : " - ", err.c_str());
    return 1;
}

}  // namespace

int main() {
    Listener events;
    corasip::Engine engine(events);
    std::string err;

    corasip::EngineConfig cfg;
    cfg.nullAudio = true;
    cfg.logLevel = 1;
    if (!engine.start(cfg, err)) return fail("engine start", err);

    // A call with no account must fail cleanly.
    int callId = -1;
    if (engine.makeCall("sip:bob@127.0.0.1", callId, err)) return fail("call without account should fail", "");
    std::printf("ok: call without account rejected (%s)\n", err.c_str());
    err.clear();

    // Register to a local port with no listener over UDP.
    corasip::AccountSettings acc;
    acc.user = "smoke";
    acc.domain = "127.0.0.1";
    acc.password = "not-a-real-password";
    acc.server = "127.0.0.1:5999";
    acc.transport = corasip::Transport::Udp;
    acc.srtp = false;  // SPEC D13 default
    if (!engine.registerAccount(acc, err)) return fail("register account", err);

    if (!events.waitForRegistration(std::chrono::seconds(45))) {
        return fail("no registration event within 45s", "");
    }
    if (events.lastRegistered()) return fail("should not register to an empty port", "");
    std::printf("ok: registration event received, sip code %d (not registered, as expected)\n", events.lastCode());

    engine.unregisterAccount(err);
    engine.stop();
    engine.stop();  // second stop must be safe
    std::printf("PASS: engine smoke test\n");
    return 0;
}
