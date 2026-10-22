#pragma once

#include "theme.h"

#include <QStringList>

namespace mpvideo {

QString activePalettePath();
QString staticPalettePath(const QString &name);
QStringList sourceWatchPaths(const Theme &theme);

}
