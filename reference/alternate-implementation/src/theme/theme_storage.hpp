#pragma once

#include "theme.hpp"

#include <QString>
#include <QStringList>
#include <functional>

namespace mpvideo {

QString defaultConfigDirectory();
QString configDirectory(const QStringList &arguments);
QString themeFilePath(const QString &configDirectory);
QString themeLockPath(const QString &outputPath);
Theme updateThemeLocked(const QString &path, const std::function<Theme(const Theme &)> &update,
                        int timeoutMs = 2000);

}
