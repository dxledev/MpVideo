#pragma once

#include <QColor>
#include <QMainWindow>
#include <QMap>
#include <QPointer>
#include <QVariantList>
#include <QStringList>

class QLabel;
class QListWidget;
class QShortcut;
class QPushButton;
class QTimer;
class QResizeEvent;
class QDragEnterEvent;
class QDropEvent;
class QUrl;
class GlyphButton;
class QSlider;
class QSplitter;
class QWidget;

namespace mpvideo {
class Player;
class VideoWidget;
class ThemeManager;
class Modal;
}

class Window final : public QMainWindow {
public:
    explicit Window(QString configDirectory, QStringList files = {});
    mpvideo::Player *player() const;
    void applyTheme(QMap<QString, QColor> colors);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QString configDirectory_;
    QMap<QString, QColor> themeColors_;
    mpvideo::ThemeManager *themeManager_ = nullptr;
    mpvideo::Player *player_ = nullptr;
    mpvideo::VideoWidget *video_ = nullptr;
    QPointer<mpvideo::Modal> modal_;
    QSplitter *contentSplitter_ = nullptr;
    QWidget *header_ = nullptr;
    QWidget *stagePanel_ = nullptr;
    QWidget *playlistPanel_ = nullptr;
    QWidget *emptyOverlay_ = nullptr;
    QWidget *emptyPlaylist_ = nullptr;
    QLabel *mediaTitle_ = nullptr;
    QLabel *positionLabel_ = nullptr;
    QLabel *durationLabel_ = nullptr;
    QLabel *playlistTitle_ = nullptr;
    QListWidget *playlistList_ = nullptr;
    QSlider *seekSlider_ = nullptr;
    QSlider *volumeSlider_ = nullptr;
    QPushButton *playlistToggle_ = nullptr;
    GlyphButton *playButton_ = nullptr;
    GlyphButton *volumeButton_ = nullptr;
    GlyphButton *fullscreenButton_ = nullptr;
    QTimer *seekTimer_ = nullptr;
    QVariantList lastPlaylist_;
    int lastPlaylistPosition_ = -2;
    int pendingSeekValue_ = 0;
    double volumeBeforeMute_ = 100.0;
    bool playlistVisible_ = true;
    bool fullscreen_ = false;

    QWidget *buildInterface();
    QWidget *buildStage();
    QWidget *buildPlaylist();
    QWidget *buildTransport();
    void createShortcuts();
    void applyThemeStyle();
    void updateResponsiveLayout();
    void updatePlayerState();
    void updatePlaylist();
    void updateSeekFromPlayer();
    void seekToSliderValue(int value);
    void seekRelative(double seconds);
    void openFiles(const QStringList &files);
    void openDroppedUrls(const QList<QUrl> &urls);
    void setFullscreen(bool enabled);
    void togglePlaylist();
    void showFilePicker();
    void showUrlDialog();
    void showTracksDialog();
    void showSettingsDialog();
    void showError(const QString &message);
    mpvideo::Modal *showModal(const QString &title, QWidget *content, QWidget *initialFocus = nullptr);
};
