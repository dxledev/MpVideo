#pragma once
#include <QEasingCurve>
#include <QPainter>
#include <QVariantAnimation>
#include <QWidget>

namespace mpvideo {
// Round play/pause badge flashed over the video; visible for one second, fading out at the end.
class PauseIndicator : public QWidget {
  public:
    explicit PauseIndicator(QWidget *parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFixedSize(84, 84);
        hide();
        anim_ = new QVariantAnimation(this);
        anim_->setDuration(1000);
        anim_->setKeyValueAt(0.0, 1.0);
        anim_->setKeyValueAt(0.7, 1.0);
        anim_->setKeyValueAt(1.0, 0.0);
        connect(anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
            opacity_ = v.toReal();
            update();
        });
        connect(anim_, &QVariantAnimation::finished, this, &QWidget::hide);
    }
    void flash(bool playing) {
        skip_ = 0;
        playing_ = playing;
        restart();
    }
    // direction < 0 shows a rewind badge, > 0 a fast-forward badge, both with the amount of time.
    void flashSkip(int direction, int seconds) {
        skip_ = direction;
        label_ = QString("%1%2s").arg(direction < 0 ? QStringLiteral("\u2212") : "+").arg(seconds);
        restart();
    }

  protected:
    void restart() {
        opacity_ = 1;
        show();
        raise();
        anim_->stop();
        anim_->start();
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setOpacity(opacity_);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 160));
        p.drawEllipse(rect());
        p.setBrush(Qt::white);
        const QPointF c = rect().center();
        if (skip_) {
            // Two chevron triangles pointing the way the video jumps, amount of time underneath.
            p.save();
            p.translate(c.x(), c.y() - 10);
            if (skip_ < 0)
                p.scale(-1, 1);
            for (qreal x : {-14.0, 1.0})
                p.drawPolygon(QPolygonF{QPointF(x, -9), QPointF(x + 13, 0), QPointF(x, 9)});
            p.restore();
            QFont f = p.font();
            f.setPixelSize(14);
            f.setBold(true);
            p.setFont(f);
            p.setPen(Qt::white);
            p.drawText(QRectF(0, c.y() + 6, width(), 24), Qt::AlignCenter, label_);
        } else if (playing_) {
            p.drawPolygon(QPolygonF{c + QPointF(-10, -18), c + QPointF(20, 0), c + QPointF(-10, 18)});
        } else {
            p.drawRoundedRect(QRectF(c.x() - 15, c.y() - 17, 10, 34), 2, 2);
            p.drawRoundedRect(QRectF(c.x() + 5, c.y() - 17, 10, 34), 2, 2);
        }
    }

  private:
    QVariantAnimation *anim_;
    qreal opacity_ = 0;
    bool playing_ = false;
    int skip_ = 0;
    QString label_;
};
} // namespace mpvideo
