#include "ui/main_window.hpp"

#include "app/account_validation.hpp"
#include "storage/contacts_csv.hpp"
#include "ui/account_dialog.hpp"
#include "ui/hotkey_x11.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QDockWidget>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGuiApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace corasip::ui {

namespace {

QString formatDuration(qint64 seconds) {
    return QStringLiteral("%1:%2").arg(seconds / 60, 2, 10, QChar('0')).arg(seconds % 60, 2, 10, QChar('0'));
}

// the reference softphone wording for why a call ended.
QString endWord(int sipCode, bool answered) {
    if (answered) return QObject::tr("Call Ended");
    switch (sipCode) {
        case 486: return QObject::tr("Busy");
        case 480: return QObject::tr("Unavailable");
        case 408: return QObject::tr("Request Timeout");
        case 603: return QObject::tr("Declined");
        case 487: return QObject::tr("Cancelled");
        default: return QObject::tr("Call Ended");
    }
}

// "<sip:1091@host;tag=x>" -> "sip:1091@host": the form the Logs tab shows for every call.
QString cleanRemote(const QString& raw) {
    QString s = raw.trimmed();
    if (s.startsWith('<')) s = s.mid(1);
    if (s.endsWith('>')) s.chop(1);
    const int semi = s.indexOf(';');
    if (semi >= 0) s = s.left(semi);
    return s.trimmed();
}

// The name shown when there is no contact name: the user part, e.g. "1091" from "sip:1091@host".
QString nameFor(const QString& clean) {
    QString s = clean;
    if (s.startsWith("sip:")) s = s.mid(4);
    const int at = s.indexOf('@');
    return at >= 0 ? s.left(at) : s;
}

}  // namespace

MainWindow::MainWindow(Settings& settings, const std::string& settingsPath, History& history, ContactStore& contacts,
                       PhoneEngine& phone, const std::string& ringtonePath, const std::string& toneDir, QWidget* parent)
    : QMainWindow(parent),
      settings_(settings),
      settingsPath_(settingsPath),
      history_(history),
      contacts_(contacts),
      phone_(phone),
      ringtonePath_(ringtonePath),
      toneDir_(toneDir) {
    setWindowTitle(tr("corasip"));
    setMinimumSize(340, 600);
    resize(420, 720);

    loadAccount(settings_, account_);
    storePassword_ = settings_.has("account", "password");
    singleCallMode_ = settings_.get("general", "single_call_mode", "1") != "0";

    tabs_ = new QTabWidget(this);
    tabs_->addTab(buildPhoneTab(), tr("Phone"));
    tabs_->addTab(buildLogsTab(), tr("Logs"));
    tabs_->addTab(buildContactsTab(), tr("Contacts"));
    qApp->installEventFilter(this);
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        tray_ = new QSystemTrayIcon(QIcon(QStringLiteral(":/corasip.svg")), this);
        tray_->setToolTip(tr("corasip"));
        auto* trayMenu = new QMenu(this);
        trayMenu->addAction(tr("Show corasip"), this, [this] { showFromTray(); });
        trayMenu->addAction(tr("Quit"), qApp, &QCoreApplication::quit);
        tray_->setContextMenu(trayMenu);
        connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason != QSystemTrayIcon::Trigger) return;
            if (isVisible()) hide(); else showFromTray();
        });
        tray_->show();
    }
    // M5F: the answer/hang-up hotkey. Default Ctrl+Alt+A; set [hotkey] answer_hangup to change it, or empty to turn it off.
    hotkey_ = new GlobalHotkey(this);
    const QString combo = QString::fromStdString(settings_.get("hotkey", "answer_hangup", "Ctrl+Alt+A"));
    QString hotkeyErr;
    if (!combo.isEmpty() && !hotkey_->bind(combo, hotkeyErr)) {
        // The status bar is built after this constructor part, so the message waits for the event loop.
        QTimer::singleShot(0, this, [this, hotkeyErr] { setStatus(hotkeyErr); });
    }
    connect(hotkey_, &GlobalHotkey::activated, this, [this] { answerOrHangup(); });
    setCentralWidget(tabs_);
    buildMenus();
    // The trace panel sits on the right and is hidden until the Trace button is pressed.
    traceDock_ = new QDockWidget(tr("Trace"), this);
    traceDock_->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
    auto* traceBody = new QWidget(traceDock_);
    auto* traceColumn = new QVBoxLayout(traceBody);
    traceStats_ = new QLabel(tr("No call selected."), traceBody);
    traceStats_->setWordWrap(true);
    traceStats_->setAccessibleName(tr("Call statistics"));
    ladder_ = new LadderView(traceBody);
    auto* ladderScroll = new QScrollArea(traceBody);
    ladderScroll->setWidget(ladder_);
    ladderScroll->setWidgetResizable(true);
    ladderScroll->setAccessibleName(tr("Call ladder"));
    traceDetail_ = new QPlainTextEdit(traceBody);
    traceDetail_->setReadOnly(true);
    traceDetail_->setAccessibleName(tr("Message details"));
    traceColumn->addWidget(traceStats_);
    traceColumn->addWidget(ladderScroll, 2);
    traceColumn->addWidget(traceDetail_, 2);
    traceDock_->setWidget(traceBody);
    addDockWidget(Qt::RightDockWidgetArea, traceDock_);
    traceDock_->hide();
    traceTimer_ = new QTimer(this);
    traceTimer_->setInterval(1000);
    connect(traceTimer_, &QTimer::timeout, this, [this] { refreshTrace(); });
    connect(traceDock_, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (trace_ != nullptr && trace_->isChecked() != visible) trace_->setChecked(visible);
        if (!visible) {
            // Closed (by the button or the panel's own close): the phone returns to its original size.
            traceTimer_->stop();
            if (preTraceSize_.isValid()) resize(preTraceSize_);
        }
    });
    connect(ladder_, &LadderView::rowClicked, this, [this](int row) {
        if (row >= 0 && row < traceLines_.size()) traceDetail_->setPlainText(QString::fromStdString(traceLines_[row].text));
    });

    status_ = new QLabel(tr("Offline"), this);
    accountLabel_ = new QLabel(this);
    statusBar()->addWidget(status_, 1);
    statusBar()->addPermanentWidget(accountLabel_);
    refreshContacts();  // after the status label exists, so a load error can be shown
    refreshAccountLabel();

    timer_ = new QTimer(this);
    timer_->setInterval(1000);
    connect(timer_, &QTimer::timeout, this, &MainWindow::tickTimer);
    timer_->start();

    connect(&phone_, &PhoneEngine::accountStatus, this, &MainWindow::onAccountStatus);
    connect(&phone_, &PhoneEngine::callStarted, this, &MainWindow::onCallStarted);
    connect(&phone_, &PhoneEngine::callState, this, &MainWindow::onCallState);
    connect(&phone_, &PhoneEngine::callEnded, this, &MainWindow::onCallEnded);
    connect(&phone_, &PhoneEngine::registrationFailed, this, &MainWindow::onRegistrationFailed);
    connect(&phone_, &PhoneEngine::transferStatus, this, &MainWindow::onTransferStatus);

    refreshLogs();
    refreshControls();
    updateRegistrationActions();
    if (account_.user.empty()) {
        QMetaObject::invokeMethod(this, &MainWindow::addOrEditAccount, Qt::QueuedConnection);
    } else {
        startPhone();
        registered_ = true;
        updateRegistrationActions();
    }
}

