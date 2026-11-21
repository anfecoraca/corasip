// M3 regression: the Call Manager after a call ends. Drives the window with the same events the engine sends.
#include "storage/contacts.hpp"
#include "storage/history.hpp"
#include "ui/main_window.hpp"
#include "ui/phone_engine.hpp"

#include <QApplication>
#include <QListWidget>
#include <QListWidgetItem>

#include <cstdio>
#include <vector>
#include <cstdlib>
#include <string>
#include <unistd.h>

int main(int argc, char** argv) {
    setenv("QT_QPA_PLATFORM", "offscreen", 0);
    setenv("CORASIP_NULL_AUDIO", "1", 1);
    QApplication app(argc, argv);

    const char* dir = std::getenv("CORASIP_TEST_TMP");
    const std::string base = std::string(dir ? dir : "/tmp") + "/calllist-" + std::to_string(getpid());
    corasip::Settings s;
    s.set("account", "username", "1090");
    s.set("account", "domain", "192.0.2.10");
    s.set("account", "server", "192.0.2.10");
    s.set("general", "single_call_mode", "0");
    std::string err;
    s.save(base + ".ini", err);

    corasip::History history;
    history.open(base + ".db", err);
    corasip::ui::PhoneEngine phone;
    corasip::ContactStore contacts;
    contacts.open(base + ".db", err);
    corasip::ui::MainWindow window(s, base + ".ini", history, contacts, phone, "", "");
    window.show();
    app.processEvents();

    auto* list = window.findChild<QListWidget*>("", Qt::FindDirectChildrenOnly);
    for (auto* l : window.findChildren<QListWidget*>()) list = l;  // the Call Manager is the only list on the Phone tab
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) { std::printf("FAIL: %s\n", what); ++failures; }
    };
    auto rows = [&] { return list ? list->count() : -1; };

    // Outgoing call 1 connects.
    emit phone.callStarted(1, false, "sip:10@192.0.2.10");
    emit phone.callState(1, "confirmed");
    app.processEvents();
    check(rows() == 1, "one call after the outgoing call starts");

    // Incoming call 2 rings, is answered, call 1 goes on hold, call 2 is connected.
    emit phone.callStarted(2, true, "<sip:1091@192.0.2.10>");
    app.processEvents();
    emit phone.callState(2, "confirmed");
    app.processEvents();
    check(rows() == 2, "two calls listed");

    // Call 2 ends (the far end or a hang up).
    emit phone.callEnded(2, 200, true);
    app.processEvents();
    check(rows() == 1, "the ended call leaves the list");
    if (list && rows() == 1) {
        const QString text = list->item(0)->text();
        check(text.contains("10@"), "the remaining call is the first one");
    }

    // An outgoing call that never connected is recorded as failed, not dialled.
    emit phone.callStarted(3, false, "sip:1092@192.0.2.10");
    app.processEvents();
    emit phone.callEnded(3, 487, true);  // "answered" flag without a connect time
    app.processEvents();
    std::vector<corasip::CallRecord> hist;
    std::string herr;
    history.list(1, hist, herr);
    check(!hist.empty() && hist[0].outcome == "failed", "unconnected outgoing call is recorded as failed");

    std::remove((base + ".ini").c_str());
    std::remove((base + ".db").c_str());
    if (failures == 0) std::printf("PASS: call list after a call ends\n");
    return failures == 0 ? 0 : 1;
}
