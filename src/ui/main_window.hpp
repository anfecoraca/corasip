// Main window (M3): the reference softphone-style layout with calling, several calls (Call Manager), call history and redial.
#pragma once

#include "engine/engine.hpp"
#include "settings/settings.hpp"
#include "storage/contacts.hpp"
#include "storage/history.hpp"
#include "ui/ladder_view.hpp"
#include "ui/phone_engine.hpp"

#include <QAction>
#include <QDateTime>
#include <QMainWindow>
#include <QMap>
#include <QPoint>

#include <vector>

class QKeyEvent;
class QSystemTrayIcon;
class QDockWidget;
class QPlainTextEdit;
class QCloseEvent;
class QLabel;
class QLineEdit;
class QTableWidget;
class QListWidget;
class QPushButton;
class QTableWidget;
class QTabWidget;
class QTimer;

namespace corasip::ui {

class GlobalHotkey;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(Settings& settings, const std::string& settingsPath, History& history, ContactStore& contacts,
               PhoneEngine& phone, const std::string& ringtonePath, const std::string& toneDir, QWidget* parent = nullptr);
    ~MainWindow() override;
    // A sip: or tel: link from the desktop, or from the command line: show the window and dial the number (M5E).
    void openUri(const QString& uri);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void showFromTray();
    // Trace panel (basic): SIP messages and RTP statistics of the selected call.
    void showTrace(bool on);
    void refreshTrace();
    QPushButton* trace_ = nullptr;
    QDockWidget* traceDock_ = nullptr;
    LadderView* ladder_ = nullptr;
    QVector<SipTraceLine> traceLines_;
    QPlainTextEdit* traceDetail_ = nullptr;
    QLabel* traceStats_ = nullptr;
    QTimer* traceTimer_ = nullptr;
    int traceCallId_ = -1;             // the call whose trace is cached
    QSize preTraceSize_;               // the window size before the panel opened; restored when it closes
    QSystemTrayIcon* tray_ = nullptr;  // tray icon: closing the window hides it there (SPEC D29)
    GlobalHotkey* hotkey_ = nullptr;   // answer or hang up from anywhere on X11 (M5F, SPEC D31)
    void answerOrHangup();

private slots:
    void addOrEditAccount();
    void registerAccount();
    void unregisterAccount();
    void toggleAlwaysOnTop(bool on);
    void onAccountStatus(const QString& text);
    void onCallStarted(int callId, bool incoming, const QString& remote);
    void onCallState(int callId, const QString& state);
    void onCallEnded(int callId, int sipCode, bool wasAnswered);
    void onTransferStatus(int callId, int sipCode, bool finalNotify);
    void onRegistrationFailed(const QString& reason);
    void tickTimer();

private:
    struct Call {
        bool incoming = false;
        QString remote;
        QDateTime started;
        QDateTime connected;
        bool answered = false;
        bool rejectedByUs = false;
        bool onHold = false;
        bool muted = false;
    };

    void buildMenus();
    QWidget* buildContactsTab();
    void refreshContacts();
    void contactCall();
    void contactAdd();
    void contactEdit();
    void contactCopy();
    void contactDelete();
    void contactMenu(const QPoint& where);
    bool editContactDialog(Contact& c, const QString& title);
    bool selectedContact(Contact& out) const;
    void importContacts();
    void exportContacts();
    QWidget* buildPhoneTab();
    QWidget* buildLogsTab();
    void startPhone();
    // Keys typed while the Phone tab shows dial like the keypad (digits, *, #, Backspace, Enter).
    // An application-wide filter, so focus on a list or button after a call does not stop them.
    bool eventFilter(QObject* watched, QEvent* event) override;
    bool dialKey(QKeyEvent* event);
    void refreshAccountLabel();
    void saveSettings();
    void settingsDialog();
    void refreshLogs();
    void refreshCallList();
    void refreshControls();
    void selectCall(int callId);
    void answerSelected();
    void rejectSelected();
    void hangupSelected();
    void setHoldSelected(bool on);
    void setMuteSelected(bool on);
    void transferBlindSelected();   // SPEC D22: blind transfer to a number
    void attendedStart();           // SPEC D22: hold the selected call, call the target
    void attendedConnect();         // SPEC D22: connect the held call to the consulted call
    void holdOthers();
    bool liveCall() const;
    void dial(const QString& number);
    void dialFromKey(const QString& key);
    QString uriFor(const QString& number) const;
    void setStatus(const QString& text);
    void updateRegistrationActions();

    Settings& settings_;
    std::string settingsPath_;
    History& history_;
    ContactStore& contacts_;
    std::vector<Contact> shownContacts_;   // the rows in the Contacts table, in order
    QTableWidget* contactTable_ = nullptr;
    QLineEdit* contactSearch_ = nullptr;
    PhoneEngine& phone_;
    std::string ringtonePath_;
    std::string toneDir_;
    AccountSettings account_;
    bool storePassword_ = false;
    bool phoneStarted_ = false;
    bool registered_ = false;
    bool dndOn_ = false;
    bool singleCallMode_ = true;          // default on (the reference softphone); off lets a second call in

    QMap<int, Call> calls_;               // every call the window tracks, by engine id
    int selected_ = -1;                   // the call the controls act on
    QString lastDialled_;

    QTabWidget* tabs_ = nullptr;
    QLineEdit* dial_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* accountLabel_ = nullptr;
    QLabel* callLabel_ = nullptr;
    QLabel* timeLabel_ = nullptr;
    QListWidget* callList_ = nullptr;
    QPushButton* call_ = nullptr;
    QPushButton* lastCall_ = nullptr;
    QPushButton* answer_ = nullptr;
    QPushButton* reject_ = nullptr;
    QPushButton* hangup_ = nullptr;
    QPushButton* hold_ = nullptr;
    QPushButton* mute_ = nullptr;
    QPushButton* transfer_ = nullptr;
    QPushButton* attended_ = nullptr;
    QPushButton* connectTransfer_ = nullptr;
    int attendedFrom_ = -1;               // the call put on hold for an attended transfer
    int attendedTo_ = -1;                 // the consulted call to connect it to
    QPushButton* dnd_ = nullptr;
    QAction* registerAction_ = nullptr;
    QAction* unregisterAction_ = nullptr;
    QTableWidget* logs_ = nullptr;
    QTimer* timer_ = nullptr;
};

}  // namespace corasip::ui
