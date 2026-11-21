// Bridge between the SIP engine and the Qt window (M3). PJSIP calls these events on its own threads, so
// every event is turned into a Qt signal; Qt delivers signals to the window on the GUI thread.
#pragma once

#include "engine/engine.hpp"

#include <QObject>
#include <QString>

namespace corasip::ui {

class PhoneEngine : public QObject, public EngineEvents {
    Q_OBJECT
public:
    explicit PhoneEngine(QObject* parent = nullptr);
    ~PhoneEngine() override;

    Engine& engine() { return engine_; }

    // EngineEvents, called on PJSIP threads.
    void onRegistration(bool registered, int sipCode, const std::string& reason) override;
    void onCallState(int callId, const std::string& state) override;
    void onIncomingCall(int callId, const std::string& remoteUri) override;
    void onAccountStatus(const std::string& text) override;
    void onCallStarted(int callId, bool incoming, const std::string& remoteUri) override;
    void onCallEnded(int callId, int sipCode, bool wasAnswered) override;
    void onTransferStatus(int callId, int sipCode, bool finalNotify) override;

signals:
    void accountStatus(const QString& text);
    void callStarted(int callId, bool incoming, const QString& remote);
    void callState(int callId, const QString& state);
    void callEnded(int callId, int sipCode, bool wasAnswered);
    void registrationFailed(const QString& reason);
    void transferStatus(int callId, int sipCode, bool finalNotify);

private:
    Engine engine_;
};

}  // namespace corasip::ui
