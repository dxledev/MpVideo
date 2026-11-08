#pragma once
#include <QObject>
#include <QTimeLine>
namespace mpvideo {
// Drives the progress (0..1) used to fade and slightly scale a modal card in and out (120 ms, OutCubic).
class ModalMotion : public QObject {
    Q_OBJECT
  public:
    static constexpr int duration = 120;
    explicit ModalMotion(QObject *parent = nullptr);
    qreal progress() const { return progress_; }
    qreal scale() const { return .98 + .02 * progress_; }
    void animate(bool open);
    void reset();
  signals:
    void progressChanged(qreal progress);
    void finished(bool open);

  private:
    QTimeLine timeline_;
    qreal startProgress_ = 0;
    qreal progress_ = 0;
    bool targetOpen_ = false;
    void setProgress(qreal value);
};
} // namespace mpvideo
