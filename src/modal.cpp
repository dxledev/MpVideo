#include "modal.h"
#include "icon_button.h"
#include <QApplication>
#include <QGraphicsBlurEffect>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QTimer>
#include <algorithm>
namespace mpvideo {
namespace {
QPixmap paddedSnapshot(const QPixmap &pixmap, int padding) {
    auto image = pixmap.toImage().convertToFormat(QImage::Format_RGB32);
    QImage padded(image.size() + QSize(2 * padding, 2 * padding), QImage::Format_RGB32);
    for (int y = 0; y < padded.height(); ++y) {
        auto *source = reinterpret_cast<const QRgb *>(
            image.constScanLine(std::clamp(y - padding, 0, image.height() - 1)));
        auto *target = reinterpret_cast<QRgb *>(padded.scanLine(y));
        std::fill_n(target, padding, source[0]);
        std::copy_n(source, image.width(), target + padding);
        std::fill_n(target + padding + image.width(), padding, source[image.width() - 1]);
    }
    return QPixmap::fromImage(padded);
}
} // namespace

Modal::Modal(QWidget *parent, QWidget *background, const QString &title)
    : QWidget(parent), background_(background) {
    setObjectName("modal");
    setFocusPolicy(Qt::StrongFocus);
    card_ = new QWidget(this);
    card_->setObjectName("modalCard");
    auto *layout = new QVBoxLayout(card_);
    layout->setContentsMargins(24, 20, 24, 24);
    layout->setSpacing(16);
    auto *header = new QHBoxLayout;
    auto *heading = new QLabel(title);
    heading->setObjectName("heading");
    header->addWidget(heading, 1);
    auto *close = new IconButton("close", "Close dialog");
    header->addWidget(close);
    connect(close, &QPushButton::clicked, this, &Modal::dismiss);
    layout->addLayout(header);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget;
    body_ = new QVBoxLayout(content);
    body_->setContentsMargins(0, 0, 0, 0);
    body_->setSpacing(12);
    scroll->setWidget(content);
    layout->addWidget(scroll, 1);
    footer_ = new QHBoxLayout;
    layout->addLayout(footer_);
    hide();
}
void Modal::capture() {
    // A reduced snapshot is blurred only when opening/resizing, never per video
    // frame.
    auto image = background_->grab().scaled(QSize(480, 320), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    image.setDevicePixelRatio(1);
    if (image.isNull())
        return;
    QGraphicsScene scene;
    auto *item = scene.addPixmap(paddedSnapshot(image, 32));
    auto *blur = new QGraphicsBlurEffect;
    blur->setBlurRadius(5);
    blur->setBlurHints(QGraphicsBlurEffect::QualityHint);
    item->setGraphicsEffect(blur);
    snapshot_ = QPixmap(image.size());
    snapshot_.fill(Qt::transparent);
    QPainter p(&snapshot_);
    scene.render(&p, QRectF(QPointF(), image.size()), QRectF(QPointF(32, 32), image.size()));
}
void Modal::fit() {
    int cardHeight =
        qMin((preferredHeight_ > 0 ? preferredHeight_ : qBound(300, body_->sizeHint().height() + 110, 550)),
             height() - 48);
    int cardWidth = qMin(590, width() - 32);
    card_->setGeometry((width() - cardWidth) / 2, (height() - cardHeight) / 2, cardWidth, cardHeight);
}
void Modal::present() {
    previousFocus_ = QApplication::focusWidget();
    setGeometry(parentWidget()->rect());
    capture();
    background_->setEnabled(false);
    show();
    raise();
    fit();
    setFocus();
}
void Modal::dismiss() {
    if (closing_)
        return;
    closing_ = true;
    background_->setEnabled(true);
    hide();
    snapshot_ = QPixmap();
    if (previousFocus_)
        previousFocus_->setFocus();
    emit dismissed();
    deleteLater();
}
void Modal::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawPixmap(rect(), snapshot_);
    auto tint = palette().window().color();
    tint.setAlpha(110);
    p.fillRect(rect(), tint);
}
void Modal::changeEvent(QEvent *event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange && isVisible()) {
        capture();
        update();
    }
}
void Modal::resizeEvent(QResizeEvent *) { fit(); }
void Modal::mousePressEvent(QMouseEvent *event) {
    if (!card_->geometry().contains(event->position().toPoint()))
        dismiss();
}
void Modal::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape)
        dismiss();
    else
        QWidget::keyPressEvent(event);
}
} // namespace mpvideo
