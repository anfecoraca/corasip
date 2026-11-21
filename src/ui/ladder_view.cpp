#include "ui/ladder_view.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QPen>

namespace corasip::ui {

LadderView::LadderView(QWidget* parent) : QWidget(parent) {
    setMinimumWidth(260);
    setAccessibleName(tr("Call ladder"));
}

void LadderView::setItems(const QVector<Item>& items) {
    items_ = items;
    updateGeometry();
    update();
}

QSize LadderView::sizeHint() const {
    return QSize(340, kHeader + kRowHeight * qMax(1, items_.size()) + 12);
}

QSize LadderView::minimumSizeHint() const {
    return QSize(260, kHeader + kRowHeight * qMax(1, items_.size()) + 12);
}

void LadderView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const int left = width() * 22 / 100;
    const int right = width() * 78 / 100;
    const QColor life(120, 120, 120);
    p.setPen(QPen(life, 1, Qt::DashLine));
    p.drawLine(left, kHeader, left, height());
    p.drawLine(right, kHeader, right, height());
    p.setPen(palette().color(QPalette::WindowText));
    p.drawText(QRect(0, 0, left * 2, kHeader), Qt::AlignCenter, tr("This phone"));
    p.drawText(QRect(right - left, 0, left * 2, kHeader), Qt::AlignCenter, tr("Other end"));

    QFont small = font();
    small.setPointSize(qMax(7, font().pointSize() - 1));
    p.setFont(small);
    for (int i = 0; i < items_.size(); ++i) {
        const Item& it = items_[i];
        const int y = kHeader + kRowHeight * i + kRowHeight / 2;
        const QColor colour = it.response ? QColor(46, 125, 80) : QColor(46, 111, 214);
        const int x1 = it.outgoing ? left : right;
        const int x2 = it.outgoing ? right : left;
        p.setPen(QPen(colour, 2));
        p.drawLine(x1, y, x2, y);
        // Arrowhead at the receiving end.
        const int dir = x2 > x1 ? -1 : 1;
        p.drawLine(x2, y, x2 + dir * 7, y - 4);
        p.drawLine(x2, y, x2 + dir * 7, y + 4);
        p.setPen(colour);
        p.drawText(QRect(qMin(x1, x2), y - kRowHeight / 2, qAbs(x2 - x1), kRowHeight / 2 - 1),
                   Qt::AlignCenter, it.label);
    }
}

void LadderView::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    const int index = (event->pos().y() - kHeader) / kRowHeight;
    if (index >= 0 && index < items_.size()) emit rowClicked(index);
}

}  // namespace corasip::ui
