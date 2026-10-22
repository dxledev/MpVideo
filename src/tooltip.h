#pragma once
#include <QObject>
#include <QPointer>
class QLabel;
namespace mpvideo {
class TooltipFilter : public QObject {
  public:
    explicit TooltipFilter(QObject *parent = nullptr) : QObject(parent) {}

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    QPointer<QLabel> label_;
};
} // namespace mpvideo
