#pragma once
#include <QMouseEvent>
#include <QPainter>
#include <QSlider>
#include <QStyle>
namespace mpvideo {
class SeekSlider final : public QSlider {
  public:
    SeekSlider() : QSlider(Qt::Horizontal) {
        setMouseTracking(true);
        setCursor(Qt::PointingHandCursor);
    }

  protected:
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() != Qt::LeftButton) {
            QSlider::mousePressEvent(event);
            return;
        }
        setSliderDown(true);
        seekAt(event->position().x());
        event->accept();
    }
    void paintEvent(QPaintEvent *event) override {
        QSlider::paintEvent(event);
        if (hoverX_ < 0 || !isEnabled())
            return;
        // Preview of the seek target: tint the span between the playhead and the pointer.
        const int head = QStyle::sliderPositionFromValue(minimum(), maximum(), value(), width(),
                                                         invertedAppearance());
        QColor accent = palette().color(QPalette::Highlight);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        accent.setAlpha(110);
        p.setBrush(accent);
        const qreal cy = height() / 2.0;
        p.drawRoundedRect(QRectF(qMin<qreal>(head, hoverX_), cy - 3, qAbs(hoverX_ - head), 6), 3, 3);
        accent.setAlpha(255);
        p.setBrush(accent);
        p.drawEllipse(QPointF(hoverX_, cy), 5, 5);
    }
    void leaveEvent(QEvent *event) override {
        hoverX_ = -1;
        update();
        QSlider::leaveEvent(event);
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        hoverX_ = isSliderDown() ? -1 : qBound(0.0, event->position().x(), qreal(width()));
        update();
        if (isSliderDown()) {
            seekAt(event->position().x());
            event->accept();
        } else
            QSlider::mouseMoveEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        if (event->button() != Qt::LeftButton || !isSliderDown()) {
            QSlider::mouseReleaseEvent(event);
            return;
        }
        seekAt(event->position().x());
        setSliderDown(false);
        event->accept();
    }

  private:
    qreal hoverX_ = -1;
    void seekAt(qreal x) {
        setValue(QStyle::sliderValueFromPosition(minimum(), maximum(), qBound(0, qRound(x), width()),
                                                 qMax(1, width()), invertedAppearance()));
    }
};
}
