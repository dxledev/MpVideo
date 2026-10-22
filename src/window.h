#pragma once
#include "palette/theme_manager.h"
#include "player.h"
#include <QMainWindow>
#include <QPointer>
#include <functional>
class QLabel;
class QSlider;
class QListWidget;
class QLineEdit;
class QVBoxLayout;
class QStackedLayout;
namespace mpvideo {
class VideoWidget;
class IconButton;
class Modal;
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

  private:
    Player player_;
    ThemeManager theme_;
    QWidget *content_;
    VideoWidget *video_ = nullptr;
    QStackedLayout *stageStack_;
    QLabel *empty_;
    QLabel *title_;
    QLabel *time_;
    QLabel *status_;
    QSlider *seek_;
    QSlider *volume_;
    IconButton *play_;
    IconButton *mute_;
    QListWidget *queue_;
    QPointer<Modal> modal_;
    void build();
    void buildHeader(QVBoxLayout *layout);
    void buildStage(QVBoxLayout *layout);
    void buildTransport(QVBoxLayout *layout);
    IconButton *makeButton(QString icon, QString label, std::function<void()> action);
    void applyTheme();
    void refresh();
    void shortcuts();
    void fileDialog(bool subtitles = false);
    void trackDialog();
    void settingsDialog();
    Modal *dialog(const QString &title);
    void fullscreen();
};
} // namespace mpvideo
