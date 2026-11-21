#include "ui/phone_engine.hpp"

namespace corasip::ui {

PhoneEngine::PhoneEngine(QObject* parent) : QObject(parent), engine_(*this) {}

PhoneEngine::~PhoneEngine() = default;

void PhoneEngine::onRegistration(bool registered, int sipCode, const std::string& reason) {
    if (!registered && sipCode != 0) {
        emit registrationFailed(QString::fromStdString(reason.empty() ? std::to_string(sipCode) : reason));
    }
}

void PhoneEngine::onCallState(int callId, const std::string& state) {
    emit callState(callId, QString::fromStdString(state));
}

void PhoneEngine::onIncomingCall(int, const std::string&) {
    // The window learns about the call from onCallStarted; nothing extra to do here.
}

void PhoneEngine::onAccountStatus(const std::string& text) {
    emit accountStatus(QString::fromStdString(text));
}

void PhoneEngine::onCallStarted(int callId, bool incoming, const std::string& remoteUri) {
    emit callStarted(callId, incoming, QString::fromStdString(remoteUri));
}

void PhoneEngine::onTransferStatus(int callId, int sipCode, bool finalNotify) {
    emit transferStatus(callId, sipCode, finalNotify);
}

void PhoneEngine::onCallEnded(int callId, int sipCode, bool wasAnswered) {
    emit callEnded(callId, sipCode, wasAnswered);
}

}  // namespace corasip::ui
