#include "window.h"

#include "../modal.h"
#include "../player.h"
#include "../video_widget.h"
#include "../theme/theme_manager.hpp"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QMimeData>
#include <QStatusBar>
#include <QPushButton>
#include <QRadioButton>
#include <QResizeEvent>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QStackedLayout>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {
enum class Glyph {
    Play,
    Pause,
    Previous,
    Next,
    SeekBack,
    SeekForward,
    Volume,
    Muted,
    Fullscreen,
    Windowed,
    Close
};

class GlyphButton final : public QToolButton {
public:
    GlyphButton(Glyph glyph, const QString &label, QWidget *parent = nullptr)
        : QToolButton(parent), glyph_(glyph) {
        setAccessibleName(label);
        setToolTip(label);
        setAutoRaise(true);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setFixedSize(38, 36);
    }

    void setGlyph(Glyph glyph) {
        if (glyph_ == glyph) return;
        glyph_ = glyph;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        QToolButton::paintEvent(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.translate((width() - 24) / 2.0, (height() - 24) / 2.0);
        QColor ink = palette().color(QPalette::ButtonText);
        if (!isEnabled()) ink.setAlpha(90);
        QPen pen(ink, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);

        switch (glyph_) {
        case Glyph::Play: {
            QPainterPath path;
            path.moveTo(7, 4);
            path.lineTo(19, 12);
            path.lineTo(7, 20);
            path.closeSubpath();
            painter.setPen(Qt::NoPen);
            painter.setBrush(ink);
            painter.drawPath(path);
            break;
        }
        case Glyph::Pause:
            painter.setPen(Qt::NoPen);
            painter.setBrush(ink);
            painter.drawRoundedRect(QRectF(6, 4, 4, 16), 1.2, 1.2);
            painter.drawRoundedRect(QRectF(14, 4, 4, 16), 1.2, 1.2);
            break;
        case Glyph::Previous:
        case Glyph::Next: {
            if (glyph_ == Glyph::Previous) {
                painter.translate(24, 0);
                painter.scale(-1, 1);
            }
            painter.drawLine(19, 5, 19, 19);
            QPainterPath path;
            path.moveTo(16, 5);
            path.lineTo(5, 12);
            path.lineTo(16, 19);
            path.closeSubpath();
            painter.setPen(Qt::NoPen);
            painter.setBrush(ink);
            painter.drawPath(path);
            break;
        }
        case Glyph::SeekBack:
        case Glyph::SeekForward: {
            if (glyph_ == Glyph::SeekBack) {
                painter.translate(24, 0);
                painter.scale(-1, 1);
            }
            painter.drawArc(QRectF(4, 5, 16, 16), 45 * 16, 270 * 16);
            QPainterPath arrow;
            arrow.moveTo(3, 5);
            arrow.lineTo(9, 5);
            arrow.lineTo(5, 10);
            arrow.closeSubpath();
            painter.setPen(Qt::NoPen);
            painter.setBrush(ink);
            painter.drawPath(arrow);
            painter.setPen(pen);
            painter.drawText(QRectF(5, 7, 14, 12), Qt::AlignCenter, "5");
            break;
        }
        case Glyph::Volume:
        case Glyph::Muted: {
            QPainterPath speaker;
            speaker.moveTo(3, 9);
            speaker.lineTo(7, 9);
            speaker.lineTo(12, 5);
            speaker.lineTo(12, 19);
            speaker.lineTo(7, 15);
            speaker.lineTo(3, 15);
            speaker.closeSubpath();
            painter.setPen(Qt::NoPen);
            painter.setBrush(ink);
            painter.drawPath(speaker);
            painter.setPen(pen);
            if (glyph_ == Glyph::Volume) {
                painter.drawArc(QRectF(11, 6, 10, 12), -65 * 16, 130 * 16);
                painter.drawArc(QRectF(10, 3, 16, 18), -65 * 16, 130 * 16);
            } else {
                painter.drawLine(16, 8, 22, 16);
                painter.drawLine(22, 8, 16, 16);
            }
            break;
        }
        case Glyph::Fullscreen:
        case Glyph::Windowed: {
            if (glyph_ == Glyph::Fullscreen) {
                painter.drawLine(4, 9, 4, 4);
                painter.drawLine(4, 4, 9, 4);
                painter.drawLine(15, 4, 20, 4);
                painter.drawLine(20, 4, 20, 9);
                painter.drawLine(4, 15, 4, 20);
                painter.drawLine(4, 20, 9, 20);
                painter.drawLine(15, 20, 20, 20);
                painter.drawLine(20, 20, 20, 15);
            } else {
                painter.drawRoundedRect(QRectF(4, 5, 16, 14), 2, 2);
                painter.drawLine(4, 9, 20, 9);
                painter.drawLine(8, 4, 8, 7);
                painter.drawLine(16, 4, 16, 7);
            }
            break;
        }
        case Glyph::Close:
            painter.drawLine(6, 6, 18, 18);
            painter.drawLine(18, 6, 6, 18);
            break;
        }
    }

private:
    Glyph glyph_;
};

class BrandMark final : public QWidget {
public:
    explicit BrandMark(QWidget *parent = nullptr) : QWidget(parent) {
        setFixedSize(28, 28);
        setAccessibleName("MpVideo");
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QColor accent = palette().color(QPalette::Highlight);
        painter.setPen(Qt::NoPen);
        painter.setBrush(accent);
        painter.drawRoundedRect(QRectF(1, 1, 26, 26), 7, 7);
        painter.setBrush(palette().color(QPalette::HighlightedText));
        QPainterPath mark;
        mark.moveTo(10, 7);
        mark.lineTo(20, 14);
        mark.lineTo(10, 21);
        mark.closeSubpath();
        painter.drawPath(mark);
    }
};

class FocusModal final : public mpvideo::Modal {
public:
    using mpvideo::Modal::Modal;

    void present(QWidget *initialFocus) {
        mpvideo::Modal::present();
        qApp->installEventFilter(this);
        if (initialFocus && initialFocus->isEnabled() && initialFocus->isVisibleTo(this)) {
            initialFocus->setFocus(Qt::OtherFocusReason);
        } else {
            focusFirst(false);
        }
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (!isVisible() || event->type() != QEvent::KeyPress) return false;
        auto *widget = qobject_cast<QWidget *>(watched);
        if (!isDescendant(widget)) return false;
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() != Qt::Key_Tab && key->key() != Qt::Key_Backtab) return false;
        focusFirst(key->key() == Qt::Key_Backtab || key->modifiers().testFlag(Qt::ShiftModifier));
        key->accept();
        return true;
    }

private:
    bool isDescendant(QWidget *widget) const {
        for (auto *current = widget; current; current = current->parentWidget())
            if (current == this) return true;
        return false;
    }