void MainWindow::buildMenus() {
    QMenu* account = menuBar()->addMenu(tr("&Account"));
    QAction* add = account->addAction(tr("&Add Account..."));
    connect(add, &QAction::triggered, this, &MainWindow::addOrEditAccount);
    QAction* edit = account->addAction(tr("&Edit Account..."));
    edit->setShortcut(QKeySequence(QStringLiteral("Ctrl+M")));
    connect(edit, &QAction::triggered, this, &MainWindow::addOrEditAccount);
    account->addSeparator();
    registerAction_ = account->addAction(tr("&Register"));
    connect(registerAction_, &QAction::triggered, this, &MainWindow::registerAccount);
    unregisterAction_ = account->addAction(tr("&Unregister"));
    connect(unregisterAction_, &QAction::triggered, this, &MainWindow::unregisterAccount);
    account->addSeparator();
    QAction* exit = account->addAction(tr("E&xit"));
    exit->setShortcut(QKeySequence(QStringLiteral("Ctrl+Q")));
    connect(exit, &QAction::triggered, this, &QWidget::close);

    QMenu* view = menuBar()->addMenu(tr("&View"));
    QAction* top = view->addAction(tr("&Always on Top"));
    top->setCheckable(true);
    connect(top, &QAction::toggled, this, &MainWindow::toggleAlwaysOnTop);

    QMenu* tools = menuBar()->addMenu(tr("&Tools"));
    QAction* importContacts = tools->addAction(tr("Import contacts..."));
    connect(importContacts, &QAction::triggered, this, &MainWindow::importContacts);
    QAction* exportContacts = tools->addAction(tr("Export contacts..."));
    connect(exportContacts, &QAction::triggered, this, &MainWindow::exportContacts);
    tools->addSeparator();
    QAction* settings = tools->addAction(tr("&Settings..."));
    settings->setShortcut(QKeySequence(QStringLiteral("Ctrl+P")));
    connect(settings, &QAction::triggered, this, &MainWindow::settingsDialog);

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    QAction* about = help->addAction(tr("&About corasip"));
    connect(about, &QAction::triggered, this, [this] {
        QMessageBox box(this);
        box.setWindowTitle(tr("About corasip"));
        box.setTextFormat(Qt::RichText);
        box.setText(tr("<b>corasip</b> 0.1.0: an open-source SIP softphone (GPL-3.0-or-later).<br>"
                       "Engine: PJSIP 2.17.<br><br>"
                       "Author: <a href=\"https://anfecora.com\">anfecora</a>"));
        box.setIconPixmap(QIcon(QStringLiteral(":/corasip.svg")).pixmap(48, 48));
        if (auto* label = box.findChild<QLabel*>(QString(), Qt::FindDirectChildrenOnly); label != nullptr) {
            label->setOpenExternalLinks(true);
        }
        box.exec();
    });
}

QWidget* MainWindow::buildContactsTab() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    contactTable_ = new QTableWidget(0, 3, page);
    contactTable_->setHorizontalHeaderLabels({tr("Name"), tr("Number"), tr("Information")});
    contactTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    contactTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    contactTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    contactTable_->verticalHeader()->setVisible(false);
    contactTable_->setAccessibleName(tr("Contacts"));
    contactTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(contactTable_, &QTableWidget::customContextMenuRequested, this, &MainWindow::contactMenu);
    connect(contactTable_, &QTableWidget::cellDoubleClicked, this, [this](int, int) { contactCall(); });
    layout->addWidget(contactTable_, 1);

    // The search box sits at the bottom of the list, as in the reference softphone (matrix §2).
    contactSearch_ = new QLineEdit(page);
    contactSearch_->setPlaceholderText(tr("Search contacts"));
    contactSearch_->setAccessibleName(tr("Search contacts"));
    connect(contactSearch_, &QLineEdit::textChanged, this, [this] { refreshContacts(); });
    // Same action as "Add..." in the right-click menu, but visible.
    auto* searchRow = new QHBoxLayout;
    searchRow->addWidget(contactSearch_, 1);
    auto* addContact = new QPushButton(tr("Add contact"), page);
    addContact->setAccessibleName(tr("Add contact"));
    connect(addContact, &QPushButton::clicked, this, &MainWindow::contactAdd);
    searchRow->addWidget(addContact);
    layout->addLayout(searchRow);
    return page;
}

void MainWindow::refreshContacts() {
    if (!contactTable_) return;
    std::string err;
    shownContacts_.clear();
    if (!contacts_.list(contactSearch_ ? contactSearch_->text().toStdString() : std::string(), shownContacts_, err)) {
        setStatus(tr("Contacts: %1").arg(QString::fromStdString(err)));
    }
    contactTable_->setRowCount(0);
    for (const Contact& c : shownContacts_) {
        const int row = contactTable_->rowCount();
        contactTable_->insertRow(row);
        const QString name = QString::fromStdString(c.name);
        // SPEC D23: favourites show a star in the Name column.
        auto* nameItem = new QTableWidgetItem(c.favourite ? QStringLiteral("★ %1").arg(name) : name);
        contactTable_->setItem(row, 0, nameItem);
        contactTable_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(c.number)));
        contactTable_->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(c.info)));
    }
}

void MainWindow::importContacts() {
    const QString file = QFileDialog::getOpenFileName(this, tr("Import contacts"), QString(), tr("CSV files (*.csv)"));
    if (file.isEmpty()) return;
    corasip::CsvImportResult res;
    std::string err;
    if (!corasip::importContactsCsv(contacts_, file.toStdString(), res, err)) {
        setStatus(QString::fromStdString(err));
        return;
    }
    refreshContacts();
    setStatus(tr("Imported %1, skipped %2 duplicate and %3 invalid")
                  .arg(res.added).arg(res.skippedDuplicate).arg(res.skippedInvalid));
}

void MainWindow::exportContacts() {
    const QString file = QFileDialog::getSaveFileName(this, tr("Export contacts"), QStringLiteral("contacts.csv"),
                                                      tr("CSV files (*.csv)"));
    if (file.isEmpty()) return;
    std::string err;
    if (!corasip::exportContactsCsv(contacts_, file.toStdString(), err)) {
        setStatus(QString::fromStdString(err));
        return;
    }
    setStatus(tr("Contacts exported"));
}

bool MainWindow::selectedContact(Contact& out) const {
    const int row = contactTable_ ? contactTable_->currentRow() : -1;
    if (row < 0 || row >= static_cast<int>(shownContacts_.size())) return false;
    out = shownContacts_[static_cast<std::size_t>(row)];
    return true;
}

void MainWindow::contactMenu(const QPoint& where) {
    QMenu menu(this);
    Contact c;
    const bool has = selectedContact(c);
    QAction* call = menu.addAction(tr("Call"));
    call->setEnabled(has);
    QAction* add = menu.addAction(tr("Add..."));
    QAction* edit = menu.addAction(tr("Edit..."));
    edit->setEnabled(has);
    QAction* copy = menu.addAction(tr("Copy number"));
    copy->setEnabled(has);
    QAction* del = menu.addAction(tr("Delete"));
    del->setEnabled(has);
    QAction* chosen = menu.exec(contactTable_->viewport()->mapToGlobal(where));
    if (chosen == call) contactCall();
    else if (chosen == add) contactAdd();
    else if (chosen == edit) contactEdit();
    else if (chosen == copy) contactCopy();
    else if (chosen == del) contactDelete();
}

void MainWindow::contactCall() {
    Contact c;
    if (selectedContact(c)) dial(QString::fromStdString(c.number));
}

