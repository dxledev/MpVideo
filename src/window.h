#pragma once
#include "palette/theme_manager.h"
#include "player.h"
#include <QMainWindow>
#include <QPointer>
#include <QSettings>
#include <functional>
class QLabel;
class QSlider;
class QListWidget;
class QLineEdit;
class QVBoxLayout;
class QStackedLayout;
class QTimer;
class QVariantAnimation;
namespace mpvideo {
class DesktopFilePicker;
class VideoWidget;
class IconButton;
class Modal;
class PauseIndicator;
class Window : public QMainWindow {
    Q_OBJECT
  public:
    explicit Window(QString configPath = {}, const QStringList &mpvOptions = {});
    ~Window() override;
    void open(const QStringList &paths, bool append = false);

  protected:
    void resizeEvent(QResizeEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    Player player_;
    ThemeManager theme_;
    QSettings settings_;
    QWidget *content_;
    VideoWidget *video_ = nullptr;
    QStackedLayout *stageStack_;
    QVBoxLayout *layout_;
    PauseIndicator *pauseIndicator_;
    PauseIndicator *skipBackIndicator_;
    PauseIndicator *skipForwardIndicator_;
    QWidget *header_;
    QWidget *footer_;
    struct Bar {
        QWidget *widget;
        QVariantAnimation *anim;
        QTimer *delay;
        bool top;
        bool shown = false;
        qreal pos = 1; // 0 = fully off-screen, 1 = fully visible
    };
    Bar bars_[2];
    QTimer *cursorTimer_;
    QPoint lastCursor_;
    QTimer *hideTimer_;
    bool immersive_ = false;
    bool introDone_ = false;
    QLabel *empty_;
    QLabel *title_;
    QLabel *time_;
    QLabel *status_;
    QSlider *seek_;
    QSlider *volume_;
    IconButton *play_;
    IconButton *skipBack_;
    IconButton *skipForward_;
    IconButton *mute_;
    QListWidget *queue_;
    QPointer<Modal> modal_;
    QPointer<DesktopFilePicker> desktopFilePicker_;
    void build();
    void buildHeader(QVBoxLayout *layout);
    void buildStage(QVBoxLayout *layout);
    void buildTransport(QVBoxLayout *layout);
    IconButton *makeButton(QString icon, QString label, std::function<void()> action);
    void applyTheme();
    void refresh();
    void shortcuts();
    void fileDialog(bool subtitles = false);
    void builtInFileDialog(bool subtitles);
    void desktopFileDialog(bool subtitles);
    bool useBuiltInFilePicker() const;
    void trackDialog();
    void settingsDialog();
    void buildSpeedSettings(Modal *modal);
    void buildFilePickerSetting(Modal *modal);
    Modal *dialog(const QString &title);
    void fullscreen();
    int skipSeconds() const;
    void skip(int direction);
    void updateSkipButtons();
    void buildSkipSetting(Modal *modal);
    void setImmersive(bool on);
    void placeBars();
    void updateBars(bool immediate);
    void initBars();
    void slideBar(Bar &bar, bool show);
};
} // namespace mpvideo
