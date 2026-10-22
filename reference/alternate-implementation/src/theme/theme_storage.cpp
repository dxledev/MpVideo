#include "theme_storage.hpp"

#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QStandardPaths>
#include <stdexcept>

namespace mpvideo {
namespace {

[[noreturn]] void fail(const QString &message) {
    throw std::invalid_argument(message.toStdString());
}

QString expandedPath(QString path) {
    if (path == "~")
        path = QDir::homePath();
    else if (path.startsWith("~/"))
        path = QDir::homePath() + path.mid(1);
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

}

QString defaultConfigDirectory() {
    const QString configured = qEnvironmentVariable("MPVIDEO_CONFIG_DIR");
    if (!configured.isEmpty())
        return expandedPath(configured);
    QString configHome = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (configHome.isEmpty())
        configHome = QDir::homePath() + "/.config";
    return QDir::cleanPath(configHome + "/mpvideo");
}

QString configDirectory(const QStringList &arguments) {
    QString configured;
    for (qsizetype index = 0; index < arguments.size(); ++index) {
        if (arguments[index] != "--config-dir")
            continue;
        if (index + 1 >= arguments.size() || arguments[index + 1].startsWith('-'))
            fail("--config-dir requires a directory");
        configured = arguments[++index];
        if (configured.trimmed().isEmpty())
            fail("--config-dir requires a directory");
    }
    return configured.isEmpty() ? defaultConfigDirectory() : expandedPath(configured);
}

QString themeFilePath(const QString &directory) {
    return QDir::cleanPath(QDir(directory).filePath("theme.json"));
}

QString themeLockPath(const QString &outputPath) {
    return QDir::cleanPath(QDir(QFileInfo(outputPath).absolutePath()).filePath(".locks/theme.lock"));
}

Theme updateThemeLocked(const QString &path, const std::function<Theme(const Theme &)> &update,
                        int timeoutMs) {
    const auto lockPath = themeLockPath(path);
    if (!QDir().mkpath(QFileInfo(lockPath).absolutePath()))
        fail("Cannot create theme lock directory");
    QLockFile lock(lockPath);
    if (!lock.tryLock(timeoutMs))
        fail("Theme is being edited by another process");
    const Theme current = QFileInfo::exists(path) ? loadTheme(path) : defaultTheme();
    const Theme next = update(current);
    saveTheme(path, next);
    return next;
}

}
