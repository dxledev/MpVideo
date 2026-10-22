#pragma once

#include <QColor>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>

namespace mpvideo {

struct Theme {
    QString name;
    QMap<QString, QColor> colors;
    QString sourceKind;
    QString sourcePath;

    QColor color(const QString &key) const;
    QJsonObject toJson() const;
    bool operator==(const Theme &) const = default;
};

QStringList themeColorKeys();
QStringList presetNames();
Theme defaultTheme();
Theme presetTheme(const QString &name);
Theme parseTheme(const QJsonObject &data);
Theme loadTheme(const QString &path);
void saveTheme(const QString &path, const Theme &theme);
Theme setThemeColors(Theme theme, const QStringList &assignments);

}