    void focusFirst(bool backwards) {
        auto focusable = findChildren<QWidget *>();
        focusable.erase(std::remove_if(focusable.begin(), focusable.end(), [this](QWidget *widget) {
            return !widget->isEnabled() || !widget->isVisibleTo(this) ||
                   !(widget->focusPolicy() & Qt::TabFocus);
        }), focusable.end());
        if (focusable.isEmpty()) return;
        QWidget *current = QApplication::focusWidget();
        int index = focusable.indexOf(current);
        if (index < 0) index = backwards ? 0 : -1;
        index = (index + (backwards ? -1 : 1) + focusable.size()) % focusable.size();
        focusable[index]->setFocus(backwards ? Qt::BacktabFocusReason : Qt::TabFocusReason);
    }
};

QColor themeColor(const QMap<QString, QColor> &colors, const QString &key, const QColor &fallback) {
    const QColor color = colors.value(key);
    return color.isValid() ? color : fallback;
}

QString pathFromPlaylistItem(const QVariant &item) {
    const QVariantMap map = item.toMap();
    for (const QString &key : {QStringLiteral("filename"), QStringLiteral("path"), QStringLiteral("file")}) {
        const QString value = map.value(key).toString();
        if (!value.isEmpty()) return value;
    }
    return item.toString();
}

QString labelFromPlaylistItem(const QVariant &item) {
    const QVariantMap map = item.toMap();
    const QString title = map.value(QStringLiteral("title")).toString();
    if (!title.isEmpty()) return title;
    const QString path = pathFromPlaylistItem(item);
    const QString fileName = QFileInfo(path).fileName();
    return fileName.isEmpty() ? path : fileName;
}

QString formatTime(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0) seconds = 0;
    const auto total = static_cast<qint64>(seconds);
    const qint64 hours = total / 3600;
    const qint64 minutes = (total / 60) % 60;
    const qint64 remainder = total % 60;
    if (hours > 0)
        return QStringLiteral("%1:%2:%3").arg(hours).arg(minutes, 2, 10, QLatin1Char('0')).arg(remainder, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(total / 60).arg(remainder, 2, 10, QLatin1Char('0'));
}

QPushButton *textButton(const QString &text, const QString &accessibleName, QWidget *parent = nullptr) {
    auto *button = new QPushButton(text, parent);
    button->setAccessibleName(accessibleName);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

QFrame *separator(QWidget *parent = nullptr) {
    auto *line = new QFrame(parent);
    line->setFrameShape(QFrame::VLine);
    line->setFrameShadow(QFrame::Plain);
    line->setFixedHeight(22);
    return line;
}
}

Window::Window(QString configDirectory, QStringList files)
    : configDirectory_(std::move(configDirectory)) {
    setWindowTitle(QStringLiteral("MpVideo"));
    setMinimumSize(740, 540);
    setAcceptDrops(true);
    player_ = new mpvideo::Player(this);
    setCentralWidget(buildInterface());

    themeManager_ = new mpvideo::ThemeManager(configDirectory_, this);
    connect(themeManager_, &mpvideo::ThemeManager::changed, this, [this] {
        applyTheme(themeManager_->current().colors);
    });
    connect(themeManager_, &mpvideo::ThemeManager::rejected, this, [this](const QString &message) {
        showError(QStringLiteral("Theme unchanged: %1").arg(message));
    });
    themeManager_->reload();
    applyTheme(themeManager_->current().colors);

    connect(player_, &mpvideo::Player::changed, this, &Window::updatePlayerState);
    connect(player_, &mpvideo::Player::error, this, &Window::showError);
    connect(video_, &mpvideo::VideoWidget::error, this, &Window::showError);
    createShortcuts();
    updatePlayerState();

    if (!files.isEmpty()) {
        QTimer::singleShot(0, this, [this, files = std::move(files)] { openFiles(files); });
    }
}

mpvideo::Player *Window::player() const {
    return player_;
}

void Window::applyTheme(QMap<QString, QColor> colors) {
    themeColors_ = std::move(colors);
    applyThemeStyle();
    update();
}

QWidget *Window::buildInterface() {
    auto *root = new QWidget(this);
    root->setObjectName(QStringLiteral("windowSurface"));
    root->setAcceptDrops(true);
    root->installEventFilter(this);
    auto *rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(18, 14, 18, 14);
    rootLayout->setSpacing(12);

    header_ = new QWidget(root);
    auto *headerLayout = new QHBoxLayout(header_);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(8);

    auto *brand = new BrandMark(header_);
    headerLayout->addWidget(brand);

    auto *brandName = new QLabel(QStringLiteral("MpVideo"), header_);
    QFont brandFont = brandName->font();
    brandFont.setPointSize(15);
    brandFont.setWeight(QFont::DemiBold);
    brandName->setFont(brandFont);
    headerLayout->addWidget(brandName);

    mediaTitle_ = new QLabel(QStringLiteral("Open media to begin"), header_);
    mediaTitle_->setObjectName(QStringLiteral("mediaTitle"));
    mediaTitle_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    mediaTitle_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    headerLayout->addWidget(mediaTitle_, 1);
    headerLayout->addWidget(separator(header_));

    auto *openButton = textButton(QStringLiteral("Open"), QStringLiteral("Open media files"), header_);
    openButton->setObjectName(QStringLiteral("primaryButton"));
    connect(openButton, &QPushButton::clicked, this, &Window::showFilePicker);
    headerLayout->addWidget(openButton);

    auto *urlButton = textButton(QStringLiteral("URL"), QStringLiteral("Open a media URL"), header_);
    connect(urlButton, &QPushButton::clicked, this, &Window::showUrlDialog);
    headerLayout->addWidget(urlButton);

    auto *tracksButton = textButton(QStringLiteral("Tracks"), QStringLiteral("Choose audio and subtitle tracks"), header_);
    connect(tracksButton, &QPushButton::clicked, this, &Window::showTracksDialog);
    headerLayout->addWidget(tracksButton);

    playlistToggle_ = textButton(QStringLiteral("Playlist"), QStringLiteral("Show or hide the playlist"), header_);
    playlistToggle_->setCheckable(true);
    playlistToggle_->setChecked(true);
    connect(playlistToggle_, &QPushButton::toggled, this, [this](bool visible) {
        playlistVisible_ = visible;
        if (playlistPanel_) playlistPanel_->setVisible(visible);
        updateResponsiveLayout();
    });
    headerLayout->addWidget(playlistToggle_);

    auto *settingsButton = textButton(QStringLiteral("Settings"), QStringLiteral("Playback settings"), header_);
    connect(settingsButton, &QPushButton::clicked, this, &Window::showSettingsDialog);
    headerLayout->addWidget(settingsButton);
    rootLayout->addWidget(header_);

    contentSplitter_ = new QSplitter(Qt::Horizontal, root);
    contentSplitter_->setObjectName(QStringLiteral("contentSplitter"));
    contentSplitter_->setHandleWidth(1);
    contentSplitter_->setChildrenCollapsible(false);
    stagePanel_ = buildStage();
    playlistPanel_ = buildPlaylist();
    contentSplitter_->addWidget(stagePanel_);
    contentSplitter_->addWidget(playlistPanel_);
    contentSplitter_->setStretchFactor(0, 1);
    contentSplitter_->setStretchFactor(1, 0);
    contentSplitter_->setSizes({820, 280});
    rootLayout->addWidget(contentSplitter_, 1);
    rootLayout->addWidget(buildTransport());
    return root;
}

