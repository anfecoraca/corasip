// corasip SIP engine wrapper implementation (M1 step 5). PJSUA2 is used only in this file.
#include "engine/engine.hpp"
#include "engine/sip_mask.hpp"

#include <pjlib.h>
#include <pjsua2.hpp>

#include <pjsip.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <vector>

namespace corasip {

namespace {

std::string describe(const pj::Error& e) {
    std::string s = e.title;
    if (!e.reason.empty()) {
        s += ": " + e.reason;
    }
    return s;
}

// PJSIP requires every thread that calls into it to be registered. Application threads are
// registered on first use. Callbacks run on PJSIP worker threads, which are already registered.
// pjlib (debug builds) asserts when an unregistered thread calls into it, and that includes the
// first libCreate() on the main thread. So the calling thread is registered with the pjlib C API
// first. The descriptor is thread_local because pjlib keeps a pointer to it for the thread's life.
void ensureThreadRegistered() {
    if (pj_thread_is_registered()) return;
    thread_local pj_thread_desc desc;
    thread_local pj_thread_t* thread = nullptr;
    pj_thread_register("corasip-api", desc, &thread);
}

// Picks the first device whose name contains `wanted`. Empty `wanted` keeps the system default.
void selectDevice(pj::Endpoint& ep, const std::string& wanted, bool capture) {
    if (wanted.empty()) return;
    pj::AudDevManager& dev = ep.audDevManager();
    for (const auto& d : dev.enumDev2()) {
        // Devices that report no channels (PipeWire's pulse route) are tried; the open call decides.
        const bool unknown = d.inputCount == 0 && d.outputCount == 0;
        const bool usable = unknown || (capture ? d.inputCount > 0 : d.outputCount > 0);
        if (usable && d.name.find(wanted) != std::string::npos) {
            if (capture) dev.setCaptureDev(d.id); else dev.setPlaybackDev(d.id);
            return;
        }
    }
    pj::Error e;  // the device the user asked for does not exist or cannot carry this direction
    e.title = std::string(capture ? "capture" : "playback") + " device not usable: " + wanted;
    throw e;
}

// Default offer, most preferred first. Opus is preferred for quality; G.711 is the fallback every PBX has.
const std::vector<std::string>& defaultCodecs() {
    static const std::vector<std::string> list = {"opus/48000/2", "G722/16000/1", "PCMA/8000/1", "PCMU/8000/1"};
    return list;
}

// Gives the listed codecs priorities in order and disables every other codec. Throws when a listed
// codec does not exist in this PJSIP build.
void applyCodecs(pj::Endpoint& ep, const std::vector<std::string>& wanted) {
    const auto codecs = ep.codecEnum2();
    for (const auto& name : wanted) {
        bool found = false;
        for (const auto& c : codecs) {
            if (c.codecId == name) { found = true; break; }
        }
        if (!found) {
            pj::Error e;
            e.title = "codec not available in this build: " + name;
            throw e;
        }
    }
    for (const auto& c : codecs) {
        std::uint8_t priority = 0;  // disabled unless listed
        for (std::size_t i = 0; i < wanted.size(); ++i) {
            if (wanted[i] == c.codecId) {
                priority = static_cast<std::uint8_t>(250 - 10 * i);  // strictly decreasing, all above 0
                break;
            }
        }
        ep.codecSetPriority(c.codecId, priority);
    }
}

const char* stateName(pjsip_inv_state st) {
    switch (st) {
        case PJSIP_INV_STATE_NULL: return "null";
        case PJSIP_INV_STATE_CALLING: return "calling";
        case PJSIP_INV_STATE_INCOMING: return "incoming";
        case PJSIP_INV_STATE_EARLY: return "early";
        case PJSIP_INV_STATE_CONNECTING: return "connecting";
        case PJSIP_INV_STATE_CONFIRMED: return "confirmed";
        case PJSIP_INV_STATE_DISCONNECTED: return "disconnected";
        default: return "unknown";
    }
}

}  // namespace

struct Engine::Impl {
    EngineEvents& events;
    pj::Endpoint endpoint;  // PJSUA2 needs the application to own the endpoint object
    bool started = false;
    EngineConfig config;  // fixed at start(): TLS transports are created with it
    std::mutex mu;