void MainWindow::contactCopy() {
    Contact c;
    if (selectedContact(c)) QGuiApplication::clipboard()->setText(QString::fromStdString(c.number));
}

bool MainWindow::editContactDialog(Contact& c, const QString& title) {
    QDialog dlg(this);
    dlg.setWindowTitle(title);
    auto* form = new QFormLayout(&dlg);
    auto* name = new QLineEdit(QString::fromStdString(c.name), &dlg);
    name->setAccessibleName(tr("Name"));
    auto* number = new QLineEdit(QString::fromStdString(c.number), &dlg);
    number->setAccessibleName(tr("Number"));
    auto* info = new QLineEdit(QString::fromStdString(c.info), &dlg);
    info->setAccessibleName(tr("Information"));
    info->setPlaceholderText(tr("Notes about this contact"));
    auto* fav = new QCheckBox(tr("Favourite"), &dlg);
    fav->setChecked(c.favourite);
    form->addRow(tr("Name:"), name);
    form->addRow(tr("Number:"), number);
    form->addRow(tr("Information:"), info);
    form->addRow(QString(), fav);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    // A contact needs a number (SPEC D21); the OK button stays off until there is one.
    auto* ok = buttons->button(QDialogButtonBox::Ok);
    auto refresh = [ok, number] { ok->setEnabled(!number->text().trimmed().isEmpty()); };
    refresh();
    connect(number, &QLineEdit::textChanged, &dlg, refresh);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    if (dlg.exec() != QDialog::Accepted) return false;
    c.name = name->text().trimmed().toStdString();
    c.number = number->text().trimmed().toStdString();
    c.favourite = fav->isChecked();
    c.info = info->text().trimmed().toStdString();
    return true;
}

void MainWindow::contactAdd() {
    Contact c;
    if (!editContactDialog(c, tr("Add contact"))) return;
    std::int64_t id = 0;
    std::string err;
    if (!contacts_.add(c, id, err)) {
        setStatus(QString::fromStdString(err));
        return;
    }
    refreshContacts();
}

void MainWindow::contactEdit() {
    Contact c;
    if (!selectedContact(c)) return;
    if (!editContactDialog(c, tr("Edit contact"))) return;
    std::string err;
    if (!contacts_.update(c, err)) {
        setStatus(QString::fromStdString(err));
        return;
    }
    refreshContacts();
}

void MainWindow::contactDelete() {
    Contact c;
    if (!selectedContact(c)) return;
    const QString shown = c.name.empty() ? QString::fromStdString(c.number) : QString::fromStdString(c.name);
    if (QMessageBox::question(this, tr("Delete contact"), tr("Delete %1?").arg(shown)) != QMessageBox::Yes) return;
    std::string err;
    if (!contacts_.remove(c.id, err)) {
        setStatus(QString::fromStdString(err));
        return;
    }
    refreshContacts();
}

QWidget* MainWindow::buildPhoneTab() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    dial_ = new QLineEdit(page);
    dial_->setPlaceholderText(tr("Enter number or SIP address"));
    dial_->setAccessibleName(tr("Dial number"));
    connect(dial_, &QLineEdit::returnPressed, this, [this] { dial(dial_->text()); });
    layout->addWidget(dial_);

    auto* keys = new QGridLayout;
    const QStringList rows[] = {{"1", "2", "3"}, {"4", "5", "6"}, {"7", "8", "9"}, {"*", "0", "#"}, {"R", "+", "Del"}};
    int r = 0;
    for (const auto& row : rows) {
        int c = 0;
        for (const auto& key : row) {
            if (key.isEmpty()) {  // keeps the bottom row aligned; no key here
                ++c;
                continue;
            }
            // the reference softphone-style key: the digit, and the letters it stands for under it (as on a phone keypad).
            static const QMap<QString, QString> letters = {
                {"2", "ABC"}, {"3", "DEF"}, {"4", "GHI"}, {"5", "JKL"}, {"6", "MNO"}, {"7", "PQRS"}, {"8", "TUV"}, {"9", "WXYZ"}};
            auto* b = new QPushButton(letters.contains(key) ? key + "\n" + letters.value(key) : key, page);
            // Fixed size: the keypad keeps the same size whatever the window size or call state.
            b->setFixedSize(QSize(88, 48));
            b->setAccessibleName(key == "Del" ? tr("Delete last digit") : key == "R" ? tr("Redial last number") : tr("Key %1").arg(key));
            if (key == "Del") b->setToolTip(tr("Delete the last digit"));
            if (key == "R") b->setToolTip(tr("Redial the last number (when the dial box is empty)"));
            connect(b, &QPushButton::clicked, this, [this, key] { dialFromKey(key); });
            keys->addWidget(b, r, c++, Qt::AlignCenter);
        }
        ++r;
    }
    layout->addLayout(keys);

    auto* dialRow = new QHBoxLayout;
    call_ = new QPushButton(tr("Call"), page);
    call_->setAccessibleName(tr("Call"));
    // Green while idle or dialing; red "End" once the selected call is connected (see refreshControls).
    call_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #2e9d4f; color: white; font-weight: bold; padding: 6px; border-radius: 4px; }"
        "QPushButton:disabled { background: #7fbf93; color: white; }"
        "QPushButton[mode=\"end\"] { background: #d64541; }"
        "QPushButton[mode=\"end\"]:disabled { background: #e8a09e; }"));
    connect(call_, &QPushButton::clicked, this, [this] {
        const bool connected = selected_ >= 0 && calls_.contains(selected_) && calls_[selected_].answered;
        if (connected) hangupSelected();
        else dial(dial_->text());
    });
    lastCall_ = new QPushButton(tr("Last Call"), page);
    lastCall_->setAccessibleName(tr("Redial last call"));
    connect(lastCall_, &QPushButton::clicked, this, [this] { dial(lastDialled_); });
    dialRow->addWidget(call_, 2);
    dialRow->addWidget(lastCall_, 1);
    layout->addLayout(dialRow);

    // The Call Manager: every call the window tracks. Click a row to make it the selected call.
    callList_ = new QListWidget(page);
    callList_->setAccessibleName(tr("Calls"));
    callList_->setMaximumHeight(90);
    connect(callList_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        selectCall(item->data(Qt::UserRole).toInt());
    });
    layout->addWidget(callList_);

    // Controls for the selected call.
    callLabel_ = new QLabel(page);
    callLabel_->setAccessibleName(tr("Call state"));
    timeLabel_ = new QLabel(page);
    timeLabel_->setAccessibleName(tr("Call time"));
    auto* info = new QHBoxLayout;
    info->addWidget(callLabel_, 1);
    info->addWidget(timeLabel_);
    layout->addLayout(info);

    auto* actions = new QHBoxLayout;
    answer_ = new QPushButton(tr("Answer"), page);
    answer_->setAccessibleName(tr("Answer call"));
    connect(answer_, &QPushButton::clicked, this, &MainWindow::answerSelected);
    reject_ = new QPushButton(tr("Reject"), page);
    reject_->setAccessibleName(tr("Reject call"));
    connect(reject_, &QPushButton::clicked, this, &MainWindow::rejectSelected);
    hangup_ = new QPushButton(tr("Hang up"), page);
    hangup_->setAccessibleName(tr("Hang up call"));
    connect(hangup_, &QPushButton::clicked, this, &MainWindow::hangupSelected);
    hold_ = new QPushButton(tr("Hold"), page);
    hold_->setCheckable(true);
    hold_->setAccessibleName(tr("Hold call"));
    connect(hold_, &QPushButton::toggled, this, &MainWindow::setHoldSelected);
    mute_ = new QPushButton(tr("Mute"), page);
    mute_->setCheckable(true);
    mute_->setAccessibleName(tr("Mute microphone"));
    connect(mute_, &QPushButton::toggled, this, &MainWindow::setMuteSelected);
    transfer_ = new QPushButton(tr("Transfer"), page);
    transfer_->setAccessibleName(tr("Transfer call to a number"));
    connect(transfer_, &QPushButton::clicked, this, &MainWindow::transferBlindSelected);
    attended_ = new QPushButton(tr("Attended transfer"), page);
    attended_->setAccessibleName(tr("Attended transfer: consult first"));
    connect(attended_, &QPushButton::clicked, this, &MainWindow::attendedStart);
    connectTransfer_ = new QPushButton(tr("Connect transfer"), page);
    connectTransfer_->setAccessibleName(tr("Connect the transfer"));
    connect(connectTransfer_, &QPushButton::clicked, this, &MainWindow::attendedConnect);
    for (QPushButton* b : {answer_, reject_, hangup_, hold_, mute_, transfer_, attended_, connectTransfer_}) actions->addWidget(b);
    layout->addLayout(actions);

    // The reference softphone switch row. DND works now; the others arrive in later milestones.
    auto* switches = new QHBoxLayout;
    for (const char* label : {"DND", "AA", "CONF", "Trace", "REC"}) {
        auto* b = new QPushButton(label, page);
        b->setCheckable(true);
        if (QString::fromLatin1(label) == "Trace") {
            trace_ = b;
            b->setEnabled(true);   // always available; with no call it says so
            b->setAccessibleName(tr("Trace the selected call"));
            b->setToolTip(tr("Show the SIP messages and RTP statistics of the selected call."));
            connect(b, &QPushButton::toggled, this, [this](bool on) { showTrace(on); });
        } else if (QString::fromLatin1(label) == "DND") {
            dnd_ = b;
            b->setAccessibleName(tr("Do not disturb"));
            b->setToolTip(tr("When on, every incoming call is declined."));
            connect(b, &QPushButton::toggled, this, [this](bool on) {
                dndOn_ = on;
                setStatus(on ? tr("Do not disturb") : tr("Connected"));
            });
        } else {
            b->setEnabled(false);
            b->setAccessibleName(tr("Toggle %1").arg(label));
        }
        switches->addWidget(b);
    }
    layout->addLayout(switches);
    layout->addStretch(1);
    return page;
}

