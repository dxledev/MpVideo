#pragma once
#include <QObject>
#include <QStringList>
#include <QVariantMap>

namespace mpvideo {
class DesktopFilePicker : public QObject {
    Q_OBJECT
  public:
    explicit DesktopFilePicker(QObject *parent = nullptr);
    ~DesktopFilePicker() override;
    void open(const QString &title, bool multiple, const QString &parentWindow = {});
    void cancel();

  signals:
    void selected(const QStringList &paths);
    void failed(const QString &message);
    void finished();

  private slots:
    void response(uint code, const QVariantMap &results);

  private:
    QString handle_;
    bool active_ = false;
    quint64 requestId_ = 0;
    bool subscribe(const QString &handle);
    void unsubscribe();
    void closeRequest();
    void finish(const QString &error = {});
};
}