    class Call;

    // Account subclass: forwards registration and incoming-call events.
    class Account : public pj::Account {
    public:
        explicit Account(Impl& owner) : owner_(owner) {}
        void onRegState(pj::OnRegStateParam& prm) override {
            try {
                const int code = static_cast<int>(prm.code);
                const bool ok = prm.code / 100 == 2;  // 2xx means registered
                owner_.events.onRegistration(ok, code, prm.reason);
                owner_.events.onAccountStatus(statusWord(code));
            } catch (...) {
                // Never let an exception unwind into PJSIP.
            }
        }
        // the reference softphone wording for the account state.
        static std::string statusWord(int code) {
            if (code / 100 == 2) return "Online";
            if (code == 408) return "Request Timeout";
            if (code == 0 || code / 100 == 1) return "Connecting...";
            return "Offline";
        }
        void onIncomingCall(pj::OnIncomingCallParam& prm) override {
            try {
                auto call = std::make_unique<Call>(owner_, *this, prm.callId);
                const int id = prm.callId;
                const std::string from = call->getInfo().remoteUri;
                call->incoming = true;
                call->ringing = true;
                owner_.ringStart();
                owner_.adopt(id, std::move(call));
                owner_.events.onCallStarted(id, true, from);
                owner_.events.onIncomingCall(id, from);
            } catch (...) {
            }
        }
    private:
        Impl& owner_;
    };

    // Call subclass: forwards state and connects audio when media becomes active.
    class Call : public pj::Call {
    public:
        Call(Impl& owner, pj::Account& acc, int callId) : pj::Call(acc, callId), owner_(owner) {}
        bool incoming = false;
        bool ringing = false;
        bool answered = false;
        bool muted = false;
        pj::AudioMedia* audio = nullptr;  // the call's audio port, set when media becomes active

        void onCallState(pj::OnCallStateParam&) override {
            try {
                const pj::CallInfo ci = getInfo();
                if (ci.state == PJSIP_INV_STATE_CONFIRMED) {
                    answered = true;
                    stopRinging();
                }
                if (ci.state == PJSIP_INV_STATE_DISCONNECTED) {
                    stopRinging();
                    owner_.events.onCallEnded(ci.id, static_cast<int>(ci.lastStatusCode), answered);
                }
                // A confirmed session can still have no audio: a media line rejected by the answer
                // (no common codec) leaves the session confirmed with port 0. Report it, do not hide it.
                if (ci.state == PJSIP_INV_STATE_CONFIRMED && !hasActiveAudio(ci)) {
                    owner_.events.onCallState(ci.id, "no-media");
                    return;
                }
                owner_.events.onCallState(ci.id, stateName(ci.state));
            } catch (...) {
            }
        }
        void stopRinging() {
            if (ringing) {
                ringing = false;
                owner_.ringStop();
            }
        }
        static bool hasActiveAudio(const pj::CallInfo& ci) {
            for (const auto& m : ci.media) {
                if (m.type == PJMEDIA_TYPE_AUDIO && m.status == PJSUA_CALL_MEDIA_ACTIVE) return true;
            }
            return false;
        }
        void onCallTransferStatus(pj::OnCallTransferStatusParam& prm) override {
            try {
                const int code = static_cast<int>(prm.statusCode);
                owner_.events.onTransferStatus(static_cast<int>(getId()), code, prm.finalNotify);
                if (code / 100 == 2) {
                    // The transfer succeeded: this side has left the call. PJSIP does not do this itself.
                    pj::CallOpParam op(true);
                    hangup(op);
                }
            } catch (...) {
            }
        }
        void onCallMediaState(pj::OnCallMediaStateParam&) override {
            try {
                const pj::CallInfo ci = getInfo();
                for (unsigned i = 0; i < ci.media.size(); ++i) {
                    if (ci.media[i].type != PJMEDIA_TYPE_AUDIO) continue;
                    if (ci.media[i].status != PJSUA_CALL_MEDIA_ACTIVE) continue;
                    pj::AudioMedia* am = pj::AudioMedia::typecastFromMedia(getMedia(i));
                    if (am == nullptr) continue;
                    audio = am;
                    pj::AudDevManager& dev = owner_.endpoint.audDevManager();
                    am->startTransmit(dev.getPlaybackDevMedia());
                    if (!muted) {  // a mute set before media came up is honoured here
                        if (owner_.tonePlayer) {
                            owner_.tonePlayer->startTransmit(*am);
                        } else {
                            dev.getCaptureDevMedia().startTransmit(*am);
                        }
                    }
                }
            } catch (...) {
            }
        }
    private:
        Impl& owner_;
    };

