#include "theme.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <cmath>
#include <functional>
#include <stdexcept>

namespace mpvideo {
namespace {

constexpr qint64 maximumThemeSize = 1024 * 1024;
const QStringList requiredColors{
    "background", "surface", "surface_alt", "text", "muted", "accent", "accent_text", "border"
};

[[noreturn]] void fail(const QString &message) {
    throw std::invalid_argument(message.toStdString());
}

void setError(QString *error, const QString &message) {
    if (error)
        *error = message;
}

template <typename Operation>
bool perform(QString *error, Operation operation) {
    try {
        operation();
        if (error)
            error->clear();
        return true;
    } catch (const std::exception &exception) {
        setError(error, QString::fromUtf8(exception.what()));
        return false;
    }
}

QColor parseColor(const QString &value) {
    static const QRegularExpression pattern(R"(^#(?:[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$)");
    if (!pattern.match(value).hasMatch())
        fail("Expected a color in #RRGGBB or #AARRGGBB format: " + value);
    const QColor color = QColor::fromString(value);
    if (!color.isValid())
        fail("Invalid color: " + value);
    return color;
}

QJsonObject readObject(const QString &path) {
    QFile file(path);
    if (!QFileInfo(path).isFile() || !file.open(QIODevice::ReadOnly))
        fail("Cannot read " + path + ": " + file.errorString());
    if (file.size() > maximumThemeSize)
        fail("Theme input exceeds 1 MiB: " + path);
    const QByteArray bytes = file.read(maximumThemeSize + 1);
    if (bytes.size() > maximumThemeSize)
        fail("Theme input exceeds 1 MiB: " + path);
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        fail("Invalid theme JSON in " + path + ": " + parseError.errorString());
    return document.object();
}

}

QStringList colorKeys() {
    return requiredColors;
}

QStringList presetNames() {
    return {"dark", "light"};
}

Theme defaultTheme() {
    return presetTheme("dark");
}

Theme presetTheme(const QString &name) {
    Theme theme;
    theme.name = name;
    if (name == "dark") {
        theme.colors = {
            {"background", QColor("#111318")}, {"surface", QColor("#191c22")},
            {"surface_alt", QColor("#232730")}, {"text", QColor("#f1f2f4")},
            {"muted", QColor("#9da4b0")}, {"accent", QColor("#c3a7ff")},
            {"accent_text", QColor("#211a30")}, {"border", QColor("#343944")}
        };
    } else if (name == "light") {
        theme.colors = {
            {"background", QColor("#f4f3f1")}, {"surface", QColor("#ffffff")},
            {"surface_alt", QColor("#e9e7e4")}, {"text", QColor("#25262a")},
            {"muted", QColor("#686b73")}, {"accent", QColor("#6941c6")},
            {"accent_text", QColor("#ffffff")}, {"border", QColor("#d2cfd0")}
        };
    } else {
        fail("Unknown preset: " + name + ". Available: " + presetNames().join(", "));
    }
    return theme;
}

QJsonObject Theme::toJson() const {
    QJsonObject colorsJson;
    for (const QString &key : requiredColors)
        colorsJson.insert(key, colors.value(key).name(QColor::HexArgb));
    QJsonObject result{{"version", 1}, {"name", name}, {"colors", colorsJson}};
    if (!sourceKind.isEmpty())
        result.insert("source", QJsonObject{{"kind", sourceKind}, {"path", sourcePath}});
    return result;
}

bool parseTheme(const QJsonObject &data, Theme *theme, QString *error) {
    return perform(error, [&] {
        if (!theme)
            fail("Theme output cannot be null");
        if (data.value("version").toInt(-1) != 1)
            fail("Theme version must be 1");
        const QString name = data.value("name").toString().trimmed();
        if (name.isEmpty() || name.size() > 60)
            fail("Theme name must contain 1 to 60 characters");
        if (!data.value("colors").isObject())
            fail("Theme must contain a colors object");
        const QJsonObject serializedColors = data.value("colors").toObject();
        for (auto entry = serializedColors.constBegin(); entry != serializedColors.constEnd(); ++entry) {
            if (!requiredColors.contains(entry.key()))
                fail("Unknown color: " + entry.key());
        }
        Theme parsed;
        parsed.name = name;
        for (const QString &key : requiredColors) {
            const QJsonValue value = serializedColors.value(key);
            if (!value.isString())
                fail("Theme is missing color: " + key);
            parsed.colors.insert(key, parseColor(value.toString()));
        }
        if (data.contains("source")) {
            if (!data.value("source").isObject())
                fail("Theme source must be an object");
            const QJsonObject source = data.value("source").toObject();
            parsed.sourceKind = source.value("kind").toString();
            parsed.sourcePath = source.value("path").toString();
            if (!QStringList{"active", "static", "file"}.contains(parsed.sourceKind)
                || parsed.sourcePath.isEmpty() || !QFileInfo(parsed.sourcePath).isAbsolute())
                fail("Theme source needs a known kind and an absolute path");
        }
        *theme = std::move(parsed);
    });
}

bool loadTheme(const QString &path, Theme *theme, QString *error) {
    return perform(error, [&] {
        if (!theme)
            fail("Theme output cannot be null");
        Theme loaded;
        QString parseError;
        if (!parseTheme(readObject(path), &loaded, &parseError))
            fail(path + ": " + parseError);
        *theme = std::move(loaded);
    });
}

bool saveTheme(const QString &path, const Theme &theme, QString *error) {
    return perform(error, [&] {
        Theme validated;
        QString validationError;
        if (!parseTheme(theme.toJson(), &validated, &validationError))
            fail(validationError);
        const QString directory = QFileInfo(path).absolutePath();
        if (!QDir().mkpath(directory))
            fail("Cannot create theme directory: " + directory);
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            fail("Cannot write " + path + ": " + file.errorString());
        const QByteArray bytes = QJsonDocument(validated.toJson()).toJson(QJsonDocument::Indented);
        if (file.write(bytes) != bytes.size() || !file.commit())
            fail("Cannot save " + path + ": " + file.errorString());
    });
}

QString defaultConfigPath() {
    const QString override = qEnvironmentVariable("XDG_CONFIG_HOME");
    QString configHome = !override.isEmpty() && QFileInfo(override).isAbsolute()
        ? QDir::cleanPath(override)
        : QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (configHome.isEmpty())
        configHome = QDir::homePath() + "/.config";
    return QDir(configHome).filePath("mpvideo/theme.json");
}

}
