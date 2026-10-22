#include "tooltip.h"
#include <QApplication>
#include <QEvent>
#include <QHelpEvent>
#include <QLabel>
#include <QTimer>
#include <QWidget>
namespace mpvideo {
bool TooltipFilter::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::ToolTip) {
        auto *widget = qobject_cast<QWidget *>(watched);
        if (!widget || widget->toolTip().isEmpty())
            return true;
        if (label_)
            delete label_.data();
        auto *host = widget->window();
        label_ = new QLabel(widget->toolTip(), host);
        label_->setTextFormat(Qt::PlainText);
        label_->setWordWrap(true);
        label_->setMaximumWidth(qMin(340, host->width() - 16));
        label_->setMargin(8);
        label_->setObjectName("embeddedTooltip");
        label_->setAttribute(Qt::WA_TransparentForMouseEvents);
        label_->setAutoFillBackground(true);
        label_->setBackgroundRole(QPalette::Base);
        label_->adjustSize();
        auto pos = host->mapFromGlobal(static_cast<QHelpEvent *>(event)->globalPos()) + QPoint(8, 16);
        pos.setX(qBound(8, pos.x(), qMax(8, host->width() - label_->width() - 8)));
        pos.setY(qBound(8, pos.y(), qMax(8, host->height() - label_->height() - 8)));
        label_->move(pos);
        label_->show();
        label_->raise();
        QTimer::singleShot(4500, label_, &QWidget::hide);
        return true;
    }
    if (label_ && (event->type() == QEvent::Leave || event->type() == QEvent::MouseButtonPress ||
                   event->type() == QEvent::KeyPress))
        label_->hide();
    return QObject::eventFilter(watched, event);
}
} // namespace mpvideo
