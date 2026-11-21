// corasip GUI entry point. One binary: no install step; the ringtone is compiled in.
#include "settings/settings.hpp"
#include "storage/contacts.hpp"
#include "storage/history.hpp"
#include "ui/main_window.hpp"
#include "ui/phone_engine.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QIcon>
#include <QDir>
#include <QFile>
#include <QLocale>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTranslator>

#include <cstdlib>
#include <string>

namespace {

// The XDG data directory: $XDG_DATA_HOME/corasip or ~/.local/share/corasip.
QString dataDir() {
    if (const char* x = std::getenv("XDG_DATA_HOME"); x && *x) return QString::fromLocal8Bit(x) + "/corasip";
    return QDir::homePath() + "/.local/share/corasip";
}

// The ringtone is compiled in. Engines need a file, so it is written once to the cache directory.
QString extractRingtone() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(dir);
    const QString path = dir + "/ringtone.wav";
    QFile src(QStringLiteral(":/ringtone.wav"));
    if (src.open(QIODevice::ReadOnly)) {
        QFile dst(path);
        if (dst.open(QIODevice::WriteOnly | QIODevice::Truncate)) dst.write(src.readAll());
    }
    return path;
}

// The keypad tones are compiled in too. They are written once to the cache directory, as the ringtone is.
QString extractKeyTones() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/dtmf";
    QDir().mkpath(dir);
    for (const char* name : {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "star", "hash"}) {
        QFile src(QStringLiteral(":/dtmf/%1.wav").arg(QString::fromLatin1(name)));
        if (!src.open(QIODevice::ReadOnly)) continue;
        QFile dst(dir + "/" + QString::fromLatin1(name) + ".wav");
        if (dst.open(QIODevice::WriteOnly | QIODevice::Truncate)) dst.write(src.readAll());
    }
    return dir;
}

}  // namespace

namespace corasip::ui {

// Bump this when the disclaimer text changes: everyone is asked again.
constexpr const char* kDisclaimerVersion = "1";

// Shows the disclaimer until the user accepts it. Returns false if the user quits instead.
bool acceptDisclaimer(Settings& settings, const std::string& settingsPath) {
    if (settings.get("general", "disclaimer_accepted", "") == kDisclaimerVersion) return true;
    QDialog dlg;
    dlg.setWindowTitle(QObject::tr("corasip: before you start"));
    auto* text = new QLabel(QObject::tr(
        "<b>corasip is free software, provided as is.</b><br>"
        "It comes with no warranty of any kind, as stated in the GNU General Public License (sections 15 and 16). "
        "The authors are not liable for any loss or damage from using it.<br><br>"
        "<b>It is not for emergency calls.</b> Do not rely on it to reach emergency services.<br><br>"
        "Calls, contacts and call history stay on this computer. corasip sends no telemetry.<br><br>"
        "You are responsible for the calls you make and for the accounts you use, and for complying with the law "
        "where you are."), &dlg);
    text->setWordWrap(true);
    text->setTextFormat(Qt::RichText);
    auto* accept = new QCheckBox(QObject::tr("I have read and accept these terms"), &dlg);
    accept->setAccessibleName(QObject::tr("Accept the terms"));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    buttons->button(QDialogButtonBox::Ok)->setText(QObject::tr("Start corasip"));
    buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    QObject::connect(accept, &QCheckBox::toggled, buttons->button(QDialogButtonBox::Ok), &QPushButton::setEnabled);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    auto* column = new QVBoxLayout(&dlg);
    column->addWidget(text);
    column->addWidget(accept);
    column->addWidget(buttons);
    dlg.setMinimumWidth(420);
    if (dlg.exec() != QDialog::Accepted) return false;
    settings.set("general", "disclaimer_accepted", kDisclaimerVersion);
    std::string err;
    settings.save(settingsPath, err);
    return true;
}

}  // namespace corasip::ui

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("corasip"));
    app.setDesktopFileName(QStringLiteral("corasip"));  // taskbar groups the window with its launcher
    app.setWindowIcon(QIcon(QStringLiteral(":/corasip.svg")));

    // Translation framework: strings are English in the source. A compiled-in corasip_<locale>.qm is used when present.
    QTranslator translator;
    if (translator.load(QStringLiteral("corasip_") + QLocale::system().name(), QStringLiteral(":/i18n"))) {
        app.installTranslator(&translator);
    }

    const std::string settingsPath = corasip::defaultSettingsPath();
    corasip::Settings settings;
    std::string err;
    if (!settings.load(settingsPath, err)) {
        QMessageBox::warning(nullptr, QStringLiteral("corasip"),
                             QStringLiteral("Could not read settings: %1").arg(QString::fromStdString(err)));
    }

    QDir().mkpath(dataDir());
    corasip::History history;
    if (!history.open((dataDir() + "/history.db").toStdString(), err)) {
        QMessageBox::warning(nullptr, QStringLiteral("corasip"),
                             QStringLiteral("Call history unavailable: %1").arg(QString::fromStdString(err)));
    }

    corasip::ContactStore contacts;
    if (!contacts.open((dataDir() + "/history.db").toStdString(), err)) {
        QMessageBox::warning(nullptr, QStringLiteral("corasip"),
                             QStringLiteral("Contacts unavailable: %1").arg(QString::fromStdString(err)));
    }

    corasip::ui::PhoneEngine phone;
    corasip::ui::MainWindow window(settings, settingsPath, history, contacts, phone, extractRingtone().toStdString(),
                                   extractKeyTones().toStdString());
    // The first start (and after the disclaimer changes) asks the user to read and accept it before the app opens.
    if (!corasip::ui::acceptDisclaimer(settings, settingsPath)) return 0;
    window.show();
    // A sip: or tel: link given on the command line dials (M5E). A link from the desktop starts this command.
    for (const QString& arg : app.arguments()) {
        if (arg.startsWith(QLatin1String("sip:")) || arg.startsWith(QLatin1String("tel:"))) {
            window.openUri(arg);
            break;
        }
    }
    const int rc = app.exec();
    phone.engine().stop();
    return rc;
}
