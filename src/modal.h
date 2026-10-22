#pragma once
#include <QPixmap>
#include <QPointer>
#include <QVBoxLayout>
#include <QWidget>
class QHBoxLayout;
namespace mpvideo {
class Modal : public QWidget {
    Q_OBJECT
  public:
    Modal(QWidget *parent, QWidget *background, const QString &title);
    QVBoxLayout *body() const { return body_; }
    QHBoxLayout *footer() const { return footer_; }
    void setPreferredHeight(int height) { preferredHeight_ = height; }
    void present();
    void dismiss();
  signals:
    void dismissed();

  protected:
    void paintEvent(QPaintEvent *) override;
    void changeEvent(QEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;

  private:
    QWidget *background_;
    QWidget *card_;
    QVBoxLayout *body_;
    QHBoxLayout *footer_;
    QPixmap snapshot_;
    QPointer<QWidget> previousFocus_;
    bool closing_ = false;
    int preferredHeight_ = 0;
    void capture();
    void fit();
};
} // namespace mpvideo
