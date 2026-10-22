#pragma once

#include "theme.h"

#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>

namespace mpvideo {

class ThemeManager final : public QObject {
    Q_OBJECT
  public:
    explicit ThemeManager(QString configPath = {}, QObject *parent = nullptr);
    static QString defaultConfigPath();

    const Theme &theme() const { return theme_; }
    QString configPath() const { return path_; }

    bool setTheme(const QString &name, QString *error = nullptr);
    bool setColors(const QStringList &assignments, QString *error = nullptr);
    bool importTheme(const QString &sourcePath, QString *error = nullptr);
    bool followActive(QString *error = nullptr);
    bool followStatic(const QString &name, QString *error = nullptr);
    bool sync(QString *error = nullptr);
    bool reset(QString *error = nullptr);

  signals:
    void changed();
    void rejected(const QString &message);

  private:
    QString path_;
    QString lastRejected_;
    Theme theme_;
    QFileSystemWatcher watcher_;
    QTimer debounce_;
    QTimer poll_;

    bool apply(Theme next, QString *error);
    bool reload(QString *error = nullptr);
    void updateWatches();
};

}
