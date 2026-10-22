#include "theme_manager.hpp"

#include "theme_source.hpp"
#include "theme_storage.hpp"

#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <exception>
#include <stdexcept>

namespace mpvideo {

ThemeManager::ThemeManager(QString configDirectory, QObject *parent)
    : QObject(parent),
      configDirectory_(configDirectory.isEmpty() ? defaultConfigDirectory()
                                                 : QDir::cleanPath(QFileInfo(configDirectory).absoluteFilePath())),
      path_(configDirectory.isEmpty() ? mpvideo::themeFilePath(configDirectory_)
          : QFileInfo(configDirectory).fileName() == "theme.json"
              ? QDir::cleanPath(QFileInfo(configDirectory).absoluteFilePath())
              : mpvideo::themeFilePath(configDirectory_)),
      theme_(defaultTheme()),
      configuredTheme_(theme_) {
    debounce_.setSingleShot(true);
    debounce_.setInterval(60);
    poll_.setInterval(400);
    connect(&watcher_, &QFileSystemWatcher::fileChanged, this, [this] { debounce_.start(); });
    connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, [this] { debounce_.start(); });
    connect(&debounce_, &QTimer::timeout, this, &ThemeManager::reload);
    connect(&poll_, &QTimer::timeout, this, &ThemeManager::reload);
    updateWatches();
    poll_.start();
    reload();
}

const Theme &ThemeManager::current() const {
    return theme_;
}

QString ThemeManager::themeFilePath() const {
    return path_;
}

void ThemeManager::updateWatches() {
    QStringList wanted;
    const auto directory = QFileInfo(path_).absolutePath();
    if (QFileInfo(directory).isDir())
        wanted.append(directory);
    if (QFileInfo::exists(path_))
        wanted.append(path_);
    wanted.append(sourceWatchPaths(configuredTheme_));
    wanted.removeDuplicates();

    const auto currentPaths = watcher_.files() + watcher_.directories();
    for (const auto &path : currentPaths)
        if (!wanted.contains(path))
            watcher_.removePath(path);

    const auto refreshed = watcher_.files() + watcher_.directories();
    for (const auto &path : wanted) {
        const QFileInfo info(path);
        if (!refreshed.contains(path) && info.exists() && !info.isSymLink())
            watcher_.addPath(path);
    }
}

void ThemeManager::reload() {
    const auto lockPath = themeLockPath(path_);
    if (!QDir().mkpath(QFileInfo(lockPath).absolutePath())) {
        if (lastError_ != "Cannot create theme lock directory") {
            lastError_ = "Cannot create theme lock directory";
            emit rejected(lastError_);
        }
        return;
    }
    QLockFile lock(lockPath);
    if (!lock.tryLock(0))
        return;

    QString errorMessage;
    bool didChange = false;
    try {
        const Theme configured = QFileInfo::exists(path_) ? loadTheme(path_) : defaultTheme();
        configuredTheme_ = configured;
        if (!configured.sourceKind.isEmpty() && sourceTargetsFile(configured, path_))
            throw std::invalid_argument("A theme cannot follow its own output file");
        const Theme resolved = configured.sourceKind.isEmpty() ? configured : refreshSource(configured);
        if (resolved != configured)
            saveTheme(path_, resolved);
        configuredTheme_ = resolved;
        if (resolved != theme_) {
            theme_ = resolved;
            didChange = true;
        }
    } catch (const std::exception &error) {
        errorMessage = QString::fromUtf8(error.what());
    }
    lock.unlock();

    if (!errorMessage.isEmpty()) {
        if (lastError_ != errorMessage) {
            lastError_ = errorMessage;
            emit rejected(errorMessage);
        }
    } else {
        lastError_.clear();
    }
    updateWatches();
    if (didChange)
        emit changed();
}

}
