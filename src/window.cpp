#include "window.h"
#include "desktop_file_picker.h"
#include "icon_button.h"
#include "modal.h"
#include "pause_indicator.h"
#include "seek_slider.h"
#include "trim_timeline.h"
#include "themed_checkbox.h"
#include "video_widget.h"
#include <QApplication>
#include <QCursor>
#include <QButtonGroup>
#include <QCheckBox>
#include <QDir>
#include <QDropEvent>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QMouseEvent>
#include <QProcess>
#include <optional>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedLayout>
#include <QTimer>
#include <QVariantAnimation>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
namespace mpvideo {
namespace {
double luminance(QColor color) {
    auto channel = [](double value) {
        return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF()) +
           0.0722 * channel(color.blueF());
}
QColor readableMuted(QColor muted, QColor text, QColor background) {
    for (int step = 0; step < 20; ++step) {
        auto a = luminance(muted), b = luminance(background);
        if ((qMax(a, b) + 0.05) / (qMin(a, b) + 0.05) >= 4.5)
            break;
        muted = QColor::fromRgbF(muted.redF() * 0.85 + text.redF() * 0.15,
                                 muted.greenF() * 0.85 + text.greenF() * 0.15,
                                 muted.blueF() * 0.85 + text.blueF() * 0.15);
    }
    return muted;
}
QColor mutedTrack(QColor muted, QColor background) {
    return QColor::fromRgbF(background.redF() * 0.7 + muted.redF() * 0.3,
                            background.greenF() * 0.7 + muted.greenF() * 0.3,
                            background.blueF() * 0.7 + muted.blueF() * 0.3);
}
QStringList embeddedOptions(QStringList options) {
    options << "vo=libmpv";
    return options;
}
QString stamp(double value) {
    auto s = qMax(0, qint64(value));
    return s >= 3600 ? QString("%1:%2:%3")
                           .arg(s / 3600)
                           .arg(s / 60 % 60, 2, 10, QChar('0'))
                           .arg(s % 60, 2, 10, QChar('0'))
                     : QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}
struct Encoder {
    QString name;
    QStringList input; // before -i
    QStringList codec; // output video options
    QString filter;    // appended to the filter graph, applied to the concat output
};
bool probeEncoder(const Encoder &e) {
    QProcess p;
    QStringList args{"-hide_banner", "-loglevel", "error", "-nostdin"};
    args << e.input << "-f" << "lavfi" << "-i" << "color=c=black:s=320x240:d=0.2:r=10";
    if (!e.filter.isEmpty())
        args << "-vf" << e.filter;
    args << e.codec << "-frames:v" << "2" << "-f" << "null" << "-";
    p.start("ffmpeg", args);
    return p.waitForFinished(4000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}
// First hardware H.264 encoder that really works on this machine, else x264 tuned for speed.
Encoder pickEncoder(bool hardware, const QString &extension) {
    static std::optional<Encoder> cached;
    const bool mp4Family = QStringList{"mp4", "mkv", "mov", "m4v"}.contains(extension);
    const Encoder software{"x264", {}, {"-c:v", "libx264", "-preset", "veryfast", "-crf", "20"}, {}};
    if (extension == "webm")
        return {"VP9", {}, {"-c:v", "libvpx-vp9", "-deadline", "realtime", "-cpu-used", "8", "-row-mt", "1",
                            "-crf", "32", "-b:v", "0"}, {}};
    if (!mp4Family) // other containers: let ffmpeg choose the codec
        return {"default", {}, {}, {}};
    if (!hardware)
        return software;
    if (!cached) {
        QList<Encoder> candidates{
            {"NVENC", {}, {"-c:v", "h264_nvenc", "-preset", "p4", "-rc", "vbr", "-cq", "21", "-b:v", "0"}, "format=nv12"}};
        for (const char *dev : {"/dev/dri/renderD128", "/dev/dri/renderD129"})
            candidates.append({"VAAPI", {"-vaapi_device", dev}, {"-c:v", "h264_vaapi", "-qp", "21"},
                               "format=nv12,hwupload"});
        candidates.append({"QSV", {}, {"-c:v", "h264_qsv", "-global_quality", "21"}, "format=nv12"});
        cached = software;
        for (const auto &c : candidates)
            if (probeEncoder(c)) {
                cached = c;
                break;
            }
    }
    return *cached;
}
QLabel *text(QString value, QString name = {}) {
    auto *label = new QLabel(value);
    label->setObjectName(name);
    label->setTextFormat(Qt::PlainText);
    return label;
}
}
Window::Window(QString configPath, const QStringList &mpvOptions)
    : player_(embeddedOptions(mpvOptions)), theme_(configPath, nullptr),
      settings_(QFileInfo(theme_.configPath()).dir().filePath("settings.ini"), QSettings::IniFormat) {
    settings_.setFallbacksEnabled(false);
    setObjectName("mpvideoWindow");
    setWindowTitle("MpVideo");
    setMinimumSize(680, 440);
    resize(1080, 720);
    setAcceptDrops(true);
    build();
    shortcuts();
    applyTheme();
    connect(&theme_, &ThemeManager::changed, this, &Window::applyTheme);
    connect(&theme_, &ThemeManager::rejected, this,
            [this](const QString &e) { status_->setText("Theme unchanged: " + e); });
    // mpv reports time-pos every frame; coalesce so the UI is not rebuilt per frame over the GL surface.
    refreshTimer_ = new QTimer(this);
    refreshTimer_->setSingleShot(true);
    refreshTimer_->setInterval(33);
    connect(refreshTimer_, &QTimer::timeout, this, &Window::refresh);
    connect(&player_, &Player::changed, this, [this] {
        if (!refreshTimer_->isActive())
            refreshTimer_->start();
    });
    auto error = [this](const QString &message) {
        status_->setText(message);
        status_->setToolTip(message);
    };
    connect(&player_, &Player::error, this, error);

    refresh();
}
Window::~Window() {
    if (export_) {
        export_->disconnect();
        export_->kill();
        export_->waitForFinished(1000);
    }
    delete desktopFilePicker_;
    delete takeCentralWidget();
}
void Window::build() {
    auto *root = new QWidget;
    setCentralWidget(root);
    auto *rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    content_ = new QWidget;
    content_->setFocusPolicy(Qt::StrongFocus);
    rootLayout->addWidget(content_);
    layout_ = new QVBoxLayout(content_);
    layout_->setContentsMargins(18, 12, 18, 16);
    layout_->setSpacing(10);
    header_ = new QWidget(content_);
    header_->setObjectName("bar");
    header_->setAttribute(Qt::WA_StyledBackground);
    auto *headerLayout = new QVBoxLayout(header_);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    footer_ = new QWidget(content_);
    footer_->setObjectName("bar");
    footer_->setAttribute(Qt::WA_StyledBackground);
    auto *footerLayout = new QVBoxLayout(footer_);
    footerLayout->setContentsMargins(0, 0, 0, 0);
    footerLayout->setSpacing(10);
    pauseIndicator_ = new PauseIndicator(content_);
    skipBackIndicator_ = new PauseIndicator(content_);
    skipForwardIndicator_ = new PauseIndicator(content_);
    layout_->addWidget(header_);
    buildHeader(headerLayout);
    buildStage(layout_);
    layout_->addWidget(footer_);
    buildEditBar(footerLayout);
    buildTransport(footerLayout);
    hideTimer_ = new QTimer(this);
    hideTimer_->setSingleShot(true);
    hideTimer_->setInterval(2000);
    connect(hideTimer_, &QTimer::timeout, this, [this] { introDone_ = true; updateBars(true); });
    initBars();
    auto *poll = new QTimer(this);
    poll->setInterval(80);
    connect(poll, &QTimer::timeout, this, [this] { updateBars(false); });
    poll->start();
    content_->setFocus();
}
IconButton *Window::makeButton(QString icon, QString label, std::function<void()> action) {
    auto *button = new IconButton(std::move(icon), std::move(label));
    connect(button, &QPushButton::clicked, this, std::move(action));
    return button;
}
void Window::buildHeader(QVBoxLayout *layout) {
    auto *header = new QHBoxLayout;
    auto *brand = text("MpVideo", "brand");
    header->addWidget(brand);
    title_ = text("", "mediaTitle");
    title_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    header->addSpacing(18);
    header->addWidget(title_, 1);
    cut_ = makeButton("cut", "Edit video", [this] { toggleEdit(); });
    cut_->setCheckable(true);
    cut_->setEnabled(false);
    header->addWidget(cut_);
    header->addWidget(makeButton("open", "Open media (Ctrl+O)", [this] { fileDialog(); }));
    header->addWidget(
        makeButton("queue", "Toggle queue (P)", [this] { queue_->setVisible(!queue_->isVisible()); }));
    header->addWidget(makeButton("settings", "Settings", [this] { settingsDialog(); }));
    layout->addLayout(header);
}
void Window::buildStage(QVBoxLayout *layout) {
    auto *middle = new QHBoxLayout;
    middle->setSpacing(14);
    auto *stage = new QWidget;
    stage->setObjectName("stage");
    auto *stack = new QStackedLayout(stage);
    stageStack_ = stack;
    stack->setContentsMargins(0, 0, 0, 0);

    empty_ = text("Your next frame starts here.\n\nDrop a video, open a file, or "
                  "paste a URL with Ctrl+O.",
                  "empty");
    empty_->setAlignment(Qt::AlignCenter);
    empty_->setWordWrap(true);
    empty_->setCursor(Qt::PointingHandCursor);
    empty_->installEventFilter(this);
    stack->addWidget(empty_);
    if (player_.isReady()) {
        // Adding the first OpenGL widget after show() recreates the native window.
        // Keep it hidden here so libmpv rendering still starts only on selection.
        video_ = new VideoWidget(&player_);
        stack->addWidget(video_);
        connect(video_, &VideoWidget::clicked, this, [this] {
            togglePause();
        });
        connect(video_, &VideoWidget::error, this,
                [this](const QString &message) { status_->setText(message); });
    }
    stack->setCurrentWidget(empty_);
    middle->addWidget(stage, 1);
    queue_ = new QListWidget;
    queue_->setAccessibleName("Playback queue");
    queue_->setMinimumWidth(160);
    queue_->setMaximumWidth(260);
    queue_->hide();
    connect(queue_, &QListWidget::itemActivated, this,
            [this](QListWidgetItem *item) { player_.setProperty("playlist-pos", queue_->row(item)); });
    middle->addWidget(queue_);
    layout->addLayout(middle, 1);
}
void Window::buildTransport(QVBoxLayout *layout) {
    seek_ = new SeekSlider;
    seek_->setRange(0, 10000);
    seek_->setAccessibleName("Seek position");
    connect(seek_, &QSlider::sliderReleased, this, [this] {
        player_.command({"seek", QString::number(seek_->value() / 100.0), "absolute-percent+exact"});
    });
    layout->addWidget(seek_);
    auto *controls = new QHBoxLayout;
    controls->setSpacing(8);
    controls->addWidget(
        makeButton("previous", "Previous media", [this] { player_.command({"playlist-prev", "weak"}); }));
    skipBack_ = makeButton("skipBack", {}, [this] { skip(-1); });
    controls->addWidget(skipBack_);
    play_ = makeButton("play", "Play or pause (Space)", [this] { togglePause(); });
    controls->addWidget(play_);
    skipForward_ = makeButton("skipForward", {}, [this] { skip(1); });
    controls->addWidget(skipForward_);
    updateSkipButtons();
    controls->addWidget(
        makeButton("next", "Next media", [this] { player_.command({"playlist-next", "weak"}); }));
    time_ = text("0:00 / 0:00", "time");
    controls->addSpacing(12);
    controls->addWidget(time_);
    controls->addStretch();
    mute_ = makeButton("volume", "Mute (M)", [this] { player_.command({"cycle", "mute"}); });
    controls->addWidget(mute_);
    volume_ = new QSlider(Qt::Horizontal);
    volume_->setRange(0, 100);
    volume_->setFixedWidth(92);
    volume_->setAccessibleName("Volume");
    connect(volume_, &QSlider::valueChanged, this, [this](int v) { player_.setProperty("volume", v); });
    controls->addWidget(volume_);
    controls->addWidget(makeButton("tracks", "Audio and subtitles", [this] { trackDialog(); }));
    controls->addWidget(makeButton("full", "Toggle fullscreen (F)", [this] { fullscreen(); }));
    layout->addLayout(controls);
    status_ = text("Ready · mpv playback", "status");
    status_->setWordWrap(true);
    status_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(status_);
}
void Window::applyTheme() {
    auto c = [this](QString key) {
        auto color = theme_.theme().color(key);
        if (key == "muted")
            color = readableMuted(color, theme_.theme().color("text"), theme_.theme().color("background"));
        return color.name(QColor::HexArgb);
    };
    auto palette = qApp->palette();
    palette.setColor(QPalette::Window, theme_.theme().color("background"));
    palette.setColor(QPalette::WindowText, theme_.theme().color("text"));
    palette.setColor(QPalette::Text, theme_.theme().color("text"));
    palette.setColor(QPalette::Button, theme_.theme().color("surface_alt"));
    palette.setColor(QPalette::Mid, theme_.theme().color("border"));
    palette.setColor(QPalette::ButtonText, theme_.theme().color("text"));
    palette.setColor(QPalette::Base, theme_.theme().color("surface"));
    palette.setColor(QPalette::Highlight, theme_.theme().color("accent"));
    palette.setColor(QPalette::HighlightedText, theme_.theme().color("accent_text"));
    qApp->setPalette(palette);
    qApp->setStyleSheet(QString(R"(
QWidget { color:%1; font-family:"Inter","Noto Sans",sans-serif; font-size:13px; }
QMainWindow, QWidget#modalCard { background:%2; }
QWidget#modalCard { border:1px solid %3; border-radius:14px; }
QWidget#stage, QLabel#empty { background:%4; border-radius:8px; }
QLabel#brand { font-size:18px; font-weight:650; } QLabel#heading { font-size:20px; font-weight:600; }
QLabel#empty { color:%5; font-size:16px; } QLabel#status, QLabel#mediaTitle { color:%5; }
QPushButton { background:transparent; border:1px solid transparent; border-radius:6px; padding:6px 12px; }
QPushButton:hover { background:%6; } QPushButton:pressed, QPushButton:checked { background:%3; }
QPushButton:focus, QLineEdit:focus, QListWidget:focus { border:1px solid %7; }
QPushButton[keyboardFocus="false"]:focus { border-color:transparent; }
QWidget#settingsModal QPushButton { background:%4; border:1px solid %3; }
QWidget#settingsModal QPushButton:hover { background:%6; border-color:%7; }
QWidget#settingsModal QPushButton:pressed, QWidget#settingsModal QPushButton:checked { background:%7; color:%9; border-color:%7; }
QWidget#settingsModal QPushButton:focus { border-color:%7; }
QWidget#settingsModal QPushButton[keyboardFocus="false"]:focus { border-color:%3; }
QPushButton:disabled { color:%5; } QLineEdit, QListWidget { background:%4; border:1px solid %3; border-radius:6px; padding:8px; }
QListWidget::item { padding:9px 4px; } QListWidget::item:selected { background:%6; }
QSlider::groove:horizontal { height:4px; background:%8; border-radius:2px; }
QSlider::add-page:horizontal { background:%8; border-radius:2px; }
QSlider::sub-page:horizontal { background:%7; border-radius:2px; }
QSlider::handle:horizontal { background:%7; width:12px; margin:-4px 0; border-radius:6px; }
QPushButton#primary { background:%7; color:%9; border-color:%7; font-weight:600; padding:6px 16px; }
QPushButton#primary:hover { background:%7; border-color:%9; } QPushButton#primary:disabled { background:%8; color:%5; border-color:%8; }
QPushButton#primary:focus { border-color:%9; }
QPushButton:checked { background:%3; }
QMenu { background:%4; border:1px solid %3; border-radius:8px; padding:4px; }
QMenu::item { padding:7px 22px 7px 14px; border-radius:5px; } QMenu::item:selected { background:%6; }
QMenu::item:disabled { color:%5; }
QScrollArea { background:transparent; border:0; } QScrollBar:vertical { background:%4; width:8px; } QScrollBar::handle:vertical { background:%3; min-height:24px; } QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical { height:0; }
)")
                            .arg(c("text"), c("background"), c("border"), c("surface"), c("muted"),
                                 c("surface_alt"), c("accent"),
                                 mutedTrack(theme_.theme().color("muted"), theme_.theme().color("background"))
                                     .name(QColor::HexArgb),
                                 c("accent_text")));
}
void Window::refresh() {
    const auto duration = player_.property("duration").toDouble(),
               pos = player_.property("time-pos").toDouble();
    bool idle = !player_.property("idle-active").isValid() || player_.property("idle-active").toBool();
    stageStack_->setCurrentWidget(idle || !video_ ? static_cast<QWidget *>(empty_) : video_);
    const bool paused = player_.property("pause").toBool();
    if (pendingPause_ && (*pendingPause_ == paused || idle))
        pendingPause_.reset();
    play_->setSymbol(pendingPause_.value_or(paused) || idle ? "play" : "pause");
    play_->setEnabled(!idle);
    seek_->setEnabled(duration > 0);
    {
        const auto path = currentPath();
        const bool canEdit = !idle && duration > 0 && !path.isEmpty();
        cut_->setEnabled(canEdit && !export_);
        if (editing_ && (!canEdit || path != editPath_) && !export_)
            exitEdit();
    }
    if (!seek_->isSliderDown()) {
        QSignalBlocker block(seek_);
        seek_->setValue(duration > 0 ? qBound(0, int(pos / duration * 10000), 10000) : 0);
    }
    if (editing_ && !export_) {
        trim_->setPlayhead(pos);
        // Playing through the editor skips what has been removed.
        if (!paused && !pendingPause_ && !idle) {
            const auto &segs = trim_->segments();
            if (trim_->indexAt(pos) >= 0)
                lastGapSkip_ = -1;
            else {
                auto next = std::find_if(segs.begin(), segs.end(), [pos](const auto &sg) { return sg.start > pos; });
                if (next == segs.end()) {
                    pendingPause_ = true;
                    player_.setProperty("pause", true);
                    player_.command({"seek", QString::number(segs.last().end, 'f', 3), "absolute+exact"});
                } else if (lastGapSkip_ != next->start) {
                    lastGapSkip_ = next->start;
                    player_.command({"seek", QString::number(next->start, 'f', 3), "absolute+exact"});
                }
            }
        }
    }
    if (const auto stampText = stamp(pos) + " / " + stamp(duration); time_->text() != stampText)
        time_->setText(stampText);
    if (!volume_->isSliderDown()) {
        QSignalBlocker block(volume_);
        volume_->setValue(player_.property("volume").toInt());
    }
    mute_->setSymbol(player_.property("mute").toBool() ? "mute" : "volume");
    auto title = player_.property("media-title").toString();
    if (title_->text() != title) {
        title_->setText(title);
        title_->setToolTip(title);
        setWindowTitle(title.isEmpty() ? "MpVideo" : title + " · MpVideo");
    }
    if (status_->text() == "Opening media…" && !title.isEmpty())
        status_->setText("Playback ready · mpv");
    const auto list = player_.playlist();
    if (queue_->count() != list.size() || queue_->property("entries") != QVariant(list)) {
        QSignalBlocker block(queue_);
        queue_->clear();
        for (const auto &entry : list) {
            auto map = entry.toMap();
            auto path = map.value("filename").toString();
            auto name = map.value("title").toString();
            auto *item = new QListWidgetItem(name.isEmpty() ? QFileInfo(path).fileName() : name, queue_);
            item->setToolTip(path);
        }
        queue_->setProperty("entries", list);
    }
    if (const int row = player_.property("playlist-pos").toInt(); queue_->currentRow() != row)
        queue_->setCurrentRow(row);
}
QString Window::currentPath() const {
    const auto path = player_.property("path").toString();
    return !path.isEmpty() && QFileInfo(path).isFile() ? QFileInfo(path).absoluteFilePath() : QString();
}
void Window::buildEditBar(QVBoxLayout *layout) {
    editBar_ = new QWidget(footer_);
    editBar_->setObjectName("editBar");
    editBar_->hide();
    auto *row = new QHBoxLayout(editBar_);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(10);
    auto *hint = text("Drag the ends to trim · right-click to split or delete", "status");
    hint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    keepLabel_ = text("", "keep");
    stopCut_ = new QPushButton("Stop");
    stopCut_->setCursor(Qt::PointingHandCursor);
    stopCut_->hide();
    saveCut_ = new QPushButton("Save…");
    saveCut_->setObjectName("primary");
    saveCut_->setCursor(Qt::PointingHandCursor);
    row->addWidget(hint, 1);
    row->addWidget(keepLabel_);
    row->addWidget(stopCut_);
    row->addWidget(saveCut_);
    layout->addWidget(editBar_);
    trim_ = new TrimTimeline(footer_);
    trim_->hide();
    layout->addWidget(trim_);
    trim_->onSeek = [this](double seconds, bool final) {
        player_.command({"seek", QString::number(seconds, 'f', 3), final ? "absolute+exact" : "absolute+keyframes"});
    };
    trim_->onChanged = [this] { updateEdit(); };
    connect(stopCut_, &QPushButton::clicked, this, [this] {
        if (export_)
            export_->kill();
    });
    connect(saveCut_, &QPushButton::clicked, this, [this] { saveDialog(); });
    // Created disabled: an always-on Delete here would clash with the queue's own Delete shortcut.
    auto bindEdit = [this](QKeySequence key, auto fn) {
        auto *s = new QShortcut(key, this);
        s->setEnabled(false);
        editKeys_.append(s);
        connect(s, &QShortcut::activated, this, [this, fn] {
            if (editing_ && !modal_ && !export_)
                fn();
        });
    };
    bindEdit(QKeySequence("Ctrl+Z"), [this] { trim_->undo(); });
    bindEdit(QKeySequence("S"), [this] { trim_->splitAt(player_.property("time-pos").toDouble()); });
    bindEdit(QKeySequence::Delete, [this] { trim_->removeAt(player_.property("time-pos").toDouble()); });
}
void Window::toggleEdit() {
    if (editing_)
        requestExitEdit();
    else {
        editPath_ = currentPath();
        const double duration = player_.property("duration").toDouble();
        if (editPath_.isEmpty() || duration <= 0) {
            cut_->setChecked(false);
            return;
        }
        player_.setProperty("pause", true);
        editing_ = true;
        for (auto *k : std::as_const(editKeys_))
            k->setEnabled(true);
        cut_->setChecked(true);
        cut_->setToolTip("Exit editor");
        trim_->reset(duration);
        trim_->setPlayhead(player_.property("time-pos").toDouble());
        seek_->hide();
        editBar_->show();
        trim_->show();
        updateEdit();
        replaceBars();
    }
}
// Leaving with unsaved edits asks first, inside the window.
void Window::requestExitEdit() {
    if (!editing_)
        return;
    if (export_) {
        cut_->setChecked(true);
        return;
    }
    if (!trim_->isModified()) {
        exitEdit();
        return;
    }
    cut_->setChecked(true);
    auto *modal = dialog("Discard your edits?");
    if (!modal)
        return;
    auto *info = text("Your cuts haven't been saved. Leaving the editor will discard them.");
    info->setWordWrap(true);
    modal->body()->addWidget(info);
    modal->body()->addStretch();
    auto *keep = new QPushButton("Keep editing");
    auto *discard = new QPushButton("Discard edits");
    discard->setObjectName("primary");
    keep->setCursor(Qt::PointingHandCursor);
    discard->setCursor(Qt::PointingHandCursor);
    modal->footer()->addStretch();
    modal->footer()->addWidget(keep);
    modal->footer()->addWidget(discard);
    connect(keep, &QPushButton::clicked, modal, &Modal::dismiss);
    connect(discard, &QPushButton::clicked, modal, [this, modal] {
        modal->dismiss();
        exitEdit();
    });
    modal->present();
    keep->setFocus();
}
// Fullscreen bars are positioned by hand, so re-measure after the footer gains or loses rows.
void Window::replaceBars() {
    if (!immersive_)
        return;
    footer_->layout()->activate();
    placeBars();
}
void Window::exitEdit() {
    editing_ = false;
    for (auto *k : std::as_const(editKeys_))
        k->setEnabled(false);
    editBar_->hide();
    trim_->hide();
    seek_->show();
    cut_->setChecked(false);
    cut_->setToolTip("Edit video");
    cuts_.clear();
    editPath_.clear();
    replaceBars();
}
void Window::updateEdit() {
    cuts_ = trim_->cuts();
    const double duration = trim_->duration();
    keepLabel_->setText(QString("Keeps %1 of %2").arg(stamp(trim_->keptSeconds()), stamp(duration)));
    keepLabel_->setProperty("changed", !cuts_.isEmpty());
    saveCut_->setEnabled(!cuts_.isEmpty() && !export_);
    stopCut_->setVisible(bool(export_));
    trim_->setEnabled(!export_);
}
void Window::saveDialog() {
    if (cuts_.isEmpty() || export_)
        return;
    auto *modal = dialog("Save edited video");
    if (!modal)
        return;
    auto *info = text(QString("%1 cut%2 will be removed; the rest is kept. The video is re-encoded so cuts land exactly.")
                          .arg(cuts_.size())
                          .arg(cuts_.size() == 1 ? "" : "s"));
    info->setWordWrap(true);
    modal->body()->addWidget(info);
    auto *asNew = new QPushButton("Save as new");
    auto *changes = new QPushButton("Save changes");
    modal->body()->addWidget(asNew);
    modal->body()->addWidget(text("Keeps the original and writes a separate “(cut)” file.", "status"));
    modal->body()->addWidget(changes);
    modal->body()->addWidget(text("Overwrites the original file with the edited video (only what you kept).", "status"));
    modal->body()->addStretch();
    connect(asNew, &QPushButton::clicked, modal, [this, modal] {
        modal->dismiss();
        exportCuts(false);
    });
    connect(changes, &QPushButton::clicked, modal, [this, modal] {
        modal->dismiss();
        exportCuts(true);
    });
    modal->present();
}
void Window::exportCuts(bool overwrite) {
    const double duration = player_.property("duration").toDouble();
    if (editPath_.isEmpty() || duration <= 0 || export_)
        return;
    // Merge the cuts, then keep their complement.
    QVector<QPair<double, double>> merged;
    for (auto c : std::as_const(cuts_)) {
        c.first = qMax(0.0, c.first);
        c.second = qMin(duration, c.second);
        if (!merged.isEmpty() && c.first <= merged.last().second)
            merged.last().second = qMax(merged.last().second, c.second);
        else
            merged.append(c);
    }
    QVector<QPair<double, double>> keep;
    double at = 0;
    for (const auto &c : merged) {
        if (c.first - at > 0.05)
            keep.append({at, c.first});
        at = c.second;
    }
    if (duration - at > 0.05)
        keep.append({at, duration});
    if (keep.isEmpty()) {
        status_->setText("Nothing would be left after these cuts.");
        return;
    }
    bool hasAudio = false, hasVideo = false;
    for (const auto &t : player_.tracks()) {
        const auto type = t.toMap().value("type").toString();
        hasAudio |= type == "audio";
        hasVideo |= type == "video";
    }
    QString graph, inputs;
    for (int i = 0; i < keep.size(); ++i) {
        const auto range = QString("start=%1:end=%2").arg(keep[i].first, 0, 'f', 3).arg(keep[i].second, 0, 'f', 3);
        if (hasVideo) {
            graph += QString("[0:v:0]trim=%1,setpts=PTS-STARTPTS[v%2];").arg(range).arg(i);
            inputs += QString("[v%1]").arg(i);
        }
        if (hasAudio) {
            graph += QString("[0:a:0]atrim=%1,asetpts=PTS-STARTPTS[a%2];").arg(range).arg(i);
            inputs += QString("[a%1]").arg(i);
        }
    }
    const QFileInfo src(editPath_);
    const QString suffix = src.suffix().isEmpty() ? QString() : "." + src.suffix();
    const Encoder enc = pickEncoder(useHardwareEncoder(), src.suffix().toLower());
    graph += QString("%1concat=n=%2:v=%3:a=%4%5%6")
                 .arg(inputs).arg(keep.size()).arg(hasVideo ? 1 : 0).arg(hasAudio ? 1 : 0)
                 .arg(hasVideo ? (enc.filter.isEmpty() ? "[v]" : "[vc]") : "", hasAudio ? "[a]" : "");
    if (hasVideo && !enc.filter.isEmpty())
        graph += QString(";[vc]%1[v]").arg(enc.filter);
    double total = 0;
    for (const auto &k : keep)
        total += k.second - k.first;

    QString output;
    if (overwrite) {
        output = src.dir().filePath("." + src.completeBaseName() + ".mpvideo-tmp" + suffix);
    } else {
        output = src.dir().filePath(src.completeBaseName() + " (cut)" + suffix);
        for (int n = 2; QFileInfo::exists(output); ++n)
            output = src.dir().filePath(QString("%1 (cut %2)%3").arg(src.completeBaseName()).arg(n).arg(suffix));
    }
    QStringList args{"-y", "-nostdin", "-loglevel", "error", "-progress", "pipe:1"};
    args << enc.input;
    if (enc.name == "NVENC" || enc.name == "VAAPI" || enc.name == "QSV")
        args << "-hwaccel" << "auto";
    args << "-i" << editPath_ << "-filter_complex" << graph;
    if (hasVideo)
        args << "-map" << "[v]" << enc.codec;
    if (hasAudio)
        args << "-map" << "[a]";
    if (hasAudio && src.suffix().toLower() == "webm")
        args << "-c:a" << "libopus";
    args << output;

    auto *proc = new QProcess(this);
    export_ = proc;
    cut_->setEnabled(false);
    updateEdit();
    status_->setText(QString("Saving with %1… 0%  (Stop to cancel)").arg(enc.name));
    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc, total, encName = enc.name] {
        const auto lines = QString::fromUtf8(proc->readAllStandardOutput()).split('\n');
        for (const auto &line : lines)
            if (line.startsWith("out_time_us=")) {
                const double done = line.mid(12).toDouble() / 1e6;
                status_->setText(QString("Saving with %1… %2%  (Stop to cancel)")
                                     .arg(encName).arg(qBound(0, int(done / total * 100), 99)));
            }
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc, output](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        status_->setText("Could not start ffmpeg. Is it installed?");
        QFile::remove(output);
        proc->deleteLater();
        export_.clear();
        updateEdit();
    });
    connect(proc, &QProcess::finished, this,
            [this, proc, output, overwrite](int code, QProcess::ExitStatus st) {
                const auto err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
                proc->deleteLater();
                export_.clear();
                const QString original = editPath_;
                if (st != QProcess::NormalExit || code != 0) {
                    QFile::remove(output);
                    status_->setText(st == QProcess::CrashExit ? "Save cancelled."
                                                               : "Save failed: " + err.left(200));
                    updateEdit();
                    return;
                }
                QString result = output;
                if (overwrite) {
                    if (std::rename(output.toLocal8Bit().constData(), original.toLocal8Bit().constData()) != 0) {
                        QFile::remove(output);
                        status_->setText("Could not replace the original file.");
                        updateEdit();
                        return;
                    }
                    result = original;
                }
                exitEdit();
                player_.command({"loadfile", result, "replace"});
                status_->setText("Saved " + QFileInfo(result).fileName());
            });
    proc->start("ffmpeg", args);
}
void Window::open(const QStringList &paths, bool append) {
    if (paths.isEmpty() || !player_.isReady())
        return;
    stageStack_->setCurrentWidget(video_);
    bool add = append;
    for (const auto &path : paths) {
        auto url = QUrl::fromUserInput(path, QDir::currentPath(), QUrl::AssumeLocalFile);
        player_.command({"loadfile", url.isLocalFile() ? url.toLocalFile() : url.toString(),
                         add ? "append-play" : "replace"});
        add = true;
    }
    status_->setText("Opening media…");
}
Modal *Window::dialog(const QString &title) {
    if (modal_ || desktopFilePicker_)
        return nullptr;
    auto *modal = new Modal(centralWidget(), content_, title);
    modal_ = modal;
    connect(modal, &Modal::dismissed, this, [this] { modal_.clear(); });
    return modal;
}
void Window::fileDialog(bool subtitles) {
    if (modal_ || desktopFilePicker_)
        return;
    if (useBuiltInFilePicker())
        builtInFileDialog(subtitles);
    else
        desktopFileDialog(subtitles);
}
bool Window::useBuiltInFilePicker() const {
    return settings_.value("files/useBuiltInPicker", settings_.value("files/useNativeDialog", true)).toBool();
}
void Window::desktopFileDialog(bool subtitles) {
    auto *picker = new DesktopFilePicker(this);
    desktopFilePicker_ = picker;
    const auto previousStatus = status_->text();
    const QString waiting = "Choose a file in your desktop file manager…";
    status_->setText(waiting);
    content_->setEnabled(false);
    connect(picker, &DesktopFilePicker::selected, this, [this, subtitles](const QStringList &paths) {
        if (subtitles)
            player_.command({"sub-add", paths.first(), "select"});
        else
            open(paths);
    });
    connect(picker, &DesktopFilePicker::failed, this, [this](const QString &message) {
        status_->setText(message);
        status_->setToolTip(message);
    });
    connect(picker, &DesktopFilePicker::finished, this, [this, picker, previousStatus, waiting] {
        content_->setEnabled(true);
        content_->setFocus();
        if (status_->text() == waiting)
            status_->setText(previousStatus);
        desktopFilePicker_.clear();
        picker->deleteLater();
    });
    QString parentWindow;
    if (QGuiApplication::platformName() == "xcb")
        parentWindow = "x11:" + QString::number(winId(), 16);
    picker->open(subtitles ? "Add subtitles" : "Open media", !subtitles, parentWindow);
}
void Window::builtInFileDialog(bool subtitles) {
    auto *modal = dialog(subtitles ? "Add subtitles" : "Open media");
    if (!modal)
        return;
    modal->setPreferredHeight(550);
    auto *input = new QLineEdit;
    input->setPlaceholderText(subtitles ? "Subtitle file path" : "File path, folder, or stream URL");
    input->setAccessibleName("Media location");
    modal->body()->addWidget(input);
    auto *list = new QListWidget;
    list->setAccessibleName("Files");
    list->setMinimumHeight(80);
    list->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    modal->body()->addWidget(list, 1);
    auto *location = text(QDir::homePath(), "status");
    location->setWordWrap(true);
    modal->body()->addWidget(location);
    auto directory = std::make_shared<QString>(QDir::homePath());
    auto populate = [list, location, directory](QString path) {
        *directory = path;
        location->setText(path);
        list->clear();
        QDir dir(path);
        if (dir.cdUp()) {
            auto *up = new QListWidgetItem("Parent folder", list);
            up->setData(Qt::UserRole, dir.absolutePath());
        }
        for (const auto &entry : QDir(path).entryInfoList(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot,
                                                          QDir::DirsFirst | QDir::Name)) {
            auto *item = new QListWidgetItem(entry.fileName() + (entry.isDir() ? "/" : ""), list);
            item->setData(Qt::UserRole, entry.absoluteFilePath());
        }
    };
    populate(*directory);
    auto *append = new QCheckBox("Add to queue");
    if (!subtitles)
        modal->footer()->addWidget(append);
    else
        append->setParent(modal);
    auto activate = [this, modal, input, directory, populate, subtitles, append] {
        auto value = input->text().trimmed();
        if (value.isEmpty())
            return;
        if (value.startsWith("~/"))
            value = QDir::homePath() + value.mid(1);
        auto url = QUrl::fromUserInput(value, *directory, QUrl::AssumeLocalFile);
        if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isDir()) {
            populate(url.toLocalFile());
            input->clear();
            return;
        }
        if (subtitles)
            player_.command({"sub-add", url.isLocalFile() ? url.toLocalFile() : url.toString(), "select"});
        else
            open({url.toString()}, append->isChecked());
        modal->dismiss();
    };
    connect(list, &QListWidget::itemActivated, modal, [input, activate](QListWidgetItem *item) {
        input->setText(item->data(Qt::UserRole).toString());
        activate();
    });
    connect(list, &QListWidget::itemClicked, modal,
            [input](QListWidgetItem *item) { input->setText(item->data(Qt::UserRole).toString()); });
    auto *actions = modal->footer();
    auto *add = new QPushButton(subtitles ? "Load subtitles" : "Open");
    actions->addStretch();
    actions->addWidget(add);

    connect(add, &QPushButton::clicked, modal, activate);
    connect(input, &QLineEdit::returnPressed, modal, activate);
    modal->present();
    input->setFocus();
}
void Window::trackDialog() {
    auto *modal = dialog("Audio & subtitles");
    if (!modal)
        return;
    for (const auto &type : QStringList{"audio", "sub"}) {
        modal->body()->addWidget(text(type == "audio" ? "Audio track" : "Subtitle track", "heading"));
        auto *group = new QButtonGroup(modal);
        group->setExclusive(true);
        auto add = [this, modal, group, type](QString label, QString id, bool selected) {
            auto *b = new QPushButton(label);
            b->setCheckable(true);
            b->setChecked(selected);
            group->addButton(b);
            modal->body()->addWidget(b);
            connect(b, &QPushButton::clicked, modal,
                    [this, type, id] { player_.setProperty(type == "audio" ? "aid" : "sid", id); });
        };
        bool hasSelection = false;
        for (const auto &track : player_.tracks()) {
            auto map = track.toMap();
            if (map.value("type").toString() == type && map.value("selected").toBool())
                hasSelection = true;
        }
        add("Off", "no", !hasSelection);
        for (const auto &track : player_.tracks()) {
            auto map = track.toMap();
            if (map.value("type").toString() != type)
                continue;
            QString label = map.value("title").toString();
            if (label.isEmpty())
                label = "Track " + map.value("id").toString();
            if (map.contains("lang"))
                label += " · " + map.value("lang").toString();
            add(label, map.value("id").toString(), map.value("selected").toBool());
        }
    }
    auto *load = new QPushButton("Load subtitle file…");
    modal->body()->addWidget(load);
    connect(load, &QPushButton::clicked, modal, [this, modal] {
        connect(modal, &Modal::dismissed, this, [this] { QTimer::singleShot(0, this, [this] { fileDialog(true); }); });
        modal->dismiss();
    });
    modal->present();
}
void Window::settingsDialog() {
    auto *modal = dialog("Settings");
    if (!modal)
        return;
    modal->setObjectName("settingsModal");
    // Playback stays paused for as long as settings are open, whatever tries to resume it.
    auto holdPause = [this] {
        const auto idle = player_.property("idle-active");
        if (idle.isValid() && !idle.toBool() && !player_.property("pause").toBool())
            player_.setProperty("pause", true);
    };
    holdPause();
    connect(&player_, &Player::changed, modal, holdPause);
    buildSpeedSettings(modal);
    buildSkipSetting(modal);
    buildFilePickerSetting(modal);
    auto *hardware = new ThemedCheckBox("Use the hardware encoder when available");
    hardware->setChecked(useHardwareEncoder());
    modal->body()->addWidget(hardware);
    connect(hardware, &QCheckBox::toggled, modal, [this](bool on) {
        settings_.setValue("export/hardwareEncoder", on);
        settings_.sync();
    });
    auto *help = text(QString("Space  Play / pause\nLeft / Right  Seek %1 seconds\nUp / Down  "
                              "Change volume\nM  Mute    F  Fullscreen    P  Queue\nCtrl+O  Open "
                              "media    Ctrl+Q  Quit\nEscape  Close dialog / leave fullscreen\nEditor  S split at playhead    Delete  Remove segment    Ctrl+Z  Undo")
                          .arg(skipSeconds()));
    help->setWordWrap(true);
    modal->body()->addWidget(help);
    auto *theme = text(
        "Theme: " + theme_.theme().name + "\nColors can be changed live with mpvideo theme set.", "status");
    theme->setWordWrap(true);
    modal->body()->addWidget(theme);
    modal->body()->addStretch();
    modal->present();
}
void Window::buildSpeedSettings(Modal *modal) {
    modal->body()->addWidget(text("Playback speed", "heading"));
    auto *row = new QHBoxLayout;
    auto *group = new QButtonGroup(modal);
    for (double speed : {0.5, 0.75, 1.0, 1.25, 1.5, 2.0}) {
        auto *button = new QPushButton(QString::number(speed) + "×");
        button->setCheckable(true);
        button->setFocusPolicy(Qt::TabFocus);
        button->setCursor(Qt::PointingHandCursor);
        group->addButton(button);
        row->addWidget(button);
        auto refreshSpeed = [this, button, speed] {
            button->setChecked(qFuzzyCompare(player_.property("speed").toDouble(), speed));
        };
        refreshSpeed();
        connect(&player_, &Player::changed, button, refreshSpeed);
        connect(button, &QPushButton::clicked, modal, [this, speed] { player_.setProperty("speed", speed); });
    }
    modal->body()->addLayout(row);
    auto *loop = new ThemedCheckBox("Repeat current media");
    loop->setChecked(player_.property("loop-file").toString() == "inf");
    modal->body()->addWidget(loop);
    connect(loop, &QCheckBox::toggled, modal,
            [this](bool on) { player_.setProperty("loop-file", on ? "inf" : "no"); });
}
bool Window::useHardwareEncoder() const { return settings_.value("export/hardwareEncoder", true).toBool(); }
int Window::skipSeconds() const { return qBound(1, settings_.value("playback/skipSeconds", 5).toInt(), 90); }
// mpv reports "pause" asynchronously, so rapid toggles track the requested state instead of the cached one.
void Window::togglePause() {
    const bool nowPaused = !pendingPause_.value_or(player_.property("pause").toBool());
    if (!nowPaused && editing_ && !trim_->segments().isEmpty() &&
        player_.property("time-pos").toDouble() >= trim_->segments().last().end - 0.1)
        player_.command({"seek", QString::number(trim_->segments().first().start, 'f', 3), "absolute+exact"});
    pendingPause_ = nowPaused;
    player_.setProperty("pause", nowPaused);
    play_->setSymbol(nowPaused ? "play" : "pause");
    if (!video_ || stageStack_->currentWidget() != video_)
        return;
    pauseIndicator_->move(video_->mapTo(content_, video_->rect().center()) -
                          QPoint(pauseIndicator_->width() / 2, pauseIndicator_->height() / 2));
    pauseIndicator_->flash(!nowPaused);
}
void Window::skip(int direction) {
    player_.command({"seek", QString::number(direction * skipSeconds()), "relative"});
    if (!video_ || stageStack_->currentWidget() != video_)
        return;
    // Back badge centered in the first third of the frame, forward badge in the last third.
    auto *indicator = direction < 0 ? skipBackIndicator_ : skipForwardIndicator_;
    const QPoint center = video_->mapTo(content_, QPoint(video_->width() * (direction < 0 ? 1 : 5) / 6,
                                                         video_->height() / 2));
    indicator->move(center - QPoint(indicator->width() / 2, indicator->height() / 2));
    indicator->flashSkip(direction, skipSeconds());
}
void Window::updateSkipButtons() {
    const QString badge = QString::number(skipSeconds());
    for (auto *button : {skipBack_, skipForward_}) {
        const bool back = button == skipBack_;
        const QString label = QString("%1 %2 seconds (%3)").arg(back ? "Back" : "Forward", badge, back ? "Left" : "Right");
        button->setBadge(badge);
        button->setAccessibleName(label);
        button->setToolTip(label);
    }
}
void Window::buildSkipSetting(Modal *modal) {
    modal->body()->addWidget(text("Skip length", "heading"));
    auto *row = new QHBoxLayout;
    auto *slider = new QSlider(Qt::Horizontal);
    slider->setRange(1, 90);
    slider->setValue(skipSeconds());
    slider->setAccessibleName("Skip length in seconds");
    slider->setCursor(Qt::PointingHandCursor);
    auto *value = text(QString::number(skipSeconds()) + " s");
    value->setMinimumWidth(40);
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    row->addWidget(slider, 1);
    row->addWidget(value);
    modal->body()->addLayout(row);
    connect(slider, &QSlider::valueChanged, modal, [this, value](int seconds) {
        value->setText(QString::number(seconds) + " s");
        settings_.setValue("playback/skipSeconds", seconds);
        updateSkipButtons();
    });
    connect(slider, &QSlider::sliderReleased, modal, [this] { settings_.sync(); });
    connect(slider, &QSlider::actionTriggered, modal, [this] { QTimer::singleShot(0, this, [this] { settings_.sync(); }); });
}
void Window::buildFilePickerSetting(Modal *modal) {
    auto *builtIn = new ThemedCheckBox("Use built-in file picker");
    builtIn->setChecked(useBuiltInFilePicker());
    modal->body()->addWidget(builtIn);
    auto *hint = text("Turn off to use your desktop’s default file chooser.", "status");
    hint->setWordWrap(true);
    modal->body()->addWidget(hint);
    connect(builtIn, &QCheckBox::toggled, modal, [this, hint](bool enabled) {
        settings_.setValue("files/useBuiltInPicker", enabled);
        settings_.sync();
        if (settings_.status() != QSettings::NoError)
            hint->setText("Could not save this setting. It will apply for this session.");
    });
}
void Window::shortcuts() {
    auto bind = [this](QString key, auto fn) {
        auto *s = new QShortcut(QKeySequence(key), this);
        connect(s, &QShortcut::activated, this, [this, fn] {
            if (!modal_ && !desktopFilePicker_)
                fn();
        });
    };
    bind("Space", [this] { togglePause(); });
    bind("Left", [this] { skip(-1); });
    bind("Right", [this] { skip(1); });
    bind("Up",
         [this] { player_.setProperty("volume", qMin(100.0, player_.property("volume").toDouble() + 5)); });
    bind("Down",
         [this] { player_.setProperty("volume", qMax(0.0, player_.property("volume").toDouble() - 5)); });
    bind("M", [this] { player_.command({"cycle", "mute"}); });
    bind("F", [this] { fullscreen(); });
    bind("P", [this] { queue_->setVisible(!queue_->isVisible()); });
    bind("Ctrl+O", [this] { fileDialog(); });
    auto *remove = new QShortcut(QKeySequence::Delete, queue_);
    remove->setContext(Qt::WidgetShortcut);
    connect(remove, &QShortcut::activated, this, [this] {
        if (queue_->currentRow() >= 0)
            player_.command({"playlist-remove", QString::number(queue_->currentRow())});
    });
    auto *quit = new QShortcut(QKeySequence::Quit, this);
    connect(quit, &QShortcut::activated, this, &QWidget::close);
}
void Window::fullscreen() { isFullScreen() ? showNormal() : showFullScreen(); }
void Window::setImmersive(bool on) {
    if (on == immersive_)
        return;
    immersive_ = on;
    for (auto &bar : bars_) {
        bar.anim->stop();
        bar.delay->stop();
        bar.pos = 1;
        bar.shown = on;
        bar.widget->setStyleSheet(on ? "QWidget#bar { background:rgba(0,0,0,170); }" : QString());
        bar.widget->show();
    }
    if (on) {
        layout_->removeWidget(header_);
        layout_->removeWidget(footer_);
        layout_->setContentsMargins(0, 0, 0, 0);
        layout_->setSpacing(0);
        header_->setContentsMargins(18, 12, 18, 8);
        footer_->setContentsMargins(18, 8, 18, 16);
        introDone_ = false;
        hideTimer_->start();
        placeBars();
    } else {
        hideTimer_->stop();
        header_->setContentsMargins(0, 0, 0, 0);
        footer_->setContentsMargins(0, 0, 0, 0);
        layout_->setContentsMargins(18, 12, 18, 16);
        layout_->setSpacing(10);
        layout_->insertWidget(0, header_);
        layout_->addWidget(footer_);
        content_->unsetCursor();
    }
}
void Window::initBars() {
    bars_[0] = {header_, nullptr, nullptr, true};
    bars_[1] = {footer_, nullptr, nullptr, false};
    for (auto &bar : bars_) {
        bar.anim = new QVariantAnimation(this);
        bar.anim->setDuration(220);
        bar.anim->setEasingCurve(QEasingCurve::OutCubic);
        connect(bar.anim, &QVariantAnimation::valueChanged, this, [this, &bar](const QVariant &v) {
            bar.pos = v.toReal();
            placeBars();
        });
        connect(bar.anim, &QVariantAnimation::finished, this, [&bar] {
            if (bar.pos <= 0)
                bar.widget->hide();
        });
        bar.delay = new QTimer(this);
        bar.delay->setSingleShot(true);
        bar.delay->setInterval(1000);
        connect(bar.delay, &QTimer::timeout, this, [this, &bar] { slideBar(bar, false); });
    }
    cursorTimer_ = new QTimer(this);
    cursorTimer_->setSingleShot(true);
    cursorTimer_->setInterval(2000);
    connect(cursorTimer_, &QTimer::timeout, this, [this] {
        if (immersive_ && isActiveWindow() && !bars_[0].shown && !bars_[1].shown)
            content_->setCursor(Qt::BlankCursor);
    });
}
void Window::slideBar(Bar &bar, bool show) {
    bar.delay->stop();
    if (bar.shown == show)
        return;
    bar.shown = show;
    if (show)
        bar.widget->show();
    bar.anim->stop();
    bar.anim->setStartValue(bar.pos);
    bar.anim->setEndValue(show ? 1.0 : 0.0);
    bar.anim->start();
}
void Window::placeBars() {
    int w = content_->width();
    for (auto &bar : bars_) {
        int h = bar.widget->sizeHint().height();
        int y = bar.top ? -qRound((1 - bar.pos) * h) : content_->height() - qRound(bar.pos * h);
        bar.widget->setGeometry(0, y, w, h);
        bar.widget->raise();
    }
}
// Both bars show for 2s on entering fullscreen; after that each one slides in while the cursor is over
// its own edge region and slides out 1s after the cursor leaves it.
void Window::updateBars(bool immediate) {
    if (!isFullScreen()) {
        setImmersive(false);
        return;
    }
    setImmersive(true);
    QPoint cursor = QCursor::pos();
    QPoint p = content_->mapFromGlobal(cursor);
    bool inside = content_->rect().adjusted(-4, -4, 4, 4).contains(p); // slack: the cursor can land just past the edge
    for (auto &bar : bars_) {
        int h = bar.widget->sizeHint().height() + 24;
        bool want = !introDone_ || modal_ || editing_ ||
                    (inside && (bar.top ? p.y() < h : p.y() > content_->height() - h));
        if (want)
            slideBar(bar, true);
        else if (bar.shown && immediate)
            slideBar(bar, false);
        else if (bar.shown && !bar.delay->isActive())
            bar.delay->start();
        if (want)
            bar.delay->stop();
    }
    // Hide the pointer only after it has idled, so it is never lost on refocus or monitor switches.
    if (cursor != lastCursor_ || !isActiveWindow() || !inside) {
        lastCursor_ = cursor;
        content_->unsetCursor();
        cursorTimer_->start();
    }
}
void Window::resizeEvent(QResizeEvent *e) {
    QMainWindow::resizeEvent(e);
    if (immersive_)
        placeBars();
    if (modal_)
        modal_->setGeometry(centralWidget()->rect());
}
bool Window::eventFilter(QObject *watched, QEvent *event) {
    if (watched == empty_ && event->type() == QEvent::MouseButtonRelease &&
        static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
        fileDialog(false);
        return true;
    }
    return QMainWindow::eventFilter(watched, event);
}
void Window::dragEnterEvent(QDragEnterEvent *e) {
    if (e->mimeData()->hasUrls() && !modal_ && !desktopFilePicker_)
        e->acceptProposedAction();
}
void Window::dropEvent(QDropEvent *e) {
    QStringList paths;
    for (const auto &url : e->mimeData()->urls())
        paths << url.toString();
    open(paths);
    e->acceptProposedAction();
}
void Window::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Escape) {
        if (desktopFilePicker_)
            desktopFilePicker_->cancel();
        else if (modal_)
            modal_->dismiss();
        else if (editing_)
            requestExitEdit();
        else if (isFullScreen())
            showNormal();
    } else
        QMainWindow::keyPressEvent(e);
}
}