QWidget* MainWindow::buildLogsTab() {
    logs_ = new QTableWidget(0, 4, this);
    logs_->setHorizontalHeaderLabels({tr("Name"), tr("Number"), tr("Time"), tr("Duration"), tr("Information")});
    logs_->setSelectionBehavior(QAbstractItemView::SelectRows);
    logs_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    logs_->setAccessibleName(tr("Call history"));
    // Columns share the window width and follow resizes; time and duration fit their text.
    QHeaderView* header = logs_->horizontalHeader();
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->setStretchLastSection(false);
    logs_->setColumnWidth(0, 110);
    logs_->setColumnWidth(1, 190);
    header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    logs_->verticalHeader()->setVisible(false);
    connect(logs_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        dial(logs_->item(row, 1)->text());  // click-to-redial
    });
    return logs_;
}

namespace {

// Information column of the Logs tab: why the call ended, in the reference softphone wording.
QString informationText(const CallRecord& r) {
    if (r.outcome == "answered" || r.outcome == "dialled") return QStringLiteral("Call Ended");
    switch (r.sipCode) {
        case 486: return QStringLiteral("Busy");
        case 480: return QStringLiteral("Unavailable");
        case 408: return QStringLiteral("Request Timeout");
        case 603: return QStringLiteral("Declined");
        case 487: return QStringLiteral("Cancelled");
        default: break;
    }
    if (r.outcome == "missed") return QStringLiteral("Missed");
    if (r.outcome == "rejected") return QStringLiteral("Declined");
    if (r.sipCode > 0) return QStringLiteral("Failed (%1)").arg(r.sipCode);
    return QStringLiteral("Failed");
}

}  // namespace

