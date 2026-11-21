// Account dialog (M2): add or edit the one active account (SPEC D12).
#pragma once

#include "engine/engine.hpp"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;

namespace corasip::ui {

class AccountDialog : public QDialog {
    Q_OBJECT
public:
    AccountDialog(const AccountSettings& current, bool storePassword, QWidget* parent = nullptr);
    AccountSettings account() const;
    bool storePassword() const;

private slots:
    void tryAccept();

private:
    QLineEdit* server_;
    QLineEdit* user_;
    QLineEdit* domain_;
    QLineEdit* password_;
    QComboBox* transport_;
    QCheckBox* srtp_;
    QCheckBox* storePassword_;
};

}  // namespace corasip::ui
