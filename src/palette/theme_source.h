#pragma once

#include "theme.h"

#include <QStringList>

namespace mpvideo {

QString activePalettePath();
QString staticPalettePath(const QString &name);
QStringList sourceWatchPaths(const Theme &theme);
bool importTheme(const QString &sourcePath, Theme *theme, QString *error = nullptr);
bool followActiveTheme(Theme *theme, QString *error = nullptr);
bool followStaticTheme(const QString &name, Theme *theme, QString *error = nullptr);
bool refreshFollowedTheme(Theme *theme, QString *error = nullptr);

}
