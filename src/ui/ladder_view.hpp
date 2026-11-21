// Call ladder for the trace panel (M6): one arrow per SIP message, between this phone and the other end.
#pragma once

#include <QString>
#include <QVector>
#include <QWidget>

namespace corasip::ui {

class LadderView : public QWidget {
    Q_OBJECT
public:
    struct Item {
        bool outgoing = false;   // true: this phone sent it
        QString label;           // "INVITE", "100 Trying", "180 Ringing", "200 OK", "ACK", "BYE"
        bool response = false;   // a SIP status line
    };

    explicit LadderView(QWidget* parent = nullptr);
    void setItems(const QVector<Item>& items);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void rowClicked(int index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    QVector<Item> items_;
    static constexpr int kRowHeight = 30;
    static constexpr int kHeader = 28;
};

}  // namespace corasip::ui
