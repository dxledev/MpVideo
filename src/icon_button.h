#pragma once
#include <QPainter>
#include <QPushButton>

namespace mpvideo {
class IconButton : public QPushButton {
  public:
    explicit IconButton(QString symbol, QString label, QWidget *parent = nullptr)
        : QPushButton(parent), symbol_(std::move(symbol)) {
        setAccessibleName(label);
        setToolTip(label);
        setFixedSize(38, 36);
        setCursor(Qt::PointingHandCursor);
    }
    void setSymbol(QString symbol) {
        symbol_ = std::move(symbol);
        update();
    }

  protected:
    void paintEvent(QPaintEvent *event) override {
        QPushButton::paintEvent(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.translate((width() - 24) / 2.0, (height() - 24) / 2.0);
        QColor color = palette().buttonText().color();
        if (!isEnabled())
            color.setAlpha(90);
        p.setPen(QPen(color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (symbol_ == "play") {
            p.drawPolygon(QPolygonF{QPointF(8, 5), QPointF(19, 12), QPointF(8, 19)});
        } else if (symbol_ == "pause") {
            p.drawLine(8, 5, 8, 19);
            p.drawLine(16, 5, 16, 19);
        } else if (symbol_ == "open") {
            p.drawPolyline(QPolygonF{QPointF(3, 19), QPointF(3, 6), QPointF(10, 6), QPointF(12, 9),
                                     QPointF(21, 9), QPointF(19, 19), QPointF(3, 19), QPointF(6, 11),
                                     QPointF(18, 11)});
        } else if (symbol_ == "queue") {
            for (int y : {6, 12, 18}) {
                p.drawLine(4, y, 5, y);
                p.drawLine(9, y, 20, y);
            }
        } else if (symbol_ == "full") {
            p.drawPolyline(QPolygonF{QPointF(9, 4), QPointF(4, 4), QPointF(4, 9)});
            p.drawPolyline(QPolygonF{QPointF(15, 4), QPointF(20, 4), QPointF(20, 9)});
            p.drawPolyline(QPolygonF{QPointF(4, 15), QPointF(4, 20), QPointF(9, 20)});
            p.drawPolyline(QPolygonF{QPointF(15, 20), QPointF(20, 20), QPointF(20, 15)});
        } else if (symbol_ == "volume" || symbol_ == "mute") {
            p.drawPolygon(QPolygonF{QPointF(3, 9), QPointF(7, 9), QPointF(12, 5), QPointF(12, 19),
                                    QPointF(7, 15), QPointF(3, 15)});
            if (symbol_ == "volume")
                p.drawArc(10, 5, 11, 14, -70 * 16, 140 * 16);
            else {
                p.drawLine(16, 9, 22, 15);
                p.drawLine(22, 9, 16, 15);
            }
        } else if (symbol_ == "next" || symbol_ == "previous") {
            if (symbol_ == "previous") {
                p.translate(24, 0);
                p.scale(-1, 1);
            }
            p.drawPolygon(QPolygonF{QPointF(5, 5), QPointF(16, 12), QPointF(5, 19)});
            p.drawLine(19, 5, 19, 19);
        } else if (symbol_ == "settings") {
            p.drawLine(4, 7, 20, 7);
            p.drawLine(4, 17, 20, 17);
            p.drawEllipse(QPointF(9, 7), 2.5, 2.5);
            p.drawEllipse(QPointF(15, 17), 2.5, 2.5);
        } else if (symbol_ == "tracks") {
            p.drawRoundedRect(QRectF(3, 5, 18, 14), 2, 2);
            p.drawLine(6, 14, 10, 14);
            p.drawLine(13, 14, 18, 14);
            p.drawLine(6, 10, 13, 10);
        } else {
            p.drawLine(6, 6, 18, 18);
            p.drawLine(18, 6, 6, 18);
        }
    }

  private:
    QString symbol_;
};
} // namespace mpvideo