    std::unique_ptr<pj::AudioMediaPlayer> tonePlayer;   // test: replaces the microphone when set
    std::unique_ptr<pj::AudioMediaPlayer> ringer;       // ringtone for incoming calls
    std::unique_ptr<pj::AudioMediaPlayer> keyTone;      // local feedback for the last key pressed
    std::string dtmfDir;
    int ringingCalls = 0;                                // ringer plays while this is above zero

    // Start or stop the ringtone. The count keeps it playing while any incoming call still rings.
    void ringStart() {
        if (!ringer || ringingCalls++ > 0) return;
        ringer->startTransmit(endpoint.audDevManager().getPlaybackDevMedia());
    }
    void ringStop() {
        if (!ringer || ringingCalls <= 0) return;
        if (--ringingCalls == 0) {
            ringer->stopTransmit(endpoint.audDevManager().getPlaybackDevMedia());
        }
    }

    std::unique_ptr<Account> account;
    Transport accountTransport = Transport::Udp;  // the transport the account registered on; calls use the same one
    std::map<int, std::unique_ptr<Call>> calls;

    // Looks a call up under the lock and returns it without the lock. PJSIP may run callbacks
    // synchronously inside the call that is made on the returned pointer, and those callbacks may
    // call back into the engine, so the lock must not be held across a PJSUA2 call.
    Call* findCall(int id) {
        std::lock_guard<std::mutex> lock(mu);
        auto it = calls.find(id);
        return it == calls.end() ? nullptr : it->second.get();
    }
    std::map<Transport, pj::TransportId> transports;

    explicit Impl(EngineEvents& ev) : events(ev) {}

    void adopt(int id, std::unique_ptr<Call> call) {
        std::lock_guard<std::mutex> lock(mu);
        calls[id] = std::move(call);
    }