QWidget *Window::buildStage() {
    stagePanel_ = new QWidget;
    auto *stageLayout = new QVBoxLayout(stagePanel_);
    stageLayout->setContentsMargins(0, 0, 0, 0);

    auto *stage = new QFrame(stagePanel_);
    stage->setObjectName(QStringLiteral("videoStage"));
    stage->setAcceptDrops(true);
    stage->installEventFilter(this);
    auto *stack = new QStackedLayout(stage);
    stack->setContentsMargins(0, 0, 0, 0);
    stack->setStackingMode(QStackedLayout::StackAll);

    video_ = new mpvideo::VideoWidget(player_, stage);
    video_->setObjectName(QStringLiteral("videoCanvas"));
    video_->setAcceptDrops(true);
    video_->installEventFilter(this);
    stack->addWidget(video_);

    emptyOverlay_ = new QWidget(stage);
    emptyOverlay_->setObjectName(QStringLiteral("videoPlaceholder"));
    emptyOverlay_->setAcceptDrops(true);
    emptyOverlay_->installEventFilter(this);
    auto *emptyLayout = new QVBoxLayout(emptyOverlay_);
    emptyLayout->setContentsMargins(24, 24, 24, 24);
    emptyLayout->setSpacing(10);
    emptyLayout->addStretch(1);

    auto *ready = new QLabel(QStringLiteral("Ready when you are"), emptyOverlay_);
    QFont readyFont = ready->font();
    readyFont.setPointSize(17);
    readyFont.setWeight(QFont::DemiBold);
    ready->setFont(readyFont);
    ready->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(ready);

    auto *instruction = new QLabel(QStringLiteral("Drop a video or audio file here, or choose where to play from."), emptyOverlay_);
    instruction->setAlignment(Qt::AlignCenter);
    instruction->setWordWrap(true);
    emptyLayout->addWidget(instruction);

    auto *actions = new QHBoxLayout;
    actions->setSpacing(8);
    actions->addStretch(1);
    auto *openButton = textButton(QStringLiteral("Open media"), QStringLiteral("Open media files"), emptyOverlay_);
    openButton->setObjectName(QStringLiteral("primaryButton"));
    connect(openButton, &QPushButton::clicked, this, &Window::showFilePicker);
    actions->addWidget(openButton);
    auto *urlButton = textButton(QStringLiteral("Open URL"), QStringLiteral("Open a media URL"), emptyOverlay_);
    connect(urlButton, &QPushButton::clicked, this, &Window::showUrlDialog);
    actions->addWidget(urlButton);
    actions->addStretch(1);
    emptyLayout->addLayout(actions);
    emptyLayout->addStretch(1);
    stack->addWidget(emptyOverlay_);
    stageLayout->addWidget(stage, 1);
    return stagePanel_;
}

QWidget *Window::buildPlaylist() {
    playlistPanel_ = new QWidget;
    playlistPanel_->setObjectName(QStringLiteral("playlistPanel"));
    playlistPanel_->setMinimumWidth(220);
    auto *layout = new QVBoxLayout(playlistPanel_);
    layout->setContentsMargins(14, 10, 10, 10);
    layout->setSpacing(10);

    auto *header = new QHBoxLayout;
    playlistTitle_ = new QLabel(QStringLiteral("Playlist"), playlistPanel_);
    QFont titleFont = playlistTitle_->font();
    titleFont.setWeight(QFont::DemiBold);
    playlistTitle_->setFont(titleFont);
    header->addWidget(playlistTitle_, 1);
    auto *addButton = textButton(QStringLiteral("Add files"), QStringLiteral("Add media to the playlist"), playlistPanel_);
    addButton->setObjectName(QStringLiteral("quietButton"));
    connect(addButton, &QPushButton::clicked, this, &Window::showFilePicker);
    header->addWidget(addButton);
    layout->addLayout(header);

    auto *pages = new QStackedWidget(playlistPanel_);
    playlistList_ = new QListWidget(pages);
    playlistList_->setObjectName(QStringLiteral("playlistList"));
    playlistList_->setSelectionMode(QAbstractItemView::SingleSelection);
    playlistList_->setWordWrap(false);
    playlistList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(playlistList_, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        if (!item) return;
        const QString path = item->data(Qt::UserRole).toString();
        if (!path.isEmpty()) player_->command({QStringLiteral("loadfile"), path, QStringLiteral("replace")});
    });
    pages->addWidget(playlistList_);

    emptyPlaylist_ = new QWidget(pages);
    auto *emptyLayout = new QVBoxLayout(emptyPlaylist_);
    emptyLayout->setContentsMargins(8, 8, 8, 8);
    emptyLayout->addStretch(1);
    auto *emptyLabel = new QLabel(QStringLiteral("No media queued"), emptyPlaylist_);
    emptyLabel->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(emptyLabel);
    auto *addMedia = textButton(QStringLiteral("Add media"), QStringLiteral("Add media to the playlist"), emptyPlaylist_);
    addMedia->setObjectName(QStringLiteral("quietButton"));
    connect(addMedia, &QPushButton::clicked, this, &Window::showFilePicker);
    auto *addLayout = new QHBoxLayout;
    addLayout->addStretch(1);
    addLayout->addWidget(addMedia);
    addLayout->addStretch(1);
    emptyLayout->addLayout(addLayout);
    emptyLayout->addStretch(1);
    pages->addWidget(emptyPlaylist_);
    layout->addWidget(pages, 1);
    return playlistPanel_;
}

