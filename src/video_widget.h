#pragma once

#include <QMetaObject>
#include <QOffscreenSurface>
#include <QOpenGLWidget>
#include <QPointer>

#include <mpv/render.h>

#include <atomic>
#include <memory>
#include <mutex>

namespace mpvideo {

class Player;

class VideoWidget final : public QOpenGLWidget {
    Q_OBJECT

  public:
    explicit VideoWidget(Player *player, QWidget *parent = nullptr);
    ~VideoWidget() override;

    VideoWidget(const VideoWidget &) = delete;
    VideoWidget &operator=(const VideoWidget &) = delete;

  signals:
    void error(const QString &message);

  protected:
    void initializeGL() override;
    void paintGL() override;

  private:
    struct CallbackBridge;

    static void renderUpdateCallback(void *context) noexcept;
    static void *getProcAddress(void *context, const char *name);

    bool createRenderContext();
    void releaseRenderContext();
    void reportRenderError(const QString &message);

    QPointer<Player> player_;
    mpv_render_context *renderContext_ = nullptr;
    QPointer<QOpenGLContext> rendererContext_;
    std::unique_ptr<QOffscreenSurface> cleanupSurface_;
    QMetaObject::Connection contextDestructionConnection_;
    QString lastRenderError_;
    std::unique_ptr<CallbackBridge> callbackBridge_;
};

}
