#include "window.h"
#include "desktop_file_picker.h"
#include "icon_button.h"
#include "modal.h"
#include "seek_slider.h"
#include "themed_checkbox.h"
#include "video_widget.h"
#include <QApplication>
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
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedLayout>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <cmath>
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
    connect(&player_, &Player::changed, this, &Window::refresh);
    auto error = [this](const QString &message) {
        status_->setText(message);
        status_->setToolTip(message);
    };
    connect(&player_, &Player::error, this, error);

    refresh();
}
Window::~Window() {
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
    auto *layout = new QVBoxLayout(content_);
    layout->setContentsMargins(18, 12, 18, 16);
    layout->setSpacing(10);
    buildHeader(layout);
    buildStage(layout);
    buildTransport(layout);
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
    empty_->setAttribute(Qt::WA_TransparentForMouseEvents);
    stack->addWidget(empty_);
    if (player_.isReady()) {
        // Adding the first OpenGL widget after show() recreates the native window.
        // Keep it hidden here so libmpv rendering still starts only on selection.
        video_ = new VideoWidget(&player_);
        stack->addWidget(video_);
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
    play_ = makeButton("play", "Play or pause (Space)", [this] { player_.command({"cycle", "pause"}); });
    controls->addWidget(play_);
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
    play_->setSymbol(player_.property("pause").toBool() || idle ? "play" : "pause");
    play_->setEnabled(!idle);
    seek_->setEnabled(duration > 0);
    if (!seek_->isSliderDown()) {
        QSignalBlocker block(seek_);
        seek_->setValue(duration > 0 ? qBound(0, int(pos / duration * 10000), 10000) : 0);
    }
    time_->setText(stamp(pos) + " / " + stamp(duration));
    if (!volume_->isSliderDown()) {
        QSignalBlocker block(volume_);
        volume_->setValue(player_.property("volume").toInt());
    }
    mute_->setSymbol(player_.property("mute").toBool() ? "mute" : "volume");
    auto title = player_.property("media-title").toString();
    title_->setText(title);
    title_->setToolTip(title);
    setWindowTitle(title.isEmpty() ? "MpVideo" : title + " · MpVideo");
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
    queue_->setCurrentRow(player_.property("playlist-pos").toInt());
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
        modal->dismiss();
        QTimer::singleShot(0, this, [this] { fileDialog(true); });
    });
    modal->present();
}
void Window::settingsDialog() {
    auto *modal = dialog("Settings");
    if (!modal)
        return;
    modal->setObjectName("settingsModal");
    buildSpeedSettings(modal);
    buildFilePickerSetting(modal);
    auto *help = text("Space  Play / pause\nLeft / Right  Seek 5 seconds\nUp / Down  "
                      "Change volume\nM  Mute    F  Fullscreen    P  Queue\nCtrl+O  Open "
                      "media    Ctrl+Q  Quit\nEscape  Close dialog / leave fullscreen");
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
    bind("Space", [this] { player_.command({"cycle", "pause"}); });
    bind("Left", [this] { player_.command({"seek", "-5", "relative"}); });
    bind("Right", [this] { player_.command({"seek", "5", "relative"}); });
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
void Window::resizeEvent(QResizeEvent *e) {
    QMainWindow::resizeEvent(e);
    if (modal_)
        modal_->setGeometry(centralWidget()->rect());
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
        else if (isFullScreen())
            showNormal();
    } else
        QMainWindow::keyPressEvent(e);
}
}
