#include "video_widget.h"
#include <QMouseEvent>

#include "player.h"

#include <QByteArray>
#include <QMetaObject>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>

#include <cstdlib>

#include <algorithm>
#include <cmath>
#include <mpv/render_gl.h>

namespace mpvideo {

struct VideoWidget::CallbackBridge {
    std::mutex mutex;
    VideoWidget *target = nullptr;
    std::atomic_bool updateQueued = false;
};

VideoWidget::VideoWidget(Player *player, QWidget *parent)
    : QOpenGLWidget(parent), player_(player), callbackBridge_(std::make_unique<CallbackBridge>()) {
    callbackBridge_->target = this;
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
    setAutoFillBackground(false);

    if (player) {
        connect(player, &Player::aboutToShutdown, this, &VideoWidget::releaseRenderContext,
                Qt::DirectConnection);
    }
}

VideoWidget::~VideoWidget() {
    releaseRenderContext();
    QObject::disconnect(contextDestructionConnection_);

    std::lock_guard lock(callbackBridge_->mutex);
    callbackBridge_->target = nullptr;
}

void VideoWidget::initializeGL() {
    if (QOpenGLContext *glContext = context()) {
        contextDestructionConnection_ = connect(glContext, &QOpenGLContext::aboutToBeDestroyed, this,
                                                &VideoWidget::releaseRenderContext, Qt::DirectConnection);
    }

    if (!createRenderContext()) {
        QOpenGLContext *glContext = QOpenGLContext::currentContext();
        if (glContext) {
            QOpenGLFunctions *functions = glContext->functions();
            functions->glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
            functions->glClear(GL_COLOR_BUFFER_BIT);
        }
    }
}

void VideoWidget::paintGL() {
    QOpenGLContext *glContext = QOpenGLContext::currentContext();
    if (!renderContext_ || !glContext || !player_ || !player_->handle()) {
        if (glContext) {
            QOpenGLFunctions *functions = glContext->functions();
            functions->glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
            functions->glClear(GL_COLOR_BUFFER_BIT);
        }
        return;
    }

    const qreal scale = devicePixelRatioF();
    mpv_opengl_fbo framebuffer{.fbo = static_cast<int>(defaultFramebufferObject()),
                               .w = std::max(1, qRound(width() * scale)),
                               .h = std::max(1, qRound(height() * scale)),
                               .internal_format = 0};
    int flipY = 1;
    int blockForTarget = 0; // the widget's swap paces frames; never sleep inside paintGL
    mpv_render_param parameters[]{{MPV_RENDER_PARAM_OPENGL_FBO, &framebuffer},
                                  {MPV_RENDER_PARAM_FLIP_Y, &flipY},
                                  {MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME, &blockForTarget},
                                  {MPV_RENDER_PARAM_INVALID, nullptr}};

    glContext->functions()->glViewport(0, 0, framebuffer.w, framebuffer.h);
    mpv_render_context_update(renderContext_);
    const int result = mpv_render_context_render(renderContext_, parameters);
    if (result < 0) {
        const char *description = mpv_error_string(result);
        reportRenderError(
            QStringLiteral("libmpv rendering failed: %1")
                .arg(description ? QString::fromUtf8(description) : QStringLiteral("Unknown error")));
    } else {
        lastRenderError_.clear();
    }
}

void VideoWidget::renderUpdateCallback(void *context) noexcept {
    auto *bridge = static_cast<CallbackBridge *>(context);
    if (!bridge)
        return;

    std::lock_guard lock(bridge->mutex);
    VideoWidget *target = bridge->target;
    if (!target || bridge->updateQueued.exchange(true, std::memory_order_acq_rel))
        return;

    QMetaObject::invokeMethod(
        target,
        [target, bridge] {
            bridge->updateQueued.store(false, std::memory_order_release);
            target->update();
        },
        Qt::QueuedConnection);
}

void *VideoWidget::getProcAddress(void *context, const char *name) {
    Q_UNUSED(context)

    QOpenGLContext *glContext = QOpenGLContext::currentContext();
    if (!glContext || !name)
        return nullptr;

    return reinterpret_cast<void *>(glContext->getProcAddress(QByteArray(name)));
}

bool VideoWidget::createRenderContext() {
    if (renderContext_)
        return true;
    if (!player_ || !player_->isReady() || !player_->handle()) {
        reportRenderError(QStringLiteral("The libmpv player is not available"));
        return false;
    }
    if (!QOpenGLContext::currentContext()) {
        reportRenderError(QStringLiteral("An active OpenGL context is required for libmpv rendering"));
        return false;
    }

    mpv_opengl_init_params glParameters{.get_proc_address = &VideoWidget::getProcAddress,
                                        .get_proc_address_ctx = nullptr};
    char apiType[] = MPV_RENDER_API_TYPE_OPENGL;
    mpv_render_param parameters[]{{MPV_RENDER_PARAM_API_TYPE, apiType},
                                  {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glParameters},
                                  {MPV_RENDER_PARAM_INVALID, nullptr}};

    const int result = mpv_render_context_create(&renderContext_, player_->handle(), parameters);
    if (result < 0) {
        renderContext_ = nullptr;
        const char *description = mpv_error_string(result);
        reportRenderError(
            QStringLiteral("Could not create the libmpv OpenGL renderer: %1")
                .arg(description ? QString::fromUtf8(description) : QStringLiteral("Unknown error")));
        return false;
    }

    rendererContext_ = QOpenGLContext::currentContext();
    cleanupSurface_ = std::make_unique<QOffscreenSurface>(rendererContext_->screen());
    cleanupSurface_->setFormat(rendererContext_->format());
    cleanupSurface_->create();
    mpv_render_context_set_update_callback(renderContext_, &VideoWidget::renderUpdateCallback,
                                           callbackBridge_.get());
    lastRenderError_.clear();
    return true;
}

void VideoWidget::releaseRenderContext() {
    if (!renderContext_)
        return;

    QOpenGLContext *rendererContext = rendererContext_.data();
    if (!rendererContext)
        qFatal("The libmpv renderer's OpenGL context was destroyed before renderer cleanup");

    const bool alreadyCurrent = QOpenGLContext::currentContext() == rendererContext;
    if (!alreadyCurrent && context() == rendererContext)
        makeCurrent();

    if (QOpenGLContext::currentContext() != rendererContext && cleanupSurface_ && cleanupSurface_->isValid())
        rendererContext->makeCurrent(cleanupSurface_.get());

    if (QOpenGLContext::currentContext() != rendererContext)
        qFatal("Could not make the libmpv renderer's OpenGL context current for cleanup");

    mpv_render_context_set_update_callback(renderContext_, nullptr, nullptr);
    mpv_render_context_free(renderContext_);
    renderContext_ = nullptr;
    rendererContext_.clear();
    if (!alreadyCurrent)
        rendererContext->doneCurrent();
    cleanupSurface_.reset();
}

void VideoWidget::reportRenderError(const QString &message) {
    if (lastRenderError_ == message)
        return;

    lastRenderError_ = message;
    emit error(message);
}

}

namespace mpvideo {
void VideoWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton)
        emit clicked();
    QOpenGLWidget::mouseReleaseEvent(event);
}
} // namespace mpvideo
