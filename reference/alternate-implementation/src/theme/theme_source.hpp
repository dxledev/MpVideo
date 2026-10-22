#pragma once

#include "theme.hpp"

namespace mpvideo {

QString sharedThemesDirectory();
Theme importPalette(const QString &path, const QString &name = {});
Theme followTheme(const QString &kind, const QString &path = {});
Theme refreshSource(const Theme &theme);
QStringList sourceWatchPaths(const Theme &theme);
bool sourceTargetsFile(const Theme &theme, const QString &path);

}
