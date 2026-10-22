#pragma once

#include <QColor>
#include <QJsonObject>
#include <QMap>
#include <QStringList>

namespace mpvideo {

struct Theme {
    QString name;
    QMap<QString, QColor> colors;
    QString sourceKind;
    QString sourcePath;

    QColor color(const QString &key) const { return colors.value(key); }
    QJsonObject toJson() const;
    bool operator==(const Theme &) const = default;
};

QStringList colorKeys();
QStringList presetNames();
Theme defaultTheme();
Theme presetTheme(const QString &name);
bool parseTheme(const QJsonObject &data, Theme *theme, QString *error = nullptr);
bool loadTheme(const QString &path, Theme *theme, QString *error = nullptr);
bool saveTheme(const QString &path, const Theme &theme, QString *error = nullptr);
bool importTheme(const QString &sourcePath, Theme *theme, QString *error = nullptr);
bool followActiveTheme(Theme *theme, QString *error = nullptr);
bool followStaticTheme(const QString &name, Theme *theme, QString *error = nullptr);
bool refreshFollowedTheme(Theme *theme, QString *error = nullptr);
QString defaultConfigPath();

}
