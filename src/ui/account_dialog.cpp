#include "ui/account_dialog.hpp"

#include "app/account_validation.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QVBoxLayout>

namespace corasip::ui {

AccountDialog::AccountDialog(const AccountSettings& current, bool storePassword, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Account"));
    setModal(true);

    server_ = new QLineEdit(current.server.c_str(), this);
    server_->setPlaceholderText(tr("sip.example.com or IP"));
    server_->setAccessibleName(tr("SIP server"));
    user_ = new QLineEdit(current.user.c_str(), this);
    user_->setAccessibleName(tr("Username"));
    domain_ = new QLineEdit(current.domain.c_str(), this);
    domain_->setAccessibleName(tr("Domain"));
    password_ = new QLineEdit(current.password.c_str(), this);
    password_->setEchoMode(QLineEdit::Password);
    password_->setAccessibleName(tr("Password"));

    transport_ = new QComboBox(this);
    transport_->addItem(tr("UDP"), static_cast<int>(Transport::Udp));
    transport_->addItem(tr("TCP"), static_cast<int>(Transport::Tcp));
    transport_->addItem(tr("TLS"), static_cast<int>(Transport::Tls));
    transport_->setCurrentIndex(transport_->findData(static_cast<int>(current.transport)));
    transport_->setAccessibleName(tr("Transport"));

    srtp_ = new QCheckBox(tr("Encrypt media (SRTP)"), this);
    srtp_->setChecked(current.srtp);
    srtp_->setToolTip(tr("Off by default. Needs TLS signalling; calls fail over UDP or TCP."));
    srtp_->setAccessibleName(tr("Encrypt media with SRTP"));

    storePassword_ = new QCheckBox(tr("Remember password (stored only in your settings file, mode 600)"), this);
    storePassword_->setChecked(storePassword);
    storePassword_->setAccessibleName(tr("Remember password"));

    auto* form = new QFormLayout;
    form->addRow(tr("SIP server:"), server_);
    form->addRow(tr("Username:"), user_);
    form->addRow(tr("Domain:"), domain_);
    form->addRow(tr("Password:"), password_);
    form->addRow(tr("Transport:"), transport_);
    form->addRow(QString(), srtp_);
    form->addRow(QString(), storePassword_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &AccountDialog::tryAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

AccountSettings AccountDialog::account() const {
    AccountSettings acc;
    acc.server = server_->text().trimmed().toStdString();
    acc.user = user_->text().trimmed().toStdString();
    acc.domain = domain_->text().trimmed().toStdString();
    acc.password = password_->text().toStdString();
    acc.transport = static_cast<Transport>(transport_->currentData().toInt());
    acc.srtp = srtp_->isChecked();
    acc.srtpRequireSecureSignaling = true;  // SPEC D13: SRTP needs TLS signalling
    return acc;
}

bool AccountDialog::storePassword() const { return storePassword_->isChecked(); }

void AccountDialog::tryAccept() {
    const auto errors = validateAccount(account());
    if (!errors.empty()) {
        QString text;
        for (const auto& e : errors) text += QString::fromStdString(e) + "\n";
        QMessageBox::warning(this, tr("Account"), text.trimmed());
        return;
    }
    accept();
}

}  // namespace corasip::ui