    // Remove calls that PJSIP has finished with. Called from the API, never from a callback.
    void reapDisconnected() {
        // Read the states without the lock (PJSUA may call back into the engine), then erase under it.
        std::vector<std::pair<int, Call*>> all;
        {
            std::lock_guard<std::mutex> lock(mu);
            for (auto& kv : calls) all.emplace_back(kv.first, kv.second.get());
        }
        std::vector<int> done;
        for (const auto& [id, c] : all) {
            bool finished = false;
            try {
                finished = c->getInfo().state == PJSIP_INV_STATE_DISCONNECTED;
            } catch (...) {
                finished = true;
            }
            if (finished) done.push_back(id);
        }
        std::lock_guard<std::mutex> lock(mu);
        for (int id : done) calls.erase(id);
    }
};

Engine::Engine(EngineEvents& events) : impl_(std::make_unique<Impl>(events)) {}

Engine::~Engine() { stop(); }

// ---- Trace (M6): every SIP message is copied into a bounded buffer by a PJSIP module. ----
namespace {

struct TraceEntry {
    std::string callId;   // SIP Call-ID of the dialog
    bool outgoing = false;
    std::string firstLine;
    std::string text;     // masked
};

std::mutex g_traceMu;
std::deque<TraceEntry> g_trace;
std::map<int, std::string> g_sipCallIds;  // engine call id -> SIP Call-ID, kept after the call ends
constexpr std::size_t kTraceMax = 400;

std::string callIdOf(pjsip_msg* msg) {
    if (msg == nullptr) return {};
    auto* cid = static_cast<pjsip_cid_hdr*>(pjsip_msg_find_hdr(msg, PJSIP_H_CALL_ID, nullptr));
    return cid == nullptr ? std::string() : std::string(cid->id.ptr, static_cast<std::size_t>(cid->id.slen));
}

void recordSip(bool outgoing, pjsip_msg* msg, const char* buf, std::size_t len) {
    if (buf == nullptr || len == 0) return;
    TraceEntry e;
    e.callId = callIdOf(msg);
    e.outgoing = outgoing;
    e.text = maskDigests(std::string(buf, len));
    e.firstLine = e.text.substr(0, e.text.find("\r\n"));
    std::lock_guard<std::mutex> lock(g_traceMu);
    g_trace.push_back(std::move(e));
    while (g_trace.size() > kTraceMax) g_trace.pop_front();
}

pj_bool_t traceRxRequest(pjsip_rx_data* rd) {
    recordSip(false, rd->msg_info.msg, static_cast<const char*>(rd->pkt_info.packet),
              static_cast<std::size_t>(rd->pkt_info.len));
    return PJ_FALSE;  // observe only: other modules still handle the message
}

pj_bool_t traceRxResponse(pjsip_rx_data* rd) { return traceRxRequest(rd); }

pj_status_t traceTxRequest(pjsip_tx_data* td) {
    // Outgoing messages are written to text after this hook runs: encode now (PJSIP reuses the text later).
    if (pjsip_tx_data_encode(td) == PJ_SUCCESS) {
        recordSip(true, td->msg, reinterpret_cast<const char*>(td->buf.start),
                  static_cast<std::size_t>(td->buf.cur - td->buf.start));
    }
    return PJ_SUCCESS;
}

pj_status_t traceTxResponse(pjsip_tx_data* td) { return traceTxRequest(td); }

pjsip_module g_traceModule = {
    nullptr, nullptr,                  // list links
    {const_cast<char*>("corasip-trace"), 13},
    -1,                                // id, set by PJSIP
    // Transport layer priority: this module sees incoming messages before the user-agent layer consumes them.
    PJSIP_MOD_PRIORITY_TRANSPORT_LAYER,
    nullptr, nullptr, nullptr, nullptr,  // load, start, stop, unload
    &traceRxRequest, &traceRxResponse,
    &traceTxRequest, &traceTxResponse,
    nullptr,                           // on_tsx_state
};

// Mean opinion score from latency, jitter and loss (simplified ITU-T G.107 E-model). An estimate only.
double estimateMos(double rttMs, double jitterMs, double lossPercent) {
    const double latency = rttMs / 2.0 + jitterMs * 2.0 + 10.0;
    double id = 0.024 * latency;
    if (latency > 177.3) id += 0.11 * (latency - 177.3);
    const double ie = 30.0 * std::log(1.0 + 15.0 * lossPercent / 100.0);  // G.711 loss impairment
    const double r = 93.2 - id - ie;
    if (r <= 0) return 1.0;
    if (r >= 100) return 4.5;
    return 1.0 + 0.035 * r + 7e-6 * r * (r - 60.0) * (100.0 - r);
}

}  // namespace

bool Engine::start(const EngineConfig& cfg, std::string& err) {
    if (impl_->started) return true;
    try {
        impl_->config = cfg;
        pj_init();
        ensureThreadRegistered();
        pj::Endpoint& ep = impl_->endpoint;
        ep.libCreate();
        pj::EpConfig epCfg;
        // Echo cancellation is on (M5B). 200 ms is PJSUA's default tail; set here so it cannot change silently.
        epCfg.medConfig.ecTailLen = 200;
        if (!cfg.stunServer.empty()) epCfg.uaConfig.stunServer.push_back(cfg.stunServer);
        epCfg.logConfig.level = cfg.logLevel;
        epCfg.logConfig.consoleLevel = cfg.logLevel;
        ep.libInit(epCfg);
        applyCodecs(ep, cfg.codecs.empty() ? defaultCodecs() : cfg.codecs);
        if (cfg.nullAudio) {
            ep.audDevManager().setNullDev();
        }
        ep.libStart();
        pjsip_endpt_register_module(pjsua_get_pjsip_endpt(), &g_traceModule);
        if (!cfg.nullAudio) {
            selectDevice(ep, cfg.captureDevice, true);
            selectDevice(ep, cfg.playbackDevice, false);
        }
        if (!cfg.txToneFile.empty()) {
            impl_->tonePlayer = std::make_unique<pj::AudioMediaPlayer>();
            impl_->tonePlayer->createPlayer(cfg.txToneFile, PJMEDIA_FILE_NO_LOOP);
        }
        impl_->dtmfDir = cfg.dtmfToneDir;
        if (!cfg.ringtoneFile.empty()) {
            impl_->ringer = std::make_unique<pj::AudioMediaPlayer>();
            impl_->ringer->createPlayer(cfg.ringtoneFile, 0);  // loops while ringing
        }
        impl_->started = true;
        return true;
    } catch (const pj::Error& e) {
        err = describe(e);
    } catch (...) {
        err = "unexpected error starting PJSIP";
    }
    try { impl_->endpoint.libDestroy(); } catch (...) {}
    return false;
}

void Engine::stop() {
    if (!impl_->started) return;
    ensureThreadRegistered();
    try {
        {
                std::vector<Impl::Call*> open;
            {
                std::lock_guard<std::mutex> lock(impl_->mu);
                for (auto& kv : impl_->calls) open.push_back(kv.second.get());
            }
            for (auto* c : open) {
                try { c->hangup(pj::CallOpParam(true)); } catch (...) {}
            }
            std::lock_guard<std::mutex> lock(impl_->mu);
            impl_->calls.clear();
        }
        impl_->account.reset();
        impl_->transports.clear();
        impl_->keyTone.reset();
        impl_->ringer.reset();
        impl_->tonePlayer.reset();
        pjsip_endpt_unregister_module(pjsua_get_pjsip_endpt(), &g_traceModule);
        impl_->endpoint.libDestroy();
    } catch (...) {
    }
    impl_->started = false;
}

bool Engine::registerAccount(const AccountSettings& acc, std::string& err) {
    ensureThreadRegistered();
    if (!impl_->started) { err = "engine not started"; return false; }
    try {
        pj::Endpoint& ep = impl_->endpoint;

        // Transport: created once per type, bound to an ephemeral local port.
        const Transport t = acc.transport;
        if (impl_->transports.find(t) == impl_->transports.end()) {
            pj::TransportConfig tc;
            tc.port = static_cast<unsigned>(impl_->config.sipPort);
            pjsip_transport_type_e type = PJSIP_TRANSPORT_UDP;
            if (t == Transport::Tcp) type = PJSIP_TRANSPORT_TCP;
            if (t == Transport::Tls) {
                type = PJSIP_TRANSPORT_TLS;
                // Verification is on unless the user opted out (SPEC §5.3). A TLS server whose
                // certificate is not in the CA file fails the handshake, and registration fails with it.
                tc.tlsConfig.CaListFile = impl_->config.tlsCaFile;
                tc.tlsConfig.certFile = impl_->config.tlsCertFile;
                tc.tlsConfig.privKeyFile = impl_->config.tlsKeyFile;
                tc.tlsConfig.verifyServer = impl_->config.tlsVerifyServer;
            }
            impl_->transports[t] = ep.transportCreate(type, tc);
        }

        pj::AccountConfig ac;
        ac.idUri = "sip:" + acc.user + "@" + acc.domain;
        const char* tp = t == Transport::Udp ? "udp" : (t == Transport::Tcp ? "tcp" : "tls");
        ac.regConfig.registrarUri = "sip:" + acc.server + ";transport=" + tp;
        ac.sipConfig.authCreds.push_back(pj::AuthCredInfo("digest", "*", acc.user, 0, acc.password));
        ac.sipConfig.transportId = impl_->transports[t];
        ac.regConfig.retryIntervalSec = 20;  // re-register after a failure, without user action (M3)
        // SPEC D13: SRTP off unless the user enables it for this account.
        ac.mediaConfig.srtpUse = acc.srtp ? PJMEDIA_SRTP_MANDATORY : PJMEDIA_SRTP_DISABLED;
        ac.mediaConfig.srtpSecureSignaling = acc.srtpRequireSecureSignaling ? 1 : 0;
        // M5C: ICE on for every account. Candidates include STUN reflexive ones when a STUN server is configured.
        ac.natConfig.iceEnabled = true;

        // One active account (SPEC D12): replace any existing one.
        impl_->account.reset();
        impl_->account = std::make_unique<Impl::Account>(*impl_);
        impl_->account->create(ac);
        impl_->accountTransport = acc.transport;
        return true;
    } catch (const pj::Error& e) {
        err = describe(e);
    } catch (...) {
        err = "unexpected error creating account";
    }
    return false;
}

bool Engine::unregisterAccount(std::string& err) {
    ensureThreadRegistered();
    try {
        impl_->account.reset();
        return true;
    } catch (const pj::Error& e) {
        err = describe(e);
    } catch (...) {
        err = "unexpected error removing account";
    }
    return false;
}

bool Engine::makeCall(const std::string& remoteUri, int& callId, std::string& err) {
    ensureThreadRegistered();
    if (!impl_->started || !impl_->account) { err = "no active account"; return false; }
    impl_->reapDisconnected();
    try {
        // A call must use the account's transport. Without ;transport= PJSIP picks UDP, which breaks TLS and SRTP.
        std::string target = remoteUri;
        const char* tp = impl_->accountTransport == Transport::Tls ? "tls"
                       : (impl_->accountTransport == Transport::Tcp ? "tcp" : nullptr);
        if (tp != nullptr && target.find(";transport=") == std::string::npos) {
            target += std::string(";transport=") + tp;
        }
        auto call = std::make_unique<Impl::Call>(*impl_, *impl_->account, PJSUA_INVALID_ID);
        pj::CallOpParam prm(true);
        call->makeCall(target, prm);
        callId = call->getId();
        impl_->adopt(callId, std::move(call));
        impl_->events.onCallStarted(callId, false, remoteUri);
        return true;
    } catch (const pj::Error& e) {
        err = describe(e);
    } catch (...) {
        err = "unexpected error placing call";
    }
    return false;
}

bool Engine::answer(int callId, std::string& err) {
    ensureThreadRegistered();
    Impl::Call* call = impl_->findCall(callId);
    if (call == nullptr) { err = "no such call"; return false; }
    try {
        pj::CallOpParam prm(true);
        prm.statusCode = PJSIP_SC_OK;
        call->answer(prm);
        return true;
    } catch (const pj::Error& e) {
        err = describe(e);
    } catch (...) {
        err = "unexpected error answering call";
    }
    return false;
}

bool Engine::hangup(int callId, std::string& err) {
    ensureThreadRegistered();
    Impl::Call* call = impl_->findCall(callId);
    if (call == nullptr) { err = "no such call"; return false; }
    try {
        pj::CallOpParam prm(true);
        call->hangup(prm);
        return true;
    } catch (const pj::Error& e) {
        err = describe(e);
    } catch (...) {
        err = "unexpected error hanging up";
    }
    return false;
}

bool Engine::reject(int callId, std::string& err) {
    Impl::Call* call = impl_->findCall(callId);
    if (call == nullptr) { err = "no such call"; return false; }
    try {
        pj::CallOpParam prm(true);
        prm.statusCode = PJSIP_SC_DECLINE;
        call->hangup(prm);
        return true;
    } catch (const pj::Error& e) {
        err = describe(e);
    } catch (...) {
        err = "unexpected error rejecting call";
    }
    return false;
}

bool Engine::setHold(int callId, bool on, std::string& err) {
    Impl::Call* call = impl_->findCall(callId);
    if (call == nullptr) { err = "no such call"; return false; }
    try {
        pj::CallOpParam prm(true);
        if (on) {
            call->setHold(prm);
        } else {
            prm.opt.flag |= PJSUA_CALL_UNHOLD;
            call->reinvite(prm);
        }
        return true;
    } catch (const pj::Error& e) {
        err = describe(e);
    } catch (...) {
        err = "unexpected error changing hold";
    }
    return false;
}

bool Engine::transferBlind(int callId, const std::string& target, std::string& err) {
    Impl::Call* call = impl_->findCall(callId);
    if (call == nullptr) { err = "no such call"; return false; }
    try {
        pj::CallOpParam prm(true);
        call->xfer(target, prm);
        return true;
    } catch (const pj::Error& e) {
        err = describe(e);
    } catch (...) {
        err = "unexpected error transferring call";
    }
    return false;
}

bool Engine::transferAttended(int callId, int targetCallId, std::string& err) {
    if (callId == targetCallId) { err = "cannot transfer a call to itself"; return false; }
    // Look both calls up under the lock, then release it before PJSUA is called (ARCHITECTURE "Threading").
    Impl::Call* call = impl_->findCall(callId);
    Impl::Call* target = impl_->findCall(targetCallId);
    if (call == nullptr || target == nullptr) { err = "no such call"; return false; }
    try {
        pj::CallOpParam prm(true);
        call->xferReplaces(*target, prm);
        return true;
    } catch (const pj::Error& e) {
        err = describe(e);
    } catch (...) {
        err = "unexpected error transferring call";
    }
    return false;
}

bool Engine::setMute(int callId, bool on, std::string& err) {
    Impl::Call* call = impl_->findCall(callId);
    if (call == nullptr) { err = "no such call"; return false; }
    Impl::Call& ref = *call;
    ref.muted = on;
    if (ref.audio == nullptr) return true;  // applied when media comes up
    try {
        pj::AudDevManager& dev = impl_->endpoint.audDevManager();
        pj::AudioMedia& source = impl_->tonePlayer ? static_cast<pj::AudioMedia&>(*impl_->tonePlayer)
                                                   : dev.getCaptureDevMedia();
        if (on) {
            source.stopTransmit(*ref.audio);
        } else {
            source.startTransmit(*ref.audio);
        }
        return true;
    } catch (const pj::Error& e) {
        err = describe(e);
    } catch (...) {
        err = "unexpected error changing mute";
    }
    return false;
}

bool Engine::sendDtmf(int callId, const std::string& digits, std::string& err) {
    Impl::Call* call = impl_->findCall(callId);
    if (call == nullptr) { err = "no such call"; return false; }
    try {
        call->dialDtmf(digits);
        return true;
    } catch (const pj::Error& e) {
        err = describe(e);
    } catch (...) {
        err = "unexpected error sending DTMF";
    }
    return false;
}

bool Engine::playKeyTone(const std::string& key, std::string& err) {
    if (!impl_->started) { err = "engine not started"; return false; }
    if (impl_->dtmfDir.empty()) { err = "no key tones configured"; return false; }
    std::string name;
    if (key == "*") name = "star";
    else if (key == "#") name = "hash";
    else if (key.size() == 1 && key[0] >= '0' && key[0] <= '9') name = key;
    else { err = "not a keypad key: " + key; return false; }
    ensureThreadRegistered();
    try {
        pj::AudDevManager& dev = impl_->endpoint.audDevManager();
        // One feedback tone at a time: a new key cuts off the previous one.
        if (impl_->keyTone) {
            try { impl_->keyTone->stopTransmit(dev.getPlaybackDevMedia()); } catch (...) {}
            impl_->keyTone.reset();
        }
        auto player = std::make_unique<pj::AudioMediaPlayer>();
        player->createPlayer(impl_->dtmfDir + "/" + name + ".wav", PJMEDIA_FILE_NO_LOOP);
        player->startTransmit(dev.getPlaybackDevMedia());
        impl_->keyTone = std::move(player);
        return true;
    } catch (const pj::Error& e) {
        err = describe(e);
    } catch (...) {
        err = "unexpected error playing key tone";
    }
    return false;
}

bool Engine::setCodecs(const std::vector<std::string>& codecs, std::string& err) {
    if (!impl_->started) { err = "engine not started"; return false; }
    {
        std::lock_guard<std::mutex> lock(impl_->mu);
        if (!impl_->calls.empty()) { err = "a call is active"; return false; }
    }
    ensureThreadRegistered();
    try {
        applyCodecs(impl_->endpoint, codecs.empty() ? defaultCodecs() : codecs);
        return true;
    } catch (const pj::Error& e) {
        err = describe(e);
    } catch (...) {
        err = "unexpected error setting codecs";
    }
    return false;
}

bool Engine::setAudioDevices(const std::string& capture, const std::string& playback, std::string& err) {
    if (!impl_->started) { err = "engine not started"; return false; }
    {
        std::lock_guard<std::mutex> lock(impl_->mu);
        if (!impl_->calls.empty()) { err = "a call is active"; return false; }
    }
    ensureThreadRegistered();
    try {
        pj::AudDevManager& dev = impl_->endpoint.audDevManager();
        if (capture.empty()) dev.setCaptureDev(PJMEDIA_AUD_DEFAULT_CAPTURE_DEV);
        else selectDevice(impl_->endpoint, capture, true);
        if (playback.empty()) dev.setPlaybackDev(PJMEDIA_AUD_DEFAULT_PLAYBACK_DEV);
        else selectDevice(impl_->endpoint, playback, false);
        return true;
    } catch (const pj::Error& e) {
        err = describe(e);
    } catch (...) {
        err = "unexpected error changing audio devices";
    }
    return false;
}

bool Engine::sipTrace(int callId, std::vector<SipTraceLine>& out, std::string& err) {
    out.clear();
    if (!impl_->started) { err = "engine not started"; return false; }
    ensureThreadRegistered();
    std::string id;
    pjsua_call_info info;
    if (pjsua_call_get_info(callId, &info) == PJ_SUCCESS) {
        id.assign(info.call_id.ptr, static_cast<std::size_t>(info.call_id.slen));
        std::lock_guard<std::mutex> lock(g_traceMu);
        g_sipCallIds[callId] = id;
    } else {
        // The call has ended: its messages are still in the buffer, found by the Call-ID remembered while it lived.
        std::lock_guard<std::mutex> lock(g_traceMu);
        auto it = g_sipCallIds.find(callId);
        if (it == g_sipCallIds.end()) { err = "no such call"; return false; }
        id = it->second;
    }
    std::lock_guard<std::mutex> lock(g_traceMu);
    for (const auto& e : g_trace) {
        if (e.callId != id) continue;
        out.push_back(SipTraceLine{e.outgoing, e.firstLine, e.text});
    }
    return true;
}

bool Engine::callStats(int callId, CallStats& out, std::string& err) {
    out = CallStats();
    if (!impl_->started) { err = "engine not started"; return false; }
    ensureThreadRegistered();
    pjsua_stream_stat st;
    if (pjsua_call_get_stream_stat(callId, 0, &st) != PJ_SUCCESS) { err = "no audio stream yet"; return false; }
    pjsua_stream_info si;
    if (pjsua_call_get_stream_info(callId, 0, &si) == PJ_SUCCESS && si.type == PJMEDIA_TYPE_AUDIO) {
        out.codec = std::string(si.info.aud.fmt.encoding_name.ptr,
                                static_cast<std::size_t>(si.info.aud.fmt.encoding_name.slen));
    }
    out.packetsSent = st.rtcp.tx.pkt;
    out.packetsReceived = st.rtcp.rx.pkt;
    out.packetsLost = st.rtcp.rx.loss;
    const double expected = static_cast<double>(out.packetsReceived) + out.packetsLost;
    out.lossPercent = expected > 0 ? 100.0 * out.packetsLost / expected : 0.0;
    out.jitterMs = st.rtcp.rx.jitter.mean / 1000.0;   // PJSIP keeps microseconds
    out.rttMs = st.rtcp.rtt.mean / 1000.0;
    out.mos = estimateMos(out.rttMs, out.jitterMs, out.lossPercent);
    out.valid = true;
    return true;
}

bool Engine::listAudioDevices(std::vector<std::string>& names, std::string& err) {
    if (!impl_->started) { err = "engine not started"; return false; }
    ensureThreadRegistered();
    try {
        names.clear();
        for (const auto& d : impl_->endpoint.audDevManager().enumDev2()) {
            names.push_back(d.name + " [in " + std::to_string(d.inputCount) + ", out " + std::to_string(d.outputCount) + "]");
        }
        return true;
    } catch (const pj::Error& e) {
        err = describe(e);
    } catch (...) {
        err = "unexpected error listing audio devices";
    }
    return false;
}

bool Engine::listCodecs(std::vector<std::string>& names, std::string& err) {
    if (!impl_->started) { err = "engine not started"; return false; }
    ensureThreadRegistered();
    try {
        names.clear();
        for (const auto& c : impl_->endpoint.codecEnum2()) {
            names.push_back(c.codecId);
        }
        return true;
    } catch (const pj::Error& e) {
        err = describe(e);
    } catch (...) {
        err = "unexpected error listing codecs";
    }
    return false;
}

}  // namespace corasip