QWidget *Window::buildTransport() {
    auto *transport = new QWidget(this);
    auto *layout = new QHBoxLayout(transport);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(5);

    auto *previous = new GlyphButton(Glyph::Previous, QStringLiteral("Previous item"), transport);
    connect(previous, &QToolButton::clicked, this, [this] { player_->command({QStringLiteral("playlist-prev"), QStringLiteral("weak")}); });
    layout->addWidget(previous);

    auto *back = new GlyphButton(Glyph::SeekBack, QStringLiteral("Seek back 5 seconds"), transport);
    connect(back, &QToolButton::clicked, this, [this] { seekRelative(-5.0); });
    layout->addWidget(back);

    playButton_ = new GlyphButton(Glyph::Play, QStringLiteral("Play"), transport);
    playButton_->setObjectName(QStringLiteral("playButton"));
    connect(playButton_, &QToolButton::clicked, this, [this] {
        if (player_->property(QStringLiteral("idle-active")).toBool()) showFilePicker();
        else player_->setProperty(QStringLiteral("pause"), !player_->property(QStringLiteral("pause")).toBool());
    });
    layout->addWidget(playButton_);

    auto *forward = new GlyphButton(Glyph::SeekForward, QStringLiteral("Seek forward 5 seconds"), transport);
    connect(forward, &QToolButton::clicked, this, [this] { seekRelative(5.0); });
    layout->addWidget(forward);

    auto *next = new GlyphButton(Glyph::Next, QStringLiteral("Next item"), transport);
    connect(next, &QToolButton::clicked, this, [this] { player_->command({QStringLiteral("playlist-next"), QStringLiteral("weak")}); });
    layout->addWidget(next);

    positionLabel_ = new QLabel(QStringLiteral("0:00"), transport);
    positionLabel_->setMinimumWidth(42);
    positionLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    positionLabel_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(positionLabel_);

    seekSlider_ = new QSlider(Qt::Horizontal, transport);
    seekSlider_->setAccessibleName(QStringLiteral("Playback position"));
    seekSlider_->setRange(0, 10000);
    seekSlider_->setSingleStep(25);
    seekSlider_->setPageStep(250);
    seekSlider_->setTracking(false);
    seekSlider_->setMinimumWidth(100);
    connect(seekSlider_, &QSlider::sliderMoved, this, [this](int value) {
        pendingSeekValue_ = value;
        if (seekTimer_) seekTimer_->start(75);
    });
    connect(seekSlider_, &QSlider::sliderReleased, this, [this] {
        if (seekTimer_) seekTimer_->stop();
        seekToSliderValue(seekSlider_->value());
    });
    connect(seekSlider_, &QSlider::valueChanged, this, [this](int value) {
        if (!seekSlider_->isSliderDown()) seekToSliderValue(value);
    });
    seekTimer_ = new QTimer(this);
    seekTimer_->setSingleShot(true);
    connect(seekTimer_, &QTimer::timeout, this, [this] { seekToSliderValue(pendingSeekValue_); });
    layout->addWidget(seekSlider_, 1);

    durationLabel_ = new QLabel(QStringLiteral("0:00"), transport);
    durationLabel_->setMinimumWidth(42);
    durationLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    durationLabel_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(durationLabel_);

    volumeButton_ = new GlyphButton(Glyph::Volume, QStringLiteral("Mute"), transport);
    connect(volumeButton_, &QToolButton::clicked, this, [this] {
        if (player_->property(QStringLiteral("volume")).toDouble() > 0.5) {
            volumeBeforeMute_ = player_->property(QStringLiteral("volume")).toDouble();
            volumeSlider_->setValue(0);
        } else {
            volumeSlider_->setValue(qBound(0, qRound(volumeBeforeMute_), 100));
        }
    });
    layout->addWidget(volumeButton_);

    volumeSlider_ = new QSlider(Qt::Horizontal, transport);
    volumeSlider_->setAccessibleName(QStringLiteral("Volume"));
    volumeSlider_->setRange(0, 100);
    volumeSlider_->setFixedWidth(96);
    connect(volumeSlider_, &QSlider::valueChanged, this, [this](int value) {
        player_->setProperty(QStringLiteral("volume"), value);
        if (value > 0) volumeBeforeMute_ = value;
        volumeButton_->setGlyph(value > 0 ? Glyph::Volume : Glyph::Muted);
        volumeButton_->setToolTip(value > 0 ? QStringLiteral("Mute") : QStringLiteral("Restore volume"));
    });
    layout->addWidget(volumeSlider_);

    fullscreenButton_ = new GlyphButton(Glyph::Fullscreen, QStringLiteral("Enter fullscreen"), transport);
    connect(fullscreenButton_, &QToolButton::clicked, this, [this] { setFullscreen(!fullscreen_); });
    layout->addWidget(fullscreenButton_);
    return transport;
}

void Window::createShortcuts() {
    const auto addShortcut = [this](const QKeySequence &sequence, std::function<void()> action) {
        auto *shortcut = new QShortcut(sequence, this);
        shortcut->setContext(Qt::WindowShortcut);
        connect(shortcut, &QShortcut::activated, this, [this, action = std::move(action)] {
            if (!modal_) action();
        });
    };

    addShortcut(QKeySequence(QStringLiteral("Ctrl+O")), [this] { showFilePicker(); });
    addShortcut(QKeySequence(QStringLiteral("Ctrl+L")), [this] { showUrlDialog(); });
    addShortcut(QKeySequence(Qt::Key_Space), [this] {
        QWidget *focus = QApplication::focusWidget();
        if (qobject_cast<QLineEdit *>(focus) || qobject_cast<QSlider *>(focus)) return;
        if (player_->property(QStringLiteral("idle-active")).toBool()) showFilePicker();
        else player_->setProperty(QStringLiteral("pause"), !player_->property(QStringLiteral("pause")).toBool());
    });
    addShortcut(QKeySequence(Qt::Key_Left), [this] { seekRelative(-5.0); });
    addShortcut(QKeySequence(Qt::Key_Right), [this] { seekRelative(5.0); });
    addShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Left), [this] { seekRelative(-10.0); });
    addShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Right), [this] { seekRelative(10.0); });
    addShortcut(QKeySequence(Qt::Key_M), [this] {
        if (player_->property(QStringLiteral("volume")).toDouble() > 0.5) {
            volumeBeforeMute_ = player_->property(QStringLiteral("volume")).toDouble();
            volumeSlider_->setValue(0);
        } else {
            volumeSlider_->setValue(qBound(0, qRound(volumeBeforeMute_), 100));
        }
    });
    addShortcut(QKeySequence(Qt::Key_F), [this] { setFullscreen(!fullscreen_); });
    addShortcut(QKeySequence(Qt::Key_P), [this] { togglePlaylist(); });
    addShortcut(QKeySequence(Qt::Key_Escape), [this] {
        if (fullscreen_) setFullscreen(false);
    });
}

