#pragma once

#include "theme.hpp"

#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>

namespace mpvideo {

class ThemeManager : public QObject {
    Q_OBJECT
public:
    explicit ThemeManager(QString configDirectory = {}, QObject *parent = nullptr);

    const Theme &current() const;
    const Theme &theme() const { return current(); }
    QString themeFilePath() const;
    QString configPath() const { return themeFilePath(); }
    void reload();

signals:
    void changed();
    void rejected(const QString &message);

private:
    QString configDirectory_;
    QString path_;
    Theme theme_;
    Theme configuredTheme_;
    QFileSystemWatcher watcher_;
    QTimer debounce_;
    QTimer poll_;
    QString lastError_;

    void updateWatches();
};

}
