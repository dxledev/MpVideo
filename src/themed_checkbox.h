#pragma once
#include <QCheckBox>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionButton>

namespace mpvideo {
class ThemedCheckBox : public QCheckBox {
  public:
    explicit ThemedCheckBox(const QString &label, QWidget *parent = nullptr) : QCheckBox(label, parent) {
        setCursor(Qt::PointingHandCursor);
    }

  protected:
    void paintEvent(QPaintEvent *event) override {
        QCheckBox::paintEvent(event);
        QStyleOptionButton option;
        initStyleOption(&option);
        const auto indicator = style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, this);
        const auto colors = option.palette;
        const auto accent = colors.color(QPalette::Highlight);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(
            QPen(hasFocus() || underMouse() || isChecked() ? accent : colors.color(QPalette::Mid), 1));
        painter.setBrush(isChecked() ? accent : colors.color(QPalette::Button));
        painter.drawRoundedRect(QRectF(indicator).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
        if (isChecked()) {
            painter.setPen(QPen(colors.color(QPalette::HighlightedText), 1.7, Qt::SolidLine, Qt::RoundCap,
                                Qt::RoundJoin));
            painter.drawPolyline(QPolygonF{QPointF(indicator.left() + indicator.width() * 0.23,
                                                   indicator.top() + indicator.height() * 0.5),
                                           QPointF(indicator.left() + indicator.width() * 0.43,
                                                   indicator.top() + indicator.height() * 0.7),
                                           QPointF(indicator.left() + indicator.width() * 0.77,
                                                   indicator.top() + indicator.height() * 0.3)});
        }
    }
};
}