void Window::applyThemeStyle() {
    const QColor background = themeColor(themeColors_, QStringLiteral("background"), QColor(QStringLiteral("#141719")));
    const QColor surface = themeColor(themeColors_, QStringLiteral("surface"), QColor(QStringLiteral("#1c2023")));
    const QColor text = themeColor(themeColors_, QStringLiteral("text"), QColor(QStringLiteral("#eef0f0")));
    const QColor muted = themeColor(themeColors_, QStringLiteral("muted"), QColor(QStringLiteral("#a2aaaf")));
    const QColor accent = themeColor(themeColors_, QStringLiteral("accent"), QColor(QStringLiteral("#b9d5e2")));
    const QColor border = themeColor(themeColors_, QStringLiteral("border"), QColor(QStringLiteral("#343b40")));
    const QColor surfaceAlt = themeColor(themeColors_, QStringLiteral("surface_alt"), surface.lighter(112));
    const QColor accentText = themeColor(themeColors_, QStringLiteral("accent_text"), QColor(QStringLiteral("#101619")));
    const QColor selection = themeColor(themeColors_, QStringLiteral("selection"), accent.darker(155));
    const QColor videoBackground = themeColor(themeColors_, QStringLiteral("video_background"), QColor(Qt::black));
    const QColor control = themeColor(themeColors_, QStringLiteral("control"), surfaceAlt);
    const QColor controlHover = themeColor(themeColors_, QStringLiteral("control_hover"), surfaceAlt.lighter(112));

    QPalette colors = palette();
    colors.setColor(QPalette::Window, background);
    colors.setColor(QPalette::WindowText, text);
    colors.setColor(QPalette::Base, surface);
    colors.setColor(QPalette::AlternateBase, surfaceAlt);
    colors.setColor(QPalette::Text, text);
    colors.setColor(QPalette::Button, control);
    colors.setColor(QPalette::ButtonText, text);
    colors.setColor(QPalette::Highlight, accent);
    colors.setColor(QPalette::HighlightedText, accentText);
    colors.setColor(QPalette::Mid, border);
    colors.setColor(QPalette::Disabled, QPalette::Text, muted);
    setPalette(colors);

    setStyleSheet(QStringLiteral(R"(
        QMainWindow { background: %1; color: %2; }
        QWidget#windowSurface { background: %1; color: %2; }
        QLabel { color: %2; background: transparent; }
        QLabel#mediaTitle { color: %3; }
        QWidget#playlistPanel { background: %4; border-left: 1px solid %6; }
        QFrame#videoStage { background: %10; border: 1px solid %6; border-radius: 9px; }
        QWidget#modalCard { background: %4; border: 1px solid %6; border-radius: 12px; }
        QLabel#heading { color: %2; font-size: 15px; font-weight: 600; }
        QPushButton {
            color: %2; background: %11; border: 1px solid %6; border-radius: 7px;
            padding: 7px 12px; min-height: 20px;
        }
        QPushButton:hover { background: %12; }
        QPushButton:pressed { background: %13; }
        QPushButton:checked { background: %13; border-color: %5; }
        QPushButton#primaryButton { color: %8; background: %5; border-color: %5; font-weight: 600; }
        QPushButton#primaryButton:hover { background: %12; color: %2; }
        QPushButton#quietButton { background: transparent; border-color: transparent; padding-left: 8px; padding-right: 8px; }
        QPushButton#quietButton:hover { background: %12; border-color: %6; }
        QToolButton { color: %2; background: transparent; border: 1px solid transparent; border-radius: 8px; padding: 3px; }
        QToolButton:hover { background: %12; border-color: %6; }
        QToolButton:pressed { background: %13; }
        QToolButton:focus, QPushButton:focus, QLineEdit:focus, QListView:focus, QListWidget:focus,
        QSlider:focus, QRadioButton:focus, QCheckBox:focus { border: 1px solid %5; }
        QLineEdit, QListView, QListWidget {
            color: %2; background: %4; border: 1px solid %6; border-radius: 6px; padding: 7px;
            selection-background-color: %9; selection-color: %2;
        }
        QLineEdit:disabled, QListView:disabled { color: %3; }
        QSlider::groove:horizontal { height: 4px; background: %6; border-radius: 2px; }
        QSlider::sub-page:horizontal { background: %5; border-radius: 2px; }
        QSlider::handle:horizontal { width: 12px; height: 12px; margin: -5px 0; border-radius: 6px; background: %5; }
        QScrollBar:vertical { width: 10px; background: transparent; margin: 2px; }
        QScrollBar::handle:vertical { min-height: 24px; background: %6; border-radius: 4px; }
        QScrollBar::handle:vertical:hover { background: %3; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QSplitter::handle { background: %6; }
        QRadioButton, QCheckBox { spacing: 8px; }
    )").arg(background.name(), text.name(), muted.name(), surface.name(), accent.name(), border.name(),
           surfaceAlt.name(), accentText.name(), selection.name(), videoBackground.name(), control.name(),
           controlHover.name(), surfaceAlt.name()));
}

void Window::updateResponsiveLayout() {
    if (!contentSplitter_ || !playlistPanel_) return;
    const bool vertical = width() < 940;
    contentSplitter_->setOrientation(vertical ? Qt::Vertical : Qt::Horizontal);
    if (vertical) {
        stagePanel_->setMinimumSize(360, 220);
        playlistPanel_->setMinimumHeight(120);
        playlistPanel_->setMaximumHeight(260);
        if (playlistVisible_) contentSplitter_->setSizes({qMax(220, contentSplitter_->height() - 165), 150});
    } else {
        stagePanel_->setMinimumSize(420, 240);
        playlistPanel_->setMinimumHeight(0);
        playlistPanel_->setMaximumHeight(QWIDGETSIZE_MAX);
        if (playlistVisible_) contentSplitter_->setSizes({qMax(420, contentSplitter_->width() - 300), 280});
    }
    playlistPanel_->setVisible(playlistVisible_);
}

void Window::updatePlayerState() {
    if (!player_) return;
    const bool idle = player_->property(QStringLiteral("idle-active")).toBool();
    const QString title = player_->property(QStringLiteral("media-title")).toString().trimmed();
    if (emptyOverlay_) emptyOverlay_->setVisible(idle);
    const QString shownTitle = title.isEmpty() ? (idle ? QStringLiteral("Open media to begin") : QStringLiteral("Playing media")) : title;
    mediaTitle_->setText(mediaTitle_->fontMetrics().elidedText(shownTitle, Qt::ElideMiddle, qMax(90, mediaTitle_->width())));
    setWindowTitle(player_->idle ? QStringLiteral("MpVideo") : QStringLiteral("%1 - MpVideo").arg(shownTitle));
    if (playButton_) {
        playButton_->setGlyph(player_->property(QStringLiteral("pause")).toBool() || idle ? Glyph::Play : Glyph::Pause);
        playButton_->setToolTip(player_->property(QStringLiteral("pause")).toBool() || idle ? QStringLiteral("Play") : QStringLiteral("Pause"));
        playButton_->setAccessibleName(playButton_->toolTip());
    }
    if (volumeButton_) {
        const bool muted = player_->property(QStringLiteral("volume")).toDouble() < 0.5;
        volumeButton_->setGlyph(muted ? Glyph::Muted : Glyph::Volume);
        volumeButton_->setToolTip(muted ? QStringLiteral("Restore volume") : QStringLiteral("Mute"));
    }
    if (fullscreenButton_) {
        fullscreenButton_->setGlyph(fullscreen_ ? Glyph::Windowed : Glyph::Fullscreen);
        fullscreenButton_->setToolTip(fullscreen_ ? QStringLiteral("Exit fullscreen") : QStringLiteral("Enter fullscreen"));
    }
    updateSeekFromPlayer();
    updatePlaylist();
}

