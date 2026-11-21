// GUI smoke test (M2): builds the main window offscreen with an account already saved, and checks the layout.
#include "storage/contacts.hpp"
#include "storage/history.hpp"
#include "ui/main_window.hpp"
#include "ui/phone_engine.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTabWidget>

#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <string>

int main(int argc, char** argv) {
    setenv("QT_QPA_PLATFORM", "offscreen", 0);  // keep a platform chosen by the caller (wayland, xcb)
    QApplication app(argc, argv);

    const char* dir = std::getenv("CORASIP_TEST_TMP");
    const std::string path = std::string(dir ? dir : "/tmp") + "/gui-smoke-" + std::to_string(getpid()) + ".ini";
    corasip::Settings s;
    s.set("account", "username", "alice");
    s.set("account", "domain", "sip.example.test");
    s.set("account", "server", "sip.example.test");
    std::string err;
    if (!s.save(path, err)) { std::printf("FAIL save: %s\n", err.c_str()); return 1; }

    corasip::Settings loaded;
    loaded.load(path, err);
    corasip::History history;
    history.open(path + ".db", err);
    corasip::ui::PhoneEngine phone;
    corasip::ContactStore contacts;
    contacts.open(path + ".db", err);
    corasip::ui::MainWindow window(loaded, path, history, contacts, phone, "", "");
    window.show();

    // Optional: CORASIP_GUI_SCREENSHOT=<png path> saves the window for a visual check.
    if (const char* shot = std::getenv("CORASIP_GUI_SCREENSHOT"); shot && *shot) {
        window.resize(300, 520);
        app.processEvents();
        window.grab().save(shot);
    }

    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) { std::printf("FAIL: %s\n", what); ++failures; }
    };
    auto* tabs = window.findChild<QTabWidget*>();
    check(tabs != nullptr && tabs->count() == 3, "three tabs: Phone, Logs, Contacts");
    if (tabs) {
        check(tabs->tabText(0) == "Phone" && tabs->tabText(1) == "Logs" && tabs->tabText(2) == "Contacts", "tab names");
    }
    check(!window.findChildren<QLineEdit*>().isEmpty(), "dial input present");
    for (const char* key : {"1", "5", "9", "*", "0", "#", "+", "Del"}) {
        bool found = false;
        for (auto* b : window.findChildren<QPushButton*>()) {
            // Digit keys show their letters under the digit (the reference softphone style): match the digit on the first line.
            if (b->text() == key || b->text().startsWith(key + QLatin1Char('\n'))) { found = true; break; }
        }
        check(found, key);
    }
    bool callEnabled = false, redialPresent = false;
    for (auto* b : window.findChildren<QPushButton*>()) {
        if (b->text() == "Call") callEnabled = b->isEnabled();     // idle: a call can be placed
        if (b->text() == "Last Call") redialPresent = true;
    }
    check(callEnabled, "Call button enabled when no call is active");
    check(redialPresent, "Last Call (redial) button present");

    // Contacts tab (M4): a table named Contacts with three columns, and the search box.
    bool contactsTable = false, searchBox = false;
    for (auto* t : window.findChildren<QTableWidget*>()) {
        if (t->accessibleName() == "Contacts") contactsTable = t->columnCount() == 3;
    }
    for (auto* e : window.findChildren<QLineEdit*>()) {
        if (e->accessibleName() == "Search contacts") searchBox = true;
    }
    check(contactsTable, "Contacts table with three columns");
    check(searchBox, "Contacts search box present");

    // Transfer controls (SPEC D22): present, and attended transfer is off with no call up.
    bool transferPresent = false, attendedOffIdle = false;
    for (auto* b : window.findChildren<QPushButton*>()) {
        if (b->text() == "Transfer") transferPresent = true;
        if (b->text() == "Attended transfer") attendedOffIdle = !b->isEnabled();
    }
    check(transferPresent, "Transfer button present");
    check(attendedOffIdle, "Attended transfer disabled with no call");

    // Typed Enter in the dial field must reach dial(). The outcome (a call, or an error such as no route) changes the
    // labels in the window, so compare the visible text before and after the key press.
    auto visibleText = [&window] {
        QString all;
        for (auto* label : window.findChildren<QLabel*>()) all += label->text() + QLatin1Char('|');
        return all;
    };
    QLineEdit* dialField = nullptr;
    for (auto* e : window.findChildren<QLineEdit*>()) {
        if (e->accessibleName() == "Dial number") dialField = e;
    }
    if (dialField) {
        dialField->setText("1091");
        const QString before = visibleText();
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QCoreApplication::sendEvent(dialField, &enter);
        app.processEvents();
        check(visibleText() != before, "typed Enter dials (window reports the dial attempt)");
    }
    unlink(path.c_str());
    unlink((path + ".db").c_str());
    if (failures == 0) std::printf("PASS: GUI smoke (platform: %s)\n", QApplication::platformName().toUtf8().constData());
    return failures == 0 ? 0 : 1;
}