namespace {

// M5E: autostart is a desktop entry in the user's autostart folder (off by default, SPEC D29).
QString autostartPath() {
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/autostart/corasip.desktop");
}

void setAutostart(bool on) {
    const QString path = autostartPath();
    if (!on) {
        QFile::remove(path);
        return;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return;
    f.write(QStringLiteral("[Desktop Entry]\nType=Application\nName=corasip\nExec=%1\nTerminal=false\n"
                           "X-GNOME-Autostart-enabled=true\n")
                .arg(QCoreApplication::applicationFilePath()).toUtf8());
}

}  // namespace

namespace {

// M5B: codec order is saved as keys: opus, g722, pcma (G.711 A-law), pcmu (G.711 u-law). The older key g711
// (both G.711 laws together) is still read and expands to pcma and pcmu.
const char* const kDefaultCodecOrder = "opus,g722,pcma,pcmu";

std::string codecLabel(const std::string& key) {
    if (key == "opus") return "Opus";
    if (key == "g722") return "G.722";
    if (key == "pcma") return "G.711 A-law";
    return "G.711 u-law";
}

// The saved order, with unknown keys dropped and any missing key added at the end in the default order.
std::vector<std::string> codecOrderFrom(const std::string& text) {
    const std::vector<std::string> known = {"opus", "g722", "pcma", "pcmu"};
    std::vector<std::string> order;
    auto add = [&order](const std::string& key) {
        if (std::find(order.begin(), order.end(), key) == order.end()) order.push_back(key);
    };
    std::string token;
    for (char c : text + ",") {
        if (c != ',') { token += c; continue; }
        if (token == "g711") { add("pcma"); add("pcmu"); }
        else if (std::find(known.begin(), known.end(), token) != known.end()) add(token);
        token.clear();
    }
    for (const auto& key : known) add(key);
    return order;
}

// The PJSIP codec names for a saved order.
std::vector<std::string> engineCodecs(const std::vector<std::string>& order) {
    std::vector<std::string> names;
    for (const auto& key : order) {
        if (key == "opus") names.push_back("opus/48000/2");
        else if (key == "g722") names.push_back("G722/16000/1");
        else if (key == "pcma") names.push_back("PCMA/8000/1");
        else names.push_back("PCMU/8000/1");
    }
    return names;
}

// The engine lists every ALSA alias (hw, dmix, hdmi, surround, ...). The choices people use are "default",
// "pulse" and one plughw entry per device, in the direction the device supports. A device that reports no
// channels (PipeWire's pulse route) is kept, as the engine does when it selects a device.
bool isUsefulDevice(const std::string& raw, bool capture) {
    const auto tagAt = raw.rfind(" [in ");
    if (tagAt == std::string::npos) return false;
    const std::string name = raw.substr(0, tagAt);
    int in = 0;
    int out = 0;
    std::sscanf(raw.c_str() + tagAt, " [in %d, out %d]", &in, &out);
    const bool nameOk = name == "default" || name == "pulse" || name.rfind("plughw:", 0) == 0;
    const bool directionOk = (in == 0 && out == 0) || (capture ? in > 0 : out > 0);
    return nameOk && directionOk;
}

}  // namespace

void MainWindow::startPhone() {
    EngineConfig cfg;
    cfg.logLevel = 1;
    cfg.nullAudio = std::getenv("CORASIP_NULL_AUDIO") != nullptr;  // tests and CI: no sound hardware
    cfg.ringtoneFile = ringtonePath_;
    cfg.dtmfToneDir = toneDir_;
    cfg.captureDevice = settings_.get("audio", "capture", "");
    cfg.playbackDevice = settings_.get("audio", "playback", "");
    cfg.codecs = engineCodecs(codecOrderFrom(settings_.get("audio", "codecs", kDefaultCodecOrder)));
    cfg.tlsCaFile = settings_.get("tls", "ca_file", "");
    cfg.tlsVerifyServer = settings_.get("tls", "verify_server", "1") == "1";  // SPEC §5.3: on unless the user opts out
    std::string err;
    if (!phone_.engine().start(cfg, err)) {
        setStatus(tr("Offline: %1").arg(QString::fromStdString(err)));
        return;
    }
    phoneStarted_ = true;
    if (!phone_.engine().registerAccount(account_, err)) {
        setStatus(tr("Offline: %1").arg(QString::fromStdString(err)));
        return;
    }
    setStatus(tr("Connecting..."));
}

void MainWindow::addOrEditAccount() {
    AccountDialog dlg(account_, storePassword_, this);
    if (dlg.exec() != QDialog::Accepted) return;
    account_ = dlg.account();
    storePassword_ = dlg.storePassword();
    saveSettings();
    refreshAccountLabel();
    if (!phoneStarted_) {
        startPhone();
    } else {
        std::string err;
        if (!phone_.engine().registerAccount(account_, err)) setStatus(QString::fromStdString(err));
    }
    registered_ = true;
    updateRegistrationActions();
}

void MainWindow::registerAccount() {
    if (account_.user.empty()) {
        setStatus(tr("Add an account first"));
        return;
    }
    if (!phoneStarted_) {
        startPhone();
    } else {
        std::string err;
        if (!phone_.engine().registerAccount(account_, err)) {
            setStatus(QString::fromStdString(err));
            return;
        }
        setStatus(tr("Connecting..."));
    }
    registered_ = true;
    updateRegistrationActions();
}

void MainWindow::unregisterAccount() {
    std::string err;
    if (!phone_.engine().unregisterAccount(err)) {
        setStatus(QString::fromStdString(err));
        return;
    }
    registered_ = false;
    setStatus(tr("Offline"));
    updateRegistrationActions();
}

void MainWindow::updateRegistrationActions() {
    registerAction_->setEnabled(!registered_ && !account_.user.empty());
    unregisterAction_->setEnabled(registered_);
}

void MainWindow::toggleAlwaysOnTop(bool on) {
    setWindowFlag(Qt::WindowStaysOnTopHint, on);
    show();
}

void MainWindow::onAccountStatus(const QString& text) {
    setStatus(text);
}

void MainWindow::onTransferStatus(int callId, int sipCode, bool finalNotify) {
    if (callId != selected_) return;
    if (sipCode / 100 == 2) {
        setStatus(tr("Transferred"));
    } else if (finalNotify && sipCode >= 300) {
        setStatus(tr("Transfer failed (%1)").arg(sipCode));
    } else {
        setStatus(tr("Transferring..."));
    }
}

void MainWindow::onRegistrationFailed(const QString& reason) {
    setStatus(tr("Request Timeout") == reason ? reason : tr("Offline: %1").arg(reason));
}

bool MainWindow::liveCall() const {
    return !calls_.isEmpty();
}

void MainWindow::onCallStarted(int callId, bool incoming, const QString& remote) {
    Call c;
    c.incoming = incoming;
    c.remote = cleanRemote(remote);
    c.started = QDateTime::currentDateTime();
    calls_.insert(callId, c);

    std::string err;
    if (incoming && dndOn_) {
        // Do not disturb: decline every incoming call (603).
        calls_[callId].rejectedByUs = true;
        phone_.engine().reject(callId, err);
        return;
    }
    if (incoming && singleCallMode_ && calls_.size() > 1) {
        // Single call mode: a second call while busy is declined.
        calls_[callId].rejectedByUs = true;  // stays tracked until it ends, so the history records "rejected"
        phone_.engine().reject(callId, err);
        return;
    }
    if (!incoming) lastDialled_ = c.remote;
    // The new call becomes the selected call: its controls are the ones that matter now.
    selectCall(callId);
    setStatus(incoming ? tr("Incoming call") : tr("Connecting..."));
}

void MainWindow::onCallState(int callId, const QString& state) {
    if (!calls_.contains(callId)) return;
    Call& c = calls_[callId];
    if (state == "confirmed") {
        c.answered = true;
        if (!c.connected.isValid()) c.connected = QDateTime::currentDateTime();
        if (callId == selected_) setStatus(tr("Connected"));
    } else if (state == "no-media") {
        if (callId == selected_) setStatus(tr("No audio: no common codec"));
    } else if (state == "early") {
        if (callId == selected_) setStatus(tr("Ringing..."));
    }
    refreshCallList();
    refreshControls();
}

void MainWindow::onCallEnded(int callId, int sipCode, bool wasAnswered) {
    if (traceDock_->isVisible() && callId == selected_) refreshTrace();  // keep the final messages (BYE)
    if (!calls_.contains(callId)) return;
    Call c = calls_.take(callId);
    if (callId == attendedFrom_ || callId == attendedTo_) {
        attendedFrom_ = -1;
        attendedTo_ = -1;
    }

    CallRecord rec;
    rec.account = account_.user + "@" + account_.domain;
    rec.incoming = c.incoming;
    rec.remote = cleanRemote(c.remote).toStdString();
    rec.startedAt = c.started.toSecsSinceEpoch();
    rec.sipCode = sipCode;
    // Answered means the call really connected (it has a connect time), not only that a state was seen.
    const bool connected = wasAnswered && c.connected.isValid();
    if (connected) {
        rec.durationSeconds = c.connected.secsTo(QDateTime::currentDateTime());
    }
    if (c.incoming) {
        rec.outcome = connected ? "answered" : (c.rejectedByUs ? "rejected" : "missed");
    } else {
        rec.outcome = connected ? "dialled" : "failed";
    }
    std::string err;
    if (!history_.add(rec, err)) setStatus(tr("History not saved: %1").arg(QString::fromStdString(err)));
    refreshLogs();

    if (callId == selected_) {
        // Select the next call that is still there, preferring one that is connected.
        selected_ = -1;
        for (auto it = calls_.constBegin(); it != calls_.constEnd(); ++it) {
            if (it.value().answered) { selected_ = it.key(); break; }
        }
        if (selected_ < 0 && !calls_.isEmpty()) selected_ = calls_.firstKey();
        if (selected_ >= 0) {
            selectCall(selected_);
        } else {
            setStatus(endWord(sipCode, wasAnswered));
        }
    }
    refreshCallList();
    refreshControls();
}

void MainWindow::selectCall(int callId) {
    selected_ = calls_.contains(callId) ? callId : -1;
    if (selected_ >= 0 && calls_[selected_].answered) {
        // Making a connected call the selected call takes it off hold, and every other connected call goes on hold,
        // so two calls never play at the same time. A call put on hold by hand stays on hold while it is not selected.
        std::string err;
        if (calls_[selected_].onHold && phone_.engine().setHold(selected_, false, err)) {
            calls_[selected_].onHold = false;
        }
        holdOthers();
    }
    if (selected_ >= 0) {
        const Call& c = calls_[selected_];
        // Show this call's hold and mute state without running the toggle handlers.
        QSignalBlocker holdBlock(hold_);
        QSignalBlocker muteBlock(mute_);
        hold_->setChecked(c.onHold);
        mute_->setChecked(c.muted);
    }
    refreshCallList();
    refreshControls();
}

void MainWindow::holdOthers() {
    std::string err;
    for (auto it = calls_.begin(); it != calls_.end(); ++it) {
        if (it.key() == selected_ || !it.value().answered || it.value().onHold) continue;
        if (phone_.engine().setHold(it.key(), true, err)) {
            it.value().onHold = true;
        }
    }
}

void MainWindow::answerSelected() {
    if (selected_ < 0 || !calls_.value(selected_).incoming) return;
    // Call Waiting: answering a second call puts the first one on hold.
    holdOthers();
    std::string err;
    if (!phone_.engine().answer(selected_, err)) setStatus(QString::fromStdString(err));
}

void MainWindow::rejectSelected() {
    if (selected_ < 0) return;
    calls_[selected_].rejectedByUs = true;
    std::string err;
    if (!phone_.engine().reject(selected_, err)) setStatus(QString::fromStdString(err));
}

void MainWindow::hangupSelected() {
    if (selected_ < 0) return;
    std::string err;
    if (!phone_.engine().hangup(selected_, err)) setStatus(QString::fromStdString(err));
}

void MainWindow::setHoldSelected(bool on) {
    if (selected_ < 0) return;
    std::string err;
    if (!phone_.engine().setHold(selected_, on, err)) {
        setStatus(QString::fromStdString(err));
        return;
    }
    calls_[selected_].onHold = on;
    setStatus(on ? tr("On hold") : tr("Connected"));
    refreshCallList();
}

void MainWindow::setMuteSelected(bool on) {
    if (selected_ < 0) return;
    std::string err;
    if (!phone_.engine().setMute(selected_, on, err)) {
        setStatus(QString::fromStdString(err));
        return;
    }
    calls_[selected_].muted = on;
}

void MainWindow::transferBlindSelected() {
    if (selected_ < 0 || !calls_.value(selected_).answered) return;
    bool ok = false;
    const QString target = QInputDialog::getText(this, tr("Transfer call"), tr("Transfer to number or SIP address"),
                                                 QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || target.isEmpty()) return;
    std::string err;
    if (!phone_.engine().transferBlind(selected_, uriFor(target).toStdString(), err)) {
        setStatus(QString::fromStdString(err));
        return;
    }
    setStatus(tr("Transferring..."));
}

void MainWindow::attendedStart() {
    if (selected_ < 0 || singleCallMode_ || !calls_.value(selected_).answered) return;
    bool ok = false;
    const QString target = QInputDialog::getText(this, tr("Attended transfer"), tr("Consult this number or SIP address"),
                                                 QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || target.isEmpty()) return;
    const int first = selected_;
    std::string err;
    // Hold the call being transferred, then call the target. The target call is not part of the hold-others rule.
    if (!phone_.engine().setHold(first, true, err)) {
        setStatus(QString::fromStdString(err));
        return;
    }
    calls_[first].onHold = true;
    int second = -1;
    if (!phone_.engine().makeCall(uriFor(target).toStdString(), second, err)) {
        phone_.engine().setHold(first, false, err);
        calls_[first].onHold = false;
        setStatus(QString::fromStdString(err));
        return;
    }
    attendedFrom_ = first;
    attendedTo_ = second;
    setStatus(tr("Consulting..."));
    refreshControls();
}

void MainWindow::attendedConnect() {
    if (attendedFrom_ < 0 || !calls_.contains(attendedTo_)) return;
    std::string err;
    if (!phone_.engine().transferAttended(attendedFrom_, attendedTo_, err)) {
        setStatus(QString::fromStdString(err));
        return;
    }
    attendedFrom_ = -1;
    attendedTo_ = -1;
    setStatus(tr("Transferring..."));
    refreshControls();
}

void MainWindow::tickTimer() {
    refreshCallList();
}

void MainWindow::dial(const QString& number) {
    const QString n = number.trimmed();
    if (n.isEmpty()) return;
    if (!phoneStarted_) {
        setStatus(tr("Add an account first"));
        return;
    }
    if (singleCallMode_ && liveCall()) {
        setStatus(tr("A call is already active"));
        return;
    }
    // Call Waiting: an outgoing call puts the calls that are up on hold first.
    holdOthers();
    std::string err;
    int id = -1;
    if (!phone_.engine().makeCall(uriFor(n).toStdString(), id, err)) {
        setStatus(QString::fromStdString(err));
    }
}

MainWindow::~MainWindow() {
    qApp->removeEventFilter(this);
}

void MainWindow::openUri(const QString& uri) {
    showFromTray();
    tabs_->setCurrentIndex(0);
    // sip:1091@host;transport=udp -> 1091@host
    QString target = uri.mid(uri.indexOf(QLatin1Char(':')) + 1).section(QLatin1Char(';'), 0, 0).trimmed();
    if (target.isEmpty()) return;
    // A link from a web page or another program must not place a call without the user's say-so.
    const auto answer = QMessageBox::question(this, tr("Call from a link"),
        tr("Call %1?").arg(target), QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer == QMessageBox::Yes) dial(target);
}

void MainWindow::answerOrHangup() {
    // Answers the selected incoming call that is ringing; otherwise hangs up the selected call.
    if (selected_ < 0) return;
    const auto& c = calls_.value(selected_);
    if (c.incoming && !c.answered) answerSelected();
    else hangupSelected();
}

void MainWindow::showTrace(bool on) {
    if (on) {
        traceLines_.clear();   // pressing Trace again clears the cached trace
        ladder_->setItems({});
        traceDetail_->clear();
        traceCallId_ = -1;
    }
    if (on && !traceDock_->isVisible()) {
        // Open the panel at full width: the window grows by the panel's width instead of squeezing the phone.
        preTraceSize_ = size();
        traceDock_->setMinimumWidth(360);
        traceDock_->show();
        resize(preTraceSize_.width() + 380, preTraceSize_.height());
    } else if (!on && traceDock_->isVisible()) {
        traceDock_->hide();   // visibilityChanged restores the size
    }
    if (on) {
        refreshTrace();
        traceTimer_->start();
    } else {
        traceTimer_->stop();
    }
}

void MainWindow::refreshTrace() {
    if (!traceDock_->isVisible()) return;
    // A different call selected: start its trace from empty.
    if (selected_ >= 0 && selected_ != traceCallId_) {
        traceLines_.clear();
        ladder_->setItems({});
        traceDetail_->clear();
        traceCallId_ = selected_;
    }
    // The panel follows the call it was showing, also after the call ended and the selection was cleared.
    const int id = traceCallId_;
    if (id < 0) {
        traceStats_->setText(tr("No call selected."));
        return;
    }
    const bool live = calls_.contains(id);
    std::vector<SipTraceLine> lines;
    std::string err;
    if (phone_.engine().sipTrace(id, lines, err)) {
        if (traceLines_.size() != static_cast<int>(lines.size())) {
            traceLines_.clear();
            QVector<LadderView::Item> items;
            for (const auto& l : lines) {
                traceLines_.push_back(l);
                LadderView::Item it;
                it.outgoing = l.outgoing;
                const QString first = QString::fromStdString(l.firstLine);
                it.response = first.startsWith(QLatin1String("SIP/2.0"));
                // Requests show their method; responses show the status code and reason (SIP/2.0 180 Ringing).
                QString label = first;
                if (it.response) label = label.mid(8).trimmed();
                else label = label.section(QLatin1Char(' '), 0, 0);
                it.label = label;
                items.push_back(it);
            }
            ladder_->setItems(items);
        }
    }
    if (!live) {
        traceStats_->setText(tr("Call ended. The trace stays until you press Trace again."));
        return;
    }
    CallStats st;
    if (phone_.engine().callStats(id, st, err)) {
        traceStats_->setText(
            tr("Codec: %1   Sent: %2   Received: %3   Lost: %4 (%5%)\nJitter: %6 ms   Round trip: %7 ms   MOS (estimate): %8")
                .arg(QString::fromStdString(st.codec)).arg(st.packetsSent).arg(st.packetsReceived).arg(st.packetsLost)
                .arg(st.lossPercent, 0, 'f', 1).arg(st.jitterMs, 0, 'f', 1).arg(st.rttMs, 0, 'f', 0)
                .arg(st.mos, 0, 'f', 2));
    }
}

void MainWindow::showFromTray() {
    show();
    raise();
    activateWindow();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // With the tray icon showing, closing hides the window; Quit is in the tray menu.
    if (tray_ != nullptr && tray_->isVisible()) {
        hide();
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::KeyPress && QApplication::activeWindow() == this && tabs_->currentIndex() == 0 &&
        !qobject_cast<QLineEdit*>(QApplication::focusWidget()) &&
        dialKey(static_cast<QKeyEvent*>(event))) {
        return true;
    }
    return QMainWindow::eventFilter(watched, event);
}

bool MainWindow::dialKey(QKeyEvent* event) {
    const QString text = event->text();
    if (text.size() == 1 && (text[0].isDigit() || text == QLatin1String("*") || text == QLatin1String("#"))) {
        dialFromKey(text);
        return true;
    }
    if (event->key() == Qt::Key_Backspace) {
        dialFromKey(QStringLiteral("Del"));
        return true;
    }
    // Enter presses a focused button as usual; otherwise it dials the number typed.
    const bool enter = event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter;
    if (enter && !qobject_cast<QPushButton*>(QApplication::focusWidget()) && !dial_->text().trimmed().isEmpty()) {
        dial(dial_->text());
        return true;
    }
    return false;
}

void MainWindow::dialFromKey(const QString& key) {
    if (key == QLatin1String("R")) {
        if (dial_->text().trimmed().isEmpty() && !lastDialled_.isEmpty()) dial(lastDialled_);
        return;
    }
    // Local feedback so the person can hear that the key registered (speakers only).
    if (key.size() == 1 && (key[0].isDigit() || key == "*" || key == "#")) {
        std::string err;
        phone_.engine().playKeyTone(key.toStdString(), err);
    }
    if (selected_ >= 0 && calls_.value(selected_).answered && key != "Del" && key != "+") {
        std::string err;
        if (!phone_.engine().sendDtmf(selected_, key.toStdString(), err)) setStatus(QString::fromStdString(err));
        return;
    }
    if (key == "Del") {
        dial_->backspace();  // delete the last digit
    } else {
        dial_->insert(key);
    }
}

QString MainWindow::uriFor(const QString& number) const {
    if (number.startsWith("sip:") || number.contains('@')) return number;
    return QStringLiteral("sip:%1@%2").arg(number, QString::fromStdString(account_.domain));
}

void MainWindow::refreshCallList() {
    if (!callList_) return;
    callList_->clear();
    for (auto it = calls_.constBegin(); it != calls_.constEnd(); ++it) {
        const Call& c = it.value();
        QString state;
        if (c.incoming && !c.answered) state = tr("Incoming");
        else if (c.onHold) state = tr("On hold");
        else if (c.answered && c.connected.isValid()) state = formatDuration(c.connected.secsTo(QDateTime::currentDateTime()));
        else state = tr("Calling");
        auto* item = new QListWidgetItem(QStringLiteral("%1  %2%3").arg(c.remote, state,
                                                                       it.key() == selected_ ? QStringLiteral("  ◀") : QString()));
        item->setData(Qt::UserRole, it.key());
        callList_->addItem(item);
    }
    if (selected_ >= 0 && calls_.contains(selected_)) {
        const Call& c = calls_[selected_];
        QString label = c.incoming && !c.answered ? tr("Incoming: %1") : (c.onHold ? tr("On hold: %1") : tr("Connected: %1"));
        if (!c.answered && !c.incoming) label = tr("Calling %1");
        callLabel_->setText(label.arg(c.remote));
        timeLabel_->setText(c.answered && c.connected.isValid()
                                ? formatDuration(c.connected.secsTo(QDateTime::currentDateTime())) : QString());
    } else {
        callLabel_->setText(QString());
        timeLabel_->setText(QString());
    }
}

void MainWindow::refreshControls() {
    const bool any = selected_ >= 0 && calls_.contains(selected_);
    const Call c = any ? calls_[selected_] : Call();
    answer_->setVisible(any && c.incoming && !c.answered);
    reject_->setVisible(any && c.incoming && !c.answered);
    answer_->setEnabled(any && c.incoming && !c.answered);
    reject_->setEnabled(any && c.incoming && !c.answered);
    hangup_->setEnabled(any);
    hold_->setEnabled(any && c.answered);
    mute_->setEnabled(any && c.answered);
    transfer_->setEnabled(any && c.answered);
    // Attended transfer needs a second call, so it is offered only with single call mode off (SPEC D22).
    attended_->setEnabled(any && c.answered && !singleCallMode_ && attendedFrom_ < 0);
    connectTransfer_->setVisible(attendedFrom_ >= 0);
    connectTransfer_->setEnabled(attendedFrom_ >= 0 && calls_.contains(attendedTo_) && calls_[attendedTo_].answered);
    call_->setEnabled(!(singleCallMode_ && liveCall()));
    lastCall_->setEnabled(!lastDialled_.isEmpty() && call_->isEnabled());
    // The call bar: "End" in red when the selected call is connected; "Calling..." (disabled, green) while dialing.
    const bool connected = any && c.answered;
    const bool dialing = any && !c.incoming && !c.answered;
    call_->setProperty("mode", connected ? QStringLiteral("end") : QStringLiteral("call"));
    call_->setText(connected ? tr("End") : dialing ? tr("Calling...") : tr("Call"));
    call_->setAccessibleName(connected ? tr("End call") : tr("Call"));
    if (connected) call_->setEnabled(true);
    if (dialing) call_->setEnabled(false);
    call_->style()->unpolish(call_);
    call_->style()->polish(call_);
}

void MainWindow::refreshLogs() {
    std::vector<CallRecord> rows;
    std::string err;
    if (!history_.list(200, rows, err)) {
        setStatus(QString::fromStdString(err));
        return;
    }
    logs_->setRowCount(0);
    for (const auto& r : rows) {
        const int row = logs_->rowCount();
        logs_->insertRow(row);
        const QString arrow = r.incoming ? QStringLiteral("↙ ") : QStringLiteral("↗ ");  // in / out
        const QString remote = cleanRemote(QString::fromStdString(r.remote));
        const QString name = r.displayName.empty() ? nameFor(remote) : QString::fromStdString(r.displayName);
        logs_->setItem(row, 0, new QTableWidgetItem(arrow + name));
        logs_->setItem(row, 1, new QTableWidgetItem(remote));
        logs_->setItem(row, 2, new QTableWidgetItem(
            QDateTime::fromSecsSinceEpoch(r.startedAt).toString(QStringLiteral("yyyy-MM-dd HH:mm"))));
        logs_->setItem(row, 3, new QTableWidgetItem(formatDuration(r.durationSeconds)));
        logs_->setItem(row, 4, new QTableWidgetItem(informationText(r)));
    }
    std::string last;
    if (history_.lastDialled(last, err)) lastDialled_ = QString::fromStdString(last);
    lastCall_->setEnabled(!lastDialled_.isEmpty());
}

void MainWindow::setStatus(const QString& text) {
    status_->setText(text);
}

void MainWindow::refreshAccountLabel() {
    if (account_.user.empty()) {
        accountLabel_->setText(tr("No account"));
    } else {
        accountLabel_->setText(QStringLiteral("%1@%2").arg(QString::fromStdString(account_.user),
                                                            QString::fromStdString(account_.domain)));
    }
}

// M5A: choose the microphone and speaker, and play a test tone on the speaker. Saved devices are applied
// when the engine starts; changes made here apply at once when no call is active.
void MainWindow::settingsDialog() {
    if (!phoneStarted_) {
        QMessageBox::information(this, tr("Settings"), tr("Add an account first. Audio devices are set while the phone runs."));
        return;
    }
    std::vector<std::string> devices;
    std::string err;
    if (!phone_.engine().listAudioDevices(devices, err)) {
        QMessageBox::warning(this, tr("Settings"), tr("Could not list audio devices: %1").arg(QString::fromStdString(err)));
        return;
    }
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Settings"));
    auto* form = new QFormLayout(&dlg);
    auto* mic = new QComboBox(&dlg);
    auto* spk = new QComboBox(&dlg);
    mic->setAccessibleName(tr("Microphone"));
    spk->setAccessibleName(tr("Speaker"));
    auto fill = [&devices](QComboBox* box, bool capture) {
        box->addItem(tr("System default"), QString());
        for (const auto& raw : devices) {
            if (!isUsefulDevice(raw, capture)) continue;
            // The engine lists "name [in n, out m]"; the choice is stored by name only.
            const QString name = QString::fromStdString(raw);
            const QString shown = name.left(name.lastIndexOf(QStringLiteral(" [in ")));
            box->addItem(shown, shown);
        }
    };
    fill(mic, true);
    fill(spk, false);
    auto select = [](QComboBox* box, const std::string& value) {
        const int i = box->findData(QString::fromStdString(value));
        box->setCurrentIndex(i < 0 ? 0 : i);
    };
    select(mic, settings_.get("audio", "capture", ""));
    select(spk, settings_.get("audio", "playback", ""));
    auto* test = new QPushButton(tr("Test tone"), &dlg);
    test->setAccessibleName(tr("Test tone"));
    auto* codecs = new QListWidget(&dlg);
    codecs->setAccessibleName(tr("Codec order"));
    for (const auto& key : codecOrderFrom(settings_.get("audio", "codecs", kDefaultCodecOrder))) {
        auto* item = new QListWidgetItem(QString::fromStdString(codecLabel(key)));
        item->setData(Qt::UserRole, QString::fromStdString(key));
        codecs->addItem(item);
    }
    auto* up = new QPushButton(tr("Move up"), &dlg);
    auto* down = new QPushButton(tr("Move down"), &dlg);
    auto moveBy = [codecs](int delta) {
        const int row = codecs->currentRow();
        const int target = row + delta;
        if (row < 0 || target < 0 || target >= codecs->count()) return;
        codecs->insertItem(target, codecs->takeItem(row));
        codecs->setCurrentRow(target);
    };
    connect(up, &QPushButton::clicked, &dlg, [moveBy] { moveBy(-1); });
    connect(down, &QPushButton::clicked, &dlg, [moveBy] { moveBy(1); });
    auto* moves = new QWidget(&dlg);
    auto* moveRow = new QHBoxLayout(moves);
    moveRow->setContentsMargins(0, 0, 0, 0);
    moveRow->addWidget(up);
    moveRow->addWidget(down);
    form->addRow(tr("Microphone:"), mic);
    form->addRow(tr("Speaker:"), spk);
    form->addRow(QString(), test);
    form->addRow(tr("Codec order:"), codecs);
    form->addRow(QString(), moves);
    auto* autostart = new QCheckBox(tr("Start corasip when I log in"), &dlg);
    autostart->setChecked(settings_.get("general", "autostart", "0") == "1");
    autostart->setAccessibleName(tr("Start corasip when I log in"));
    form->addRow(QString(), autostart);
    auto* verify = new QCheckBox(tr("Check the server certificate for TLS calls (recommended)"), &dlg);
    verify->setChecked(settings_.get("tls", "verify_server", "1") == "1");
    verify->setAccessibleName(tr("Check the server certificate for TLS calls"));
    verify->setToolTip(tr("Applies the next time corasip starts."));
    form->addRow(QString(), verify);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(test, &QPushButton::clicked, &dlg, [this, mic, spk, &dlg] {
        std::string e;
        if (!phone_.engine().setAudioDevices(mic->currentData().toString().toStdString(),
                                             spk->currentData().toString().toStdString(), e)) {
            QMessageBox::warning(&dlg, tr("Test tone"), tr("Cannot test now: %1").arg(QString::fromStdString(e)));
            return;
        }
        std::string playErr;
        if (!phone_.engine().playKeyTone("1", playErr)) {
            QMessageBox::warning(&dlg, tr("Test tone"), tr("Cannot play the tone: %1").arg(QString::fromStdString(playErr)));
        }
    });
    if (dlg.exec() == QDialog::Accepted) {
        const std::string capture = mic->currentData().toString().toStdString();
        const std::string playback = spk->currentData().toString().toStdString();
        settings_.set("audio", "capture", capture);
        settings_.set("audio", "playback", playback);
        std::vector<std::string> order;
        std::string orderText;
        for (int i = 0; i < codecs->count(); ++i) {
            const std::string key = codecs->item(i)->data(Qt::UserRole).toString().toStdString();
            order.push_back(key);
            orderText += (orderText.empty() ? "" : ",") + key;
        }
        settings_.set("audio", "codecs", orderText);
        // Turning the certificate check off weakens TLS, so it needs a clear confirmation (SPEC §5.3).
        bool verifyChoice = verify->isChecked();
        if (!verifyChoice) {
            const auto answer = QMessageBox::warning(this, tr("Certificate check off"),
                tr("With the certificate check off, a TLS call cannot confirm who the server is. Turn it off anyway?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            verifyChoice = answer != QMessageBox::Yes;
        }
        settings_.set("tls", "verify_server", verifyChoice ? "1" : "0");
        settings_.set("general", "autostart", autostart->isChecked() ? "1" : "0");
        setAutostart(autostart->isChecked());
        saveSettings();
        std::string applyErr;
        const bool devicesOk = phone_.engine().setAudioDevices(capture, playback, applyErr);
        std::string codecErr;
        const bool codecsOk = phone_.engine().setCodecs(engineCodecs(order), codecErr);
        if (!devicesOk || !codecsOk) {
            QMessageBox::information(this, tr("Settings"),
                tr("Saved. The changes apply when no call is active: %1")
                    .arg(QString::fromStdString(devicesOk ? codecErr : applyErr)));
        }
    } else {
        // The test tone may have moved the devices: put the saved ones back.
        std::string restoreErr;
        phone_.engine().setAudioDevices(settings_.get("audio", "capture", ""), settings_.get("audio", "playback", ""),
                                        restoreErr);
    }
}

void MainWindow::saveSettings() {
    saveAccount(settings_, account_, storePassword_);
    std::string err;
    if (!settings_.save(settingsPath_, err)) {
        QMessageBox::warning(this, tr("Settings"), tr("Could not save settings: %1").arg(QString::fromStdString(err)));
    }
}

}  // namespace corasip::ui
