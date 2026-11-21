// M1 step 7: loopback call between two processes, used by tests/call/run.sh.
// Usage (transport is udp or tls; cert and key are used for tls and ignored for udp):
//   engine_call answer <host:port> <listen-port> <srtp 0|1> <udp|tls> <cert> <key>
//   engine_call call   <host:port> <peer-uri>    <srtp 0|1> <udp|tls> <cert>
// Exit 0: call confirmed, media ran for a few seconds, hung up cleanly. Exit 1: failure.
#include "engine/engine.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
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
        lastState_ = state;
        if (state == "confirmed") confirmed_ = true;
        if (state == "no-media") noMedia_ = true;
        if (state == "disconnected") disconnected_ = true;
        cv_.notify_all();
    }
    void onIncomingCall(int callId, const std::string& from) override {
        std::printf("incoming call %d from %s\n", callId, from.c_str());
        std::fflush(stdout);
        std::lock_guard<std::mutex> lock(mu_);
        incomingId_ = callId;
        cv_.notify_all();
    }

    bool waitFor(std::chrono::seconds timeout, bool& flag) {
        std::unique_lock<std::mutex> lock(mu_);
        return cv_.wait_for(lock, timeout, [&] { return flag; });
    }
    // Wait until either flag is set (confirmed or disconnected, whichever comes first).
    bool waitEither(std::chrono::seconds timeout, bool& a, bool& b) {
        std::unique_lock<std::mutex> lock(mu_);
        return cv_.wait_for(lock, timeout, [&] { return a || b; });
    }
    bool waitIncoming(std::chrono::seconds timeout, int& id) {
        std::unique_lock<std::mutex> lock(mu_);
        const bool ok = cv_.wait_for(lock, timeout, [this] { return incomingId_ >= 0; });
        id = incomingId_;
        return ok;
    }
    bool confirmed_ = false;
    bool noMedia_ = false;
    bool disconnected_ = false;

private:
    std::mutex mu_;
    std::condition_variable cv_;
    std::string lastState_;
    int incomingId_ = -1;
};

int finish(corasip::Engine& engine, int rc) {
    engine.stop();
    return rc;
}