void Window::updatePlaylist() {
    if (!playlistList_ || !player_) return;
    const QVariantList playlist = player_->playlist();
    if (lastPlaylist_ != playlist) {
        lastPlaylist_ = playlist;
        playlistList_->clear();
        for (const QVariant &entry : playlist) {
            const QString path = pathFromPlaylistItem(entry);
            const QString label = labelFromPlaylistItem(entry);
            auto *item = new QListWidgetItem(label, playlistList_);
            item->setData(Qt::UserRole, path);
            item->setToolTip(path);
        }
    }
    const int playlistPosition = player_->property(QStringLiteral("playlist-pos")).toInt();
    if (lastPlaylistPosition_ != playlistPosition) {
        lastPlaylistPosition_ = playlistPosition;
        if (playlistPosition >= 0 && playlistPosition < playlistList_->count())
            playlistList_->setCurrentRow(playlistPosition);
    }
    auto *pages = qobject_cast<QStackedWidget *>(playlistList_->parentWidget());
    if (pages) pages->setCurrentWidget(playlist.isEmpty() ? emptyPlaylist_ : playlistList_);
    playlistTitle_->setText(playlist.isEmpty()
        ? QStringLiteral("Playlist")
        : QStringLiteral("Playlist  ·  %1").arg(playlist.size()));
}

void Window::updateSeekFromPlayer() {
    if (!player_ || !seekSlider_) return;
    const double duration = player_->property(QStringLiteral("duration")).toDouble();
    const double position = player_->property(QStringLiteral("time-pos")).toDouble();
    const double volume = player_->property(QStringLiteral("volume")).toDouble();
    const bool canSeek = !player_->property(QStringLiteral("idle-active")).toBool() && std::isfinite(duration) && duration > 0;
    seekSlider_->setEnabled(canSeek);
    if (canSeek && !seekSlider_->isSliderDown()) {
        const int value = qBound(0, qRound(position / duration * seekSlider_->maximum()), seekSlider_->maximum());
        const QSignalBlocker blocker(seekSlider_);
        seekSlider_->setValue(value);
    }
    positionLabel_->setText(formatTime(position));
    durationLabel_->setText(formatTime(duration));
    if (!volumeSlider_->isSliderDown()) {
        const QSignalBlocker blocker(volumeSlider_);
        volumeSlider_->setValue(qBound(0, qRound(volume), 100));
    }
}

void Window::seekToSliderValue(int value) {
    const double duration = player_ ? player_->property(QStringLiteral("duration")).toDouble() : 0;
    if (!player_ || player_->property(QStringLiteral("idle-active")).toBool() || !std::isfinite(duration) || duration <= 0) return;
    const double position = duration * qBound(0, value, seekSlider_->maximum()) / seekSlider_->maximum();
    player_->command({QStringLiteral("seek"), QString::number(position, 'f', 3), QStringLiteral("absolute+exact")});
}

void Window::seekRelative(double seconds) {
    if (!player_ || player_->property(QStringLiteral("idle-active")).toBool()) return;
    player_->command({QStringLiteral("seek"), QString::number(seconds, 'f', 1), QStringLiteral("relative+exact")});
}

void Window::openFiles(const QStringList &files) {
    QStringList validFiles;
    for (const QString &file : files) {
        const QString clean = file.trimmed();
        if (!clean.isEmpty()) validFiles.append(clean);
    }
    if (validFiles.isEmpty()) return;
    bool append = false;
    for (const QString &file : validFiles) {
        const QUrl url = QUrl::fromUserInput(file, QDir::currentPath(), QUrl::AssumeLocalFile);
        const QString source = url.isLocalFile() ? url.toLocalFile() : url.toString(QUrl::FullyEncoded);
        player_->command({QStringLiteral("loadfile"), source, append ? QStringLiteral("append-play") : QStringLiteral("replace")});
        append = true;
    }
}

void Window::openDroppedUrls(const QList<QUrl> &urls) {
    QStringList sources;
    for (const QUrl &url : urls) {
        if (url.isLocalFile()) sources.append(url.toLocalFile());
        else if (url.isValid()) sources.append(url.toString(QUrl::FullyEncoded));
    }
    openFiles(sources);
}

void Window::setFullscreen(bool enabled) {
    if (fullscreen_ == enabled) return;
    fullscreen_ = enabled;
    if (header_) header_->setVisible(!enabled);
    if (enabled) showFullScreen();
    else showNormal();
    updateResponsiveLayout();
    updatePlayerState();
}

void Window::togglePlaylist() {
    if (playlistToggle_) playlistToggle_->setChecked(!playlistVisible_);
}

