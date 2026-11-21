// corasip-cli: M1 engine harness.
// Commands: (none) prints the version; "audio-devices" lists sound devices; "codecs" lists codec names.
#include "engine/engine.hpp"

#include <pjlib.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifndef CORASIP_VERSION
#define CORASIP_VERSION "unknown"
#endif

namespace {

class NoEvents : public corasip::EngineEvents {
public:
    void onRegistration(bool, int, const std::string&) override {}
    void onCallState(int, const std::string&) override {}
    void onIncomingCall(int, const std::string&) override {}
};

int listAudioDevices() {
    NoEvents events;
    corasip::Engine engine(events);
    corasip::EngineConfig cfg;
    cfg.logLevel = 1;
    std::string err;
    if (!engine.start(cfg, err)) {
        std::fprintf(stderr, "cannot start audio: %s\n", err.c_str());
        return 1;
    }
    std::vector<std::string> names;
    const bool ok = engine.listAudioDevices(names, err);
    engine.stop();
    if (!ok) {
        std::fprintf(stderr, "cannot list devices: %s\n", err.c_str());
        return 1;
    }
    for (std::size_t i = 0; i < names.size(); ++i) {
        std::printf("%zu  %s\n", i, names[i].c_str());
    }
    return 0;
}

int listCodecs() {
    NoEvents events;
    corasip::Engine engine(events);
    corasip::EngineConfig cfg;
    cfg.nullAudio = true;
    cfg.logLevel = 0;
    std::string err;
    if (!engine.start(cfg, err)) {
        std::fprintf(stderr, "cannot start engine: %s\n", err.c_str());
        return 1;
    }
    std::vector<std::string> names;
    const bool ok = engine.listCodecs(names, err);
    engine.stop();
    if (!ok) {
        std::fprintf(stderr, "cannot list codecs: %s\n", err.c_str());
        return 1;
    }
    for (const auto& n : names) std::printf("%s\n", n.c_str());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "audio-devices") == 0) {
        return listAudioDevices();
    }
    if (argc > 1 && std::strcmp(argv[1], "codecs") == 0) {
        return listCodecs();
    }
    std::printf("corasip-cli %s (PJSIP %s, static)\n", CORASIP_VERSION, pj_get_version());
    return 0;
}
