#pragma once
#include <QObject>
#include <QPointer>
#include <QTimer>
class QLabel;
class QWidget;
namespace mpvideo {
class TooltipFilter : public QObject {
  public:
    explicit TooltipFilter(QObject *parent = nullptr) : QObject(parent) {
        pending_.setSingleShot(true);
    }

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    static constexpr int kShowDelayMs = 800;
    void show(QWidget *widget, const QPoint &globalPos);
    QTimer pending_;
    QPointer<QLabel> label_;
};
} // namespace mpvideo