void Window::showFilePicker() {
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    auto *pathRow = new QHBoxLayout;
    auto *pathEdit = new QLineEdit(QDir::homePath(), content);
    pathEdit->setAccessibleName(QStringLiteral("Folder or media path"));
    pathEdit->setClearButtonEnabled(true);
    auto *upButton = textButton(QStringLiteral("Up"), QStringLiteral("Go to parent folder"), content);
    pathRow->addWidget(pathEdit, 1);
    pathRow->addWidget(upButton);
    layout->addLayout(pathRow);

    auto *showAllFiles = new QCheckBox(QStringLiteral("Show all files"), content);
    layout->addWidget(showAllFiles);

    auto *model = new QFileSystemModel(content);
    model->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Readable);
    model->setNameFilterDisables(false);
    const QStringList mediaFilters = {
        QStringLiteral("*.mp4"), QStringLiteral("*.mkv"), QStringLiteral("*.webm"), QStringLiteral("*.mov"),
        QStringLiteral("*.avi"), QStringLiteral("*.m4v"), QStringLiteral("*.mpg"), QStringLiteral("*.mpeg"),
        QStringLiteral("*.ts"), QStringLiteral("*.m2ts"), QStringLiteral("*.mp3"), QStringLiteral("*.flac"),
        QStringLiteral("*.m4a"), QStringLiteral("*.ogg"), QStringLiteral("*.opus"), QStringLiteral("*.wav"),
        QStringLiteral("*.aac"), QStringLiteral("*.wmv"), QStringLiteral("*.3gp"), QStringLiteral("*.vob"),
        QStringLiteral("*.ogv"), QStringLiteral("*.mka"), QStringLiteral("*.ass"), QStringLiteral("*.srt"),
        QStringLiteral("*.vtt")
    };
    model->setNameFilters(mediaFilters);

    auto *view = new QListView(content);
    view->setModel(model);
    view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    view->setUniformItemSizes(true);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(view, 1);

    auto *message = new QLabel(QStringLiteral("Choose media files to open or add to the playlist."), content);
    message->setWordWrap(true);
    message->setObjectName(QStringLiteral("mutedMessage"));
    layout->addWidget(message);

    auto *buttonRow = new QHBoxLayout;
    buttonRow->addStretch(1);
    auto *cancelButton = textButton(QStringLiteral("Cancel"), QStringLiteral("Cancel opening media"), content);
    auto *openButton = textButton(QStringLiteral("Open"), QStringLiteral("Open selected media files"), content);
    openButton->setObjectName(QStringLiteral("primaryButton"));
    buttonRow->addWidget(cancelButton);
    buttonRow->addWidget(openButton);
    layout->addLayout(buttonRow);

    const QString initialDirectory = QDir::homePath();
    model->setRootPath(initialDirectory);
    view->setRootIndex(model->index(initialDirectory));
    const auto navigate = [model, view, pathEdit, message](const QString &requestedPath) {
        const QFileInfo info(requestedPath);
        if (!info.exists() || !info.isDir()) {
            message->setText(QStringLiteral("That folder is not available. Check the path and try again."));
            return false;
        }
        const QString directory = info.absoluteFilePath();
        model->setRootPath(directory);
        view->setRootIndex(model->index(directory));
        pathEdit->setText(directory);
        message->setText(QStringLiteral("Choose media files to open or add to the playlist."));
        return true;
    };
    connect(pathEdit, &QLineEdit::returnPressed, this, [pathEdit, navigate] { navigate(pathEdit->text().trimmed()); });
    connect(upButton, &QPushButton::clicked, this, [pathEdit, navigate] {
        QDir directory(pathEdit->text().trimmed());
        if (directory.cdUp()) navigate(directory.absolutePath());
    });
    connect(showAllFiles, &QCheckBox::toggled, model, [model, mediaFilters](bool showAll) {
        model->setNameFilters(showAll ? QStringList{} : mediaFilters);
    });
    connect(view, &QListView::clicked, this, [model, pathEdit](const QModelIndex &index) {
        pathEdit->setText(model->filePath(index));
    });

    auto *dialog = showModal(QStringLiteral("Open media"), content, pathEdit);
    connect(cancelButton, &QPushButton::clicked, dialog, &mpvideo::Modal::dismiss);
    const auto openSelected = [this, model, view, pathEdit, message, dialog] {
        QStringList paths;
        for (const QModelIndex &index : view->selectionModel()->selectedIndexes()) {
            if (!model->isDir(index)) paths.append(model->filePath(index));
        }
        const QString typedPath = pathEdit->text().trimmed();
        const QFileInfo typedInfo(typedPath);
        if (paths.isEmpty() && typedInfo.isFile()) paths.append(typedInfo.absoluteFilePath());
        paths.removeDuplicates();
        if (paths.isEmpty()) {
            message->setText(QStringLiteral("Select one or more files to open."));
            return;
        }
        dialog->dismiss();
        openFiles(paths);
    };
    connect(openButton, &QPushButton::clicked, this, openSelected);
    connect(view, &QListView::doubleClicked, this, [model, view, navigate, openSelected](const QModelIndex &index) {
        const QString path = model->filePath(index);
        if (model->isDir(index)) navigate(path);
        else {
            view->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            openSelected();
        }
    });
}

void Window::showUrlDialog() {
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    auto *description = new QLabel(QStringLiteral("Enter a stream address or a web URL."), content);
    description->setWordWrap(true);
    layout->addWidget(description);
    auto *urlEdit = new QLineEdit(content);
    urlEdit->setAccessibleName(QStringLiteral("Media URL"));
    urlEdit->setPlaceholderText(QStringLiteral("https://example.com/video.m3u8"));
    urlEdit->setClearButtonEnabled(true);
    layout->addWidget(urlEdit);
    auto *message = new QLabel(QString(), content);
    message->setWordWrap(true);
    layout->addWidget(message);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto *cancel = textButton(QStringLiteral("Cancel"), QStringLiteral("Cancel opening URL"), content);
    auto *open = textButton(QStringLiteral("Open"), QStringLiteral("Open this URL"), content);
    open->setObjectName(QStringLiteral("primaryButton"));
    buttons->addWidget(cancel);
    buttons->addWidget(open);
    layout->addLayout(buttons);

    auto *dialog = showModal(QStringLiteral("Open URL"), content, urlEdit);
    const auto openUrl = [this, urlEdit, message, dialog] {
        const QString input = urlEdit->text().trimmed();
        if (input.isEmpty()) {
            message->setText(QStringLiteral("Enter a URL to continue."));
            urlEdit->setFocus();
            return;
        }
        const QUrl url = QUrl::fromUserInput(input);
        if (!url.isValid() || url.isEmpty()) {
            message->setText(QStringLiteral("That address is not valid. Check it and try again."));
            urlEdit->setFocus();
            return;
        }
        const QString source = url.isLocalFile() ? url.toLocalFile() : url.toString(QUrl::FullyEncoded);
        dialog->dismiss();
        player_->open(source, false);
    };
    connect(cancel, &QPushButton::clicked, dialog, &mpvideo::Modal::dismiss);
    connect(open, &QPushButton::clicked, this, openUrl);
    connect(urlEdit, &QLineEdit::returnPressed, this, openUrl);
}