// Lab runs supply credentials through the environment, so passwords never appear on a command line.
const char* envOr(const char* name, const char* fallback) {
    const char* v = std::getenv(name);
    return (v != nullptr && *v != '\0') ? v : fallback;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 7) {
        std::fprintf(stderr, "usage: %s answer|call <host:port> <port|peer> <srtp> <udp|tls> <cert> [key]\n", argv[0]);
        return 1;
    }
    const bool useTls = std::strcmp(argv[5], "tls") == 0;
    const std::string mode = argv[1];
    const bool isAnswer = mode == "answer";
    if (!isAnswer && mode != "call") { std::fprintf(stderr, "mode must be answer or call\n"); return 1; }

    corasip::EngineConfig cfg;
    cfg.nullAudio = true;
    cfg.logLevel = static_cast<unsigned>(std::atoi(envOr("CORASIP_LOG_LEVEL", "1")));  // 4 shows SDP
    // Codec list, most preferred first, comma separated. Default G.711 u-law keeps the step 7 capture checks exact.
    {
        std::string list = envOr("CORASIP_CODECS", "PCMU/8000/1");
        for (std::size_t start = 0; start <= list.size();) {
            std::size_t end = list.find(',', start);
            if (end == std::string::npos) end = list.size();
            if (end > start) cfg.codecs.push_back(list.substr(start, end - start));
            start = end + 1;
        }
    }
    cfg.nullAudio = envOr("CORASIP_NULL_AUDIO", "0")[0] == '1';   // tests set 1; manual runs use real devices
    cfg.txToneFile = envOr("CORASIP_TONE_TX", "");                 // step 8: tone into the call
    cfg.captureDevice = envOr("CORASIP_CAPTURE_DEV", "");          // step 8: microphone name (manual runs)
    cfg.playbackDevice = envOr("CORASIP_PLAYBACK_DEV", "");        // step 8: speaker name (manual runs)
    cfg.tlsCaFile = envOr("CORASIP_TLS_CA", argv[6]);  // trust file for the server (M5D: the lab PBX certificate)
    cfg.stunServer = envOr("CORASIP_STUN", "");  // M5C: STUN host:port for NAT tests (empty: none)
    if (isAnswer) {
        cfg.sipPort = std::atoi(argv[3]);
        cfg.tlsCertFile = argv[6];
        cfg.tlsKeyFile = argc > 7 ? argv[7] : "";
    }

    Listener events;
    corasip::Engine engine(events);
    std::string err;
    if (!engine.start(cfg, err)) { std::printf("ERROR start: %s\n", err.c_str()); return 1; }

    corasip::AccountSettings acc;
    acc.user = envOr("CORASIP_USER", isAnswer ? "answer" : "caller");
    acc.domain = envOr("CORASIP_DOMAIN", "127.0.0.1");
    acc.password = envOr("CORASIP_PASSWORD", "not-a-real-password");
    acc.server = argv[2];
    acc.transport = useTls ? corasip::Transport::Tls : corasip::Transport::Udp;
    acc.srtp = std::atoi(argv[4]) != 0;  // SPEC D13 toggle, per account
    if (!engine.registerAccount(acc, err)) { std::printf("ERROR register: %s\n", err.c_str()); return finish(engine, 1); }

    int callId = -1;
    if (isAnswer) {
        const int waitSeconds = std::atoi(envOr("CORASIP_WAIT_SECONDS", "60"));  // how long to wait for a call
        if (!events.waitIncoming(std::chrono::seconds(waitSeconds), callId)) {
            std::printf("ERROR no incoming call within %ds\n", waitSeconds);
            return finish(engine, 1);
        }
        if (std::atoi(envOr("CORASIP_NO_ANSWER", "0")) != 0) {
            // M4 T6: ring and never answer. The caller hears ringing until it gives up or this side times out.
            std::printf("ringing, not answering\n");
            std::fflush(stdout);
            const bool ended = events.waitFor(std::chrono::seconds(120), events.disconnected_);
            std::printf("%s\n", ended ? "call ended while ringing" : "NOTE: no end within 120s");
            return finish(engine, 0);
        }
        if (std::atoi(envOr("CORASIP_REJECT", "0")) != 0) {
            if (!engine.reject(callId, err)) { std::printf("ERROR reject: %s\n", err.c_str()); return finish(engine, 1); }
            std::printf("rejected incoming call\n");
            events.waitFor(std::chrono::seconds(10), events.disconnected_);
            return finish(engine, 0);
        }
        if (!engine.answer(callId, err)) { std::printf("ERROR answer: %s\n", err.c_str()); return finish(engine, 1); }
    } else {
        if (!engine.makeCall(argv[3], callId, err)) { std::printf("ERROR call: %s\n", err.c_str()); return finish(engine, 1); }
    }

    if (!events.waitEither(std::chrono::seconds(30), events.confirmed_, events.disconnected_)) {
        std::printf("ERROR call not confirmed within 30s\n");
        return finish(engine, 1);
    }
    if (!events.confirmed_ && !events.noMedia_) {
        std::printf("ERROR call ended before it was confirmed\n");
        return finish(engine, 1);
    }
    if (events.noMedia_) {
        std::printf("ERROR call confirmed without audio (no common codec?)\n");
        if (!events.disconnected_) engine.hangup(callId, err);
        events.waitFor(std::chrono::seconds(10), events.disconnected_);
        return finish(engine, 1);
    }
    std::printf("media running (srtp=%d)\n", acc.srtp ? 1 : 0);
    std::fflush(stdout);
    if (std::atoi(envOr("CORASIP_FEATURES", "0")) != 0) {
        // M3: hold, resume, mute, unmute and DTMF during a live call. Each API must succeed and the call must stay up.
        auto check = [&](const char* name, bool ok, const std::string& why) {
            std::printf("%s: %s\n", name, ok ? "ok" : why.c_str());
            if (!ok || events.disconnected_) {
                std::printf("ERROR %s failed or the call dropped\n", name);
                engine.hangup(callId, err);
                finish(engine, 1);
                std::exit(1);
            }
        };
        check("hold", engine.setHold(callId, true, err), err);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        check("resume", engine.setHold(callId, false, err), err);
        check("mute", engine.setMute(callId, true, err), err);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        check("unmute", engine.setMute(callId, false, err), err);
        check("dtmf", engine.sendDtmf(callId, "5", err), err);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    if (!isAnswer && std::getenv("CORASIP_TRANSFER_TO") != nullptr) {
        // M4: this side transfers the call (blind transfer, REFER) to the target, then waits for the call to end.
        std::this_thread::sleep_for(std::chrono::seconds(3));
        const std::string target = envOr("CORASIP_TRANSFER_TO", "");
        if (!engine.transferBlind(callId, target, err)) {
            std::printf("ERROR transfer: %s\n", err.c_str());
            return finish(engine, 1);
        }
        std::printf("transfer requested to %s\n", target.c_str());
        std::fflush(stdout);
        const bool ended = events.waitFor(std::chrono::seconds(30), events.disconnected_);
        std::printf("%s transfer\n", ended ? "PASS: call left this side after the transfer" : "NOTE: this side still in the call after 30s");
        return finish(engine, ended ? 0 : 1);
    }
    std::this_thread::sleep_for(std::chrono::seconds(std::atoi(envOr("CORASIP_HOLD_SECONDS", "6"))));

    // If the far end already ended the call, there is nothing to hang up.
    if (!events.disconnected_ && !engine.hangup(callId, err)) {
        std::printf("ERROR hangup: %s\n", err.c_str());
        return finish(engine, 1);
    }
    if (!events.waitFor(std::chrono::seconds(10), events.disconnected_)) {
        std::printf("ERROR no disconnect within 10s\n");
        return finish(engine, 1);
    }
    if (std::getenv("CORASIP_TRACE_DUMP") != nullptr) {
        // Trace check (M6): the SIP messages of the ended call, as the trace panel reads them.
        std::vector<corasip::SipTraceLine> lines;
        if (engine.sipTrace(callId, lines, err)) {
            for (const auto& l : lines) std::printf("TRACE %s %s\n", l.outgoing ? "out" : "in ", l.firstLine.c_str());
        } else {
            std::printf("TRACE error: %s\n", err.c_str());
        }
    }
    std::printf("PASS call (srtp=%d)\n", acc.srtp ? 1 : 0);
    return finish(engine, 0);
}
