// corasip SIP engine wrapper (M1 step 5).
// Public header: no PJSIP types leak out. All PJSUA2 errors are caught inside engine.cpp and
// returned as bool + message. PJSIP callbacks never throw into PJSIP.
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace corasip {

enum class Transport { Udp, Tcp, Tls };

struct EngineConfig {
    bool nullAudio = false;        // true: no sound device (CI, smoke tests)
    std::string tlsCaFile;         // CA list for TLS server verification
    bool tlsVerifyServer = true;   // verification stays on unless the user opts out (SPEC §5.3)
    unsigned logLevel = 2;         // PJSIP console log level
    int sipPort = 0;               // local SIP port; 0 = ephemeral. Fixed ports are for tests and answering.
    std::string tlsCertFile;       // server certificate for incoming TLS
    std::string tlsKeyFile;        // its private key
    // Codecs to offer, most preferred first. Names as PJSIP writes them, e.g. "opus/48000/2",
    // "PCMU/8000/1". Empty: the default list (Opus, G.722, G.711 A-law, G.711 u-law).
    std::vector<std::string> codecs;
    std::string txToneFile;        // test: play this WAV into calls instead of the microphone
    std::string captureDevice;     // microphone, matched by name (see audio-devices). Empty: system default
    std::string playbackDevice;    // speaker, matched by name. Empty: system default
    std::string ringtoneFile;      // WAV played on incoming calls until answered or ended. Empty: silent
    std::string dtmfToneDir;       // folder with 0-9.wav, star.wav, hash.wav: local feedback for key presses
    // STUN server for reflexive (public) candidates, as host:port (M5C). Empty: host candidates only.
    std::string stunServer;
};

// One active account (SPEC D12). A second account is a separate process.
struct AccountSettings {
    std::string user;              // SIP username
    std::string domain;            // SIP domain, used in the account URI
    std::string password;          // never logged
    std::string server;            // registrar host or IP, optional port
    Transport transport = Transport::Udp;
    bool srtp = false;             // SPEC D13: off by default, per-account toggle
    // SRTP keys travel in SDP. With this on (default), SRTP needs TLS signalling and the call
    // fails over UDP or TCP instead of exposing the keys. Turn off only with a documented reason.
    bool srtpRequireSecureSignaling = true;
};

// One SIP message of a call, for the trace panel. Authorization digests are replaced by <hidden> before storage.
struct SipTraceLine {
    bool outgoing = false;
    std::string firstLine;   // "INVITE sip:...", "SIP/2.0 200 OK"
    std::string text;        // the whole message
};

// RTP statistics of a call. mos is an estimate (ITU-T G.107 style), not a measured listening score.
struct CallStats {
    std::string codec;
    bool valid = false;
    unsigned packetsSent = 0;
    unsigned packetsReceived = 0;
    unsigned packetsLost = 0;
    double lossPercent = 0;
    double jitterMs = 0;
    double rttMs = 0;
    double mos = 0;
};

// Events are delivered on PJSIP worker threads. Implementations must be quick and must not
// call back into Engine from inside the event.
class EngineEvents {
public:
    virtual ~EngineEvents() = default;
    virtual void onRegistration(bool registered, int sipCode, const std::string& reason) = 0;
    virtual void onCallState(int callId, const std::string& state) = 0;
    virtual void onIncomingCall(int callId, const std::string& remoteUri) = 0;
    // Optional events (M3). Defaults do nothing, so listeners that do not need them stay as they are.
    // Account status in the reference softphone wording: "Connecting...", "Online", "Request Timeout", "Offline".
    virtual void onAccountStatus(const std::string& /*text*/) {}
    // A call has started, outgoing or incoming. Used for the history and the call window.
    virtual void onCallStarted(int /*callId*/, bool /*incoming*/, const std::string& /*remoteUri*/) {}
    // A call has ended. sipCode is the last SIP status (200 normal, 486 busy, 603 declined, 487 cancelled).
    virtual void onCallEnded(int /*callId*/, int /*sipCode*/, bool /*wasAnswered*/) {}
    // Progress of a transfer started on this call (M4). A 2xx final status means the far end took the call
    // over, and the engine ends this side's call. Any other final status leaves the call up.
    virtual void onTransferStatus(int /*callId*/, int /*sipCode*/, bool /*finalNotify*/) {}
};

class Engine {
public:
    explicit Engine(EngineEvents& events);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Start the PJSIP endpoint. Returns false and sets err on failure.
    bool start(const EngineConfig& cfg, std::string& err);
    // Hang up calls, remove the account, destroy PJSIP. Safe to call twice.
    void stop();

    // Create or replace the single active account and start registration.
    bool registerAccount(const AccountSettings& acc, std::string& err);
    bool unregisterAccount(std::string& err);

    bool makeCall(const std::string& remoteUri, int& callId, std::string& err);
    bool answer(int callId, std::string& err);
    bool hangup(int callId, std::string& err);
    // Reject an incoming call with 603 Decline.
    bool reject(int callId, std::string& err);
    // Hold (on) or resume (off) a call.
    bool setHold(int callId, bool on, std::string& err);
    // Mute (on) or unmute (off) the microphone for a call.
    bool setMute(int callId, bool on, std::string& err);
    // Blind transfer: send the call to target (a SIP URI); the transferor leaves the call (SPEC D22 M4).
    bool transferBlind(int callId, const std::string& target, std::string& err);
    // Attended transfer: connect callId to targetCallId (its dialog is replaced). Needs two calls (SPEC D22).
    bool transferAttended(int callId, int targetCallId, std::string& err);
    // Send DTMF digits (0-9, *, #, A-D) on a call.
    bool sendDtmf(int callId, const std::string& digits, std::string& err);
    // Change the codec order while the engine runs (M5B). Same rules as setAudioDevices.
    bool setCodecs(const std::vector<std::string>& codecs, std::string& err);
    // Change the microphone and speaker while the engine runs (M5A). Empty name = system default.
    // Refused while a call is active, so no call's media is moved.
    bool setAudioDevices(const std::string& capture, const std::string& playback, std::string& err);
    // Trace panel (M6): the SIP requests and responses of a call (digests are masked), and its RTP statistics.
    bool sipTrace(int callId, std::vector<SipTraceLine>& out, std::string& err);
    bool callStats(int callId, CallStats& out, std::string& err);
    // Play the local tone for one key on the speakers only. The far end does not hear it.
    bool playKeyTone(const std::string& key, std::string& err);

    // Names of the sound devices PJSIP can see (microphone and speaker choices). Needs start().
    bool listAudioDevices(std::vector<std::string>& names, std::string& err);
    // Codec names this PJSIP build supports, as used in EngineConfig::codecs. Needs start().
    bool listCodecs(std::vector<std::string>& names, std::string& err);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace corasip
