#include "theme_manager.h"
#include "theme_source.h"

#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QTimer>
#include <stdexcept>
#include <utility>

namespace mpvideo {
namespace {

QString absolutePath(QString path) {
    if (path.startsWith("~/"))
        path = QDir::homePath() + path.mid(1);
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString managerPath(QString path) {
    return path.isEmpty() ? defaultConfigPath() : absolutePath(std::move(path));
}

QString lockPath(const QString &path) {
    return QDir(QFileInfo(path).absolutePath()).filePath(".locks/theme.lock");
}

void setError(QString *error, const QString &message) {
    if (error)
        *error = message;
}

}

ThemeManager::ThemeManager(QString configPath, QObject *parent)
    : QObject(parent), path_(managerPath(std::move(configPath))), theme_(defaultTheme()) {
    debounce_.setSingleShot(true);
    debounce_.setInterval(45);
    poll_.setInterval(750);
    connect(&watcher_, &QFileSystemWatcher::fileChanged, this, [this] { debounce_.start(); });
    connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, [this] { debounce_.start(); });
    connect(&debounce_, &QTimer::timeout, this, [this] { reload(); });
    connect(&poll_, &QTimer::timeout, this, [this] { reload(); });

    if (QFileInfo::exists(path_)) {
        QString error;
        if (loadTheme(path_, &theme_, &error) && !theme_.sourceKind.isEmpty()) {
            Theme refreshed = theme_;
            if (refreshFollowedTheme(&refreshed, &error) && refreshed != theme_) {
                QString saveError;
                if (saveTheme(path_, refreshed, &saveError))
                    theme_ = std::move(refreshed);
            }
        }
    } else {
        theme_.sourceKind = "active";
        theme_.sourcePath = activePalettePath();
        Theme followed = theme_;
        QString ignoredError;
        if (followActiveTheme(&followed, &ignoredError))
            theme_ = std::move(followed);
        QString ignoredSaveError;
        saveTheme(path_, theme_, &ignoredSaveError);
    }
    updateWatches();
    poll_.start();
    QTimer::singleShot(0, this, [this] { reload(); });
}

QString ThemeManager::defaultConfigPath() { return mpvideo::defaultConfigPath(); }

bool ThemeManager::apply(Theme next, QString *error) {
    const QString lockFile = lockPath(path_);
    if (!QDir().mkpath(QFileInfo(lockFile).absolutePath())) {
        setError(error, "Cannot create theme lock directory");
        return false;
    }
    QLockFile lock(lockFile);
    if (!lock.tryLock(2000)) {
        setError(error, "Theme is being edited by another process");
        return false;
    }
    QString saveError;
    if (!saveTheme(path_, next, &saveError)) {
        setError(error, saveError);
        return false;
    }
    if (error)
        error->clear();
    if (next != theme_) {
        theme_ = std::move(next);
        emit changed();
    }
    lastRejected_.clear();
    updateWatches();
    return true;
}

bool ThemeManager::setTheme(const QString &name, QString *error) {
    try {
        return apply(presetTheme(name), error);
    } catch (const std::exception &exception) {
        setError(error, QString::fromUtf8(exception.what()));
        return false;
    }
}

bool ThemeManager::setColors(const QStringList &assignments, QString *error) {
    if (assignments.isEmpty()) {
        setError(error, "Provide at least one COLOR=#RRGGBB assignment");
        return false;
    }
    QJsonObject data = theme_.toJson();
    QJsonObject colors = data.value("colors").toObject();
    for (const QString &assignment : assignments) {
        const qsizetype separator = assignment.indexOf('=');
        const QString key = assignment.left(separator);
        if (separator < 1 || !colorKeys().contains(key)) {
            setError(error, "Expected COLOR=#RRGGBB. Colors: " + colorKeys().join(", "));
            return false;
        }
        colors.insert(key, assignment.mid(separator + 1));
    }
    data.insert("name", "Custom");
    data.insert("colors", colors);
    data.remove("source");
    Theme next;
    if (!parseTheme(data, &next, error))
        return false;
    return apply(std::move(next), error);
}

bool ThemeManager::importTheme(const QString &sourcePath, QString *error) {
    Theme next;
    if (!mpvideo::importTheme(sourcePath, &next, error))
        return false;
    return apply(std::move(next), error);
}

bool ThemeManager::followActive(QString *error) {
    Theme next;
    if (!followActiveTheme(&next, error))
        return false;
    return apply(std::move(next), error);
}

bool ThemeManager::followStatic(const QString &name, QString *error) {
    Theme next;
    if (!followStaticTheme(name, &next, error))
        return false;
    return apply(std::move(next), error);
}

bool ThemeManager::sync(QString *error) {
    Theme next = theme_;
    if (!refreshFollowedTheme(&next, error))
        return false;
    return apply(std::move(next), error);
}

bool ThemeManager::reset(QString *error) { return apply(defaultTheme(), error); }

bool ThemeManager::reload(QString *error) {
    const QString lockFile = lockPath(path_);
    if (!QDir().mkpath(QFileInfo(lockFile).absolutePath())) {
        const QString message = "Cannot create theme lock directory";
        if (message != lastRejected_) {
            lastRejected_ = message;
            emit rejected(message);
        }
        setError(error, message);
        return false;
    }
    QLockFile lock(lockFile);
    if (!lock.tryLock(0))
        return true;

    Theme next;
    QString message;
    if (!loadTheme(path_, &next, &message)) {
        if (message != lastRejected_) {
            lastRejected_ = message;
            emit rejected(message);
        }
        setError(error, message);
        updateWatches();
        return false;
    }
    if (!next.sourceKind.isEmpty()) {
        const QString sourceCanonical = QFileInfo(next.sourcePath).canonicalFilePath();
        const QString configCanonical = QFileInfo(path_).canonicalFilePath();
        if (!sourceCanonical.isEmpty() && sourceCanonical == configCanonical) {
            message = "Theme source cannot be its own output file";
        } else if (!refreshFollowedTheme(&next, &message)) {
            message = message.isEmpty() ? "Cannot refresh theme source" : message;
        } else if (next.toJson() != theme_.toJson()) {
            QString saveError;
            if (!saveTheme(path_, next, &saveError))
                message = saveError;
        }
    }
    lock.unlock();
    if (!message.isEmpty()) {
        if (message != lastRejected_) {
            lastRejected_ = message;
            emit rejected(message);
        }
        setError(error, message);
        updateWatches();
        return false;
    }
    if (error)
        error->clear();
    lastRejected_.clear();
    if (next != theme_) {
        theme_ = std::move(next);
        emit changed();
    }
    updateWatches();
    return true;
}

void ThemeManager::updateWatches() {
    QStringList wanted{QFileInfo(path_).absolutePath()};
    if (QFileInfo::exists(path_))
        wanted.append(path_);
    wanted.append(sourceWatchPaths(theme_));
    wanted.removeDuplicates();
    const QStringList current = watcher_.files() + watcher_.directories();
    for (const QString &path : current) {
        if (!wanted.contains(path))
            watcher_.removePath(path);
    }
    for (const QString &path : wanted) {
        if (!current.contains(path) && QFileInfo(path).exists())
            watcher_.addPath(path);
    }
}

}