void Window::showTracksDialog() {
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    struct TrackSection {
        QString label;
        QString property;
        QList<QVariantMap> tracks;
        bool allowOff = false;
    };
    TrackSection video{QStringLiteral("Video"), QStringLiteral("vid"), {}, false};
    TrackSection audio{QStringLiteral("Audio"), QStringLiteral("aid"), {}, false};
    TrackSection subtitles{QStringLiteral("Subtitles"), QStringLiteral("sid"), {}, true};
    for (const QVariant &value : player_->tracks()) {
        const QVariantMap track = value.toMap();
        const QString type = track.value(QStringLiteral("type")).toString().toLower();
        if (type == QStringLiteral("video")) video.tracks.append(track);
        else if (type == QStringLiteral("audio")) audio.tracks.append(track);
        else if (type == QStringLiteral("sub") || type == QStringLiteral("subtitle") || type == QStringLiteral("captions"))
            subtitles.tracks.append(track);
    }

    QWidget *firstFocus = nullptr;
    int shown = 0;
    const auto addSection = [this, content, layout, &firstFocus, &shown](const TrackSection &section) {
        if (section.tracks.isEmpty() && !section.allowOff) return;
        auto *heading = new QLabel(section.label, content);
        QFont font = heading->font();
        font.setWeight(QFont::DemiBold);
        heading->setFont(font);
        layout->addWidget(heading);
        auto *group = new QButtonGroup(content);
        group->setExclusive(true);
        if (section.allowOff) {
            auto *off = new QRadioButton(QStringLiteral("Off"), content);
            off->setChecked(std::none_of(section.tracks.cbegin(), section.tracks.cend(), [](const QVariantMap &track) {
                return track.value(QStringLiteral("selected")).toBool();
            }));
            group->addButton(off);
            layout->addWidget(off);
            if (!firstFocus) firstFocus = off;
            connect(off, &QRadioButton::toggled, this, [this, property = section.property](bool checked) {
                if (checked) player_->setProperty(property, QStringLiteral("no"));
            });
            ++shown;
        }
        for (const QVariantMap &track : section.tracks) {
            bool idValid = false;
            const int id = track.value(QStringLiteral("id")).toInt(&idValid);
            if (!idValid) continue;
            QString label = track.value(QStringLiteral("title")).toString();
            const QString language = track.value(QStringLiteral("lang")).toString();
            const QString codec = track.value(QStringLiteral("codec")).toString();
            if (label.isEmpty()) label = language.isEmpty() ? codec : language;
            else if (!language.isEmpty() && language != label) label += QStringLiteral(" - ") + language;
            if (label.isEmpty()) label = QStringLiteral("Track %1").arg(id);
            if (!codec.isEmpty() && !label.contains(codec, Qt::CaseInsensitive)) label += QStringLiteral(" (%1)").arg(codec);
            auto *choice = new QRadioButton(label, content);
            choice->setChecked(track.value(QStringLiteral("selected")).toBool());
            group->addButton(choice);
            layout->addWidget(choice);
            if (!firstFocus) firstFocus = choice;
            connect(choice, &QRadioButton::toggled, this, [this, property = section.property, id](bool checked) {
                if (checked) player_->setProperty(property, id);
            });
            ++shown;
        }
    };
    addSection(video);
    addSection(audio);
    addSection(subtitles);

    if (shown == 0) {
        auto *empty = new QLabel(QStringLiteral("No selectable audio, video, or subtitle tracks are available."), content);
        empty->setWordWrap(true);
        layout->addWidget(empty);
    }
    layout->addStretch(1);
    showModal(QStringLiteral("Tracks"), content, firstFocus);
}

void Window::showSettingsDialog() {
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto *description = new QLabel(QStringLiteral("Adjust the playback rate for the current media."), content);
    description->setWordWrap(true);
    layout->addWidget(description);

    auto *row = new QHBoxLayout;
    auto *label = new QLabel(QStringLiteral("Playback speed"), content);
    row->addWidget(label);
    row->addStretch(1);
    auto *value = new QLabel(QStringLiteral("100%"), content);
    value->setMinimumWidth(48);
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    row->addWidget(value);
    layout->addLayout(row);

    auto *speed = new QSlider(Qt::Horizontal, content);
    speed->setAccessibleName(QStringLiteral("Playback speed"));
    speed->setRange(25, 300);
    speed->setSingleStep(5);
    speed->setPageStep(25);
    speed->setValue(qBound(25, qRound(player_->property(QStringLiteral("speed")).toDouble() * 100), 300));
    connect(speed, &QSlider::valueChanged, this, [this, value](int percent) {
        value->setText(QStringLiteral("%1%").arg(percent));
        player_->setProperty(QStringLiteral("speed"), percent / 100.0);
    });
    layout->addWidget(speed);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto *reset = textButton(QStringLiteral("Reset to 100%"), QStringLiteral("Reset playback speed"), content);
    connect(reset, &QPushButton::clicked, this, [speed] { speed->setValue(100); });
    buttons->addWidget(reset);
    layout->addLayout(buttons);
    layout->addStretch(1);
    showModal(QStringLiteral("Playback settings"), content, speed);
}

void Window::showError(const QString &message) {
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    auto *details = new QLabel(message.trimmed().isEmpty() ? QStringLiteral("Playback could not continue.") : message, content);
    details->setTextFormat(Qt::PlainText);
    details->setWordWrap(true);
    layout->addWidget(details);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto *close = textButton(QStringLiteral("Close"), QStringLiteral("Close playback error"), content);
    auto *open = textButton(QStringLiteral("Open another file"), QStringLiteral("Choose another media file"), content);
    open->setObjectName(QStringLiteral("primaryButton"));
    buttons->addWidget(close);
    buttons->addWidget(open);
    layout->addLayout(buttons);
    auto *dialog = showModal(QStringLiteral("Playback error"), content, open);
    connect(close, &QPushButton::clicked, dialog, &mpvideo::Modal::dismiss);
    connect(open, &QPushButton::clicked, this, [this, dialog] {
        dialog->dismiss();
        QTimer::singleShot(0, this, &Window::showFilePicker);
    });
}

mpvideo::Modal *Window::showModal(const QString &title, QWidget *content, QWidget *initialFocus) {
    if (modal_) modal_->dismiss();
    auto *dialog = new FocusModal(this, centralWidget(), title);
    dialog->body()->addWidget(content);
    connect(dialog, &mpvideo::Modal::dismissed, this, [this, dialog] {
        if (modal_ == dialog) modal_.clear();
    });
    modal_ = dialog;
    dialog->present(initialFocus);
    return dialog;
}

void Window::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    updateResponsiveLayout();
    if (mediaTitle_ && player_) {
        const QString sourceTitle = player_->property(QStringLiteral("media-title")).toString().trimmed();
        const QString title = sourceTitle.isEmpty()
            ? (player_->property(QStringLiteral("idle-active")).toBool() ? QStringLiteral("Open media to begin") : QStringLiteral("Playing media"))
            : sourceTitle;
        mediaTitle_->setText(mediaTitle_->fontMetrics().elidedText(title, Qt::ElideMiddle, qMax(90, mediaTitle_->width())));
    }
}

void Window::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
        return;
    }
    QMainWindow::dragEnterEvent(event);
}

void Window::dropEvent(QDropEvent *event) {
    openDroppedUrls(event->mimeData()->urls());
    event->acceptProposedAction();
}

bool Window::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::DragEnter) {
        auto *drag = static_cast<QDragEnterEvent *>(event);
        if (drag->mimeData()->hasUrls()) {
            drag->acceptProposedAction();
            return true;
        }
    } else if (event->type() == QEvent::Drop) {
        auto *drop = static_cast<QDropEvent *>(event);
        openDroppedUrls(drop->mimeData()->urls());
        drop->acceptProposedAction();
        return true;
    } else if (event->type() == QEvent::MouseButtonDblClick && watched == video_) {
        setFullscreen(!fullscreen_);
        return true;
    }
    return QMainWindow::eventFilter(watched, event);
}
