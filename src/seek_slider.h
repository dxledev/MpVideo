#pragma once
#include <QMouseEvent>
#include <QSlider>
#include <QStyle>
namespace mpvideo {
class SeekSlider final : public QSlider {
  public:
    SeekSlider() : QSlider(Qt::Horizontal) {}

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
    void mouseMoveEvent(QMouseEvent *event) override {
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
    void seekAt(qreal x) {
        setValue(QStyle::sliderValueFromPosition(minimum(), maximum(), qBound(0, qRound(x), width()),
                                                 qMax(1, width()), invertedAppearance()));
    }
};
}
