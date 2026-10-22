#include "theme_source.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStandardPaths>
#include <cmath>
#include <stdexcept>

namespace mpvideo {
namespace {

constexpr qint64 maximumPaletteSize = 1024 * 1024;
using Palette = QMap<QString, QColor>;

[[noreturn]] void fail(const QString &message) {
    throw std::invalid_argument(message.toStdString());
}

template <typename Operation>
bool perform(QString *error, Operation &&operation) {
    try {
        operation();
        if (error)
            error->clear();
        return true;
    } catch (const std::exception &exception) {
        if (error)
            *error = QString::fromUtf8(exception.what());
        return false;
    }
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

QString configHome() {
    const QString override = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (!override.isEmpty() && QFileInfo(override).isAbsolute())
        return QDir::cleanPath(override);
    QString path = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (path.isEmpty())
        path = QDir::homePath() + "/.config";
    return path;
}

QString expandPath(QString path) {
    if (path == "~")
        path = QDir::homePath();
    else if (path.startsWith("~/"))
        path = QDir::homePath() + path.mid(1);
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QByteArray readBounded(const QString &path) {
    QFile file(path);
    if (!QFileInfo(path).isFile() || !file.open(QIODevice::ReadOnly))
        fail("Cannot read palette: " + path);
    if (file.size() > maximumPaletteSize)
        fail("Palette input exceeds 1 MiB: " + path);
    const QByteArray bytes = file.read(maximumPaletteSize + 1);
    if (bytes.size() > maximumPaletteSize)
        fail("Palette input exceeds 1 MiB: " + path);
    if (file.error() != QFileDevice::NoError)
        fail("Cannot read palette: " + path);
    return bytes;
}

QString normalized(QString key) {
    key = key.toLower();
    key.remove(QRegularExpression("[^a-z0-9]"));
    return key;
}

QColor externalColor(QString value) {
    value = value.trimmed();
    static const QRegularExpression bare(R"(^[0-9a-fA-F]{6}(?:[0-9a-fA-F]{2})?$)");
    if (bare.match(value).hasMatch())
        value.prepend('#');
    static const QRegularExpression valid(R"(^#[0-9a-fA-F]{6}(?:[0-9a-fA-F]{2})?$)");
    if (!valid.match(value).hasMatch())
        fail("Unsupported palette color: " + value);
    const QColor color = QColor::fromString(value);
    if (!color.isValid())
        fail("Unsupported palette color: " + value);
    return color;
}

double channel(QString value) {
    const QStringList parts = value.trimmed().split('/');
    if (parts.size() > 2)
        fail("Unsupported Qt.rgba channel: " + value);
    bool valid = false;
    double result = parts.at(0).trimmed().toDouble(&valid);
    if (!valid)
        fail("Invalid Qt.rgba channel: " + value);
    if (parts.size() == 2) {
        bool denominatorValid = false;
        const double denominator = parts.at(1).trimmed().toDouble(&denominatorValid);
        if (!denominatorValid || denominator <= 0)
            fail("Invalid Qt.rgba divisor: " + value);
        result /= denominator;
    }
    if (!std::isfinite(result) || result < 0 || result > 1)
        fail("Qt.rgba channels must be between 0 and 1");
    return result;
}

QColor rgba(const QString &expression) {
    const QStringList channels = expression.split(',');
    if (channels.size() != 4)
        fail("Qt.rgba needs four channels");
    return QColor::fromRgbF(channel(channels.at(0)), channel(channels.at(1)),
                            channel(channels.at(2)), channel(channels.at(3)));
}

Palette readQmlPalette(const QString &path) {
    QString contents = QString::fromUtf8(readBounded(path));
    contents.remove(QRegularExpression("/\\*.*?\\*/", QRegularExpression::DotMatchesEverythingOption));
    contents.remove(QRegularExpression("//[^\\n]*"));
    static const QRegularExpression property(
        R"REGEX(property\s+color\s+(\w+)\s*:\s*(?:"(#[\da-fA-F]{6}(?:[\da-fA-F]{2})?)"|'(#[\da-fA-F]{6}(?:[\da-fA-F]{2})?)'|Qt\.rgba\(([^)]*)\)))REGEX");
    Palette palette;
    auto matches = property.globalMatch(contents);
    while (matches.hasNext()) {
        const auto match = matches.next();
        const QString name = match.captured(1);
        const QString quoted = !match.captured(2).isEmpty() ? match.captured(2) : match.captured(3);
        palette.insert(name, quoted.isEmpty() ? rgba(match.captured(4)) : externalColor(quoted));
    }
    if (palette.isEmpty())
        fail("No supported color properties in " + path);
    return palette;
}

QJsonObject paletteObject(QJsonObject data) {
    for (int depth = 0; depth < 4; ++depth) {
        if (data.value("colors").isObject()) {
            data = data.value("colors").toObject();
            continue;
        }
        if (data.value("colours").isObject()) {
            data = data.value("colours").toObject();
            continue;
        }
        if (data.value("palette").isObject()) {
            data = data.value("palette").toObject();
            continue;
        }
        if (data.value("dark").isObject() || data.value("light").isObject()) {
            QString mode = data.value("mode").toString("dark").toLower();
            if (mode != "dark" && mode != "light")
                mode = "dark";
            if (!data.value(mode).isObject())
                mode = mode == "dark" ? "light" : "dark";
            data = data.value(mode).toObject();
            continue;
        }
        break;
    }
    return data;
}

Palette readJsonPalette(const QString &path) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(readBounded(path), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        fail("Invalid palette JSON in " + path + ": " + parseError.errorString());
    const QJsonObject values = paletteObject(document.object());
    Palette palette;
    for (auto value = values.constBegin(); value != values.constEnd(); ++value) {
        if (!value.value().isString())
            continue;
        palette.insert(value.key(), externalColor(value.value().toString()));
    }
    if (palette.isEmpty())
        fail("No supported colors in palette JSON: " + path);
    return palette;
}

QColor pick(const Palette &palette, const QStringList &aliases, const QColor &fallback = {}) {
    QMap<QString, QColor> keys;
    for (auto entry = palette.constBegin(); entry != palette.constEnd(); ++entry)
        keys.insert(normalized(entry.key()), entry.value());
    for (const auto &alias : aliases) {
        const QString key = normalized(alias);
        if (keys.contains(key))
            return keys.value(key);
    }
    if (fallback.isValid())
        return fallback;
    fail("Palette is missing " + aliases.join(" / "));
}

QColor mix(const QColor &base, const QColor &tint, double amount) {
    return QColor::fromRgbF(base.redF() * (1.0 - amount) + tint.redF() * amount,
                            base.greenF() * (1.0 - amount) + tint.greenF() * amount,
                            base.blueF() * (1.0 - amount) + tint.blueF() * amount,
                            base.alphaF() * (1.0 - amount) + tint.alphaF() * amount);
}

double luminance(const QColor &color) {
    const auto linear = [](double channelValue) {
        return channelValue <= .04045 ? channelValue / 12.92
                                      : std::pow((channelValue + .055) / 1.055, 2.4);
    };
    return .2126 * linear(color.redF()) + .7152 * linear(color.greenF()) + .0722 * linear(color.blueF());
}

Theme mappedTheme(const Palette &palette, const QString &name) {
    const QColor background = pick(palette, {"background", "base", "m3background", "mBackground", "mSurface", "surface"});
    const QColor text = pick(palette, {"foreground", "onBackground", "text", "mOnSurface", "onSurface", "on_surface", "m3onSurface"});
    const QColor accent = pick(palette, {"primary", "accent", "mPrimary", "m3primary"});
    const QColor surface = pick(palette, {"surface_container_low", "surfaceContainerLow", "backgroundAlt", "mantle", "surface0"},
                                mix(background, text, .035));
    const QColor surfaceAlt = pick(palette, {"surface_container", "surfaceContainer", "backgroundGray", "surface1", "mSurfaceVariant"},
                                   mix(background, text, .075));
    const QColor border = pick(palette, {"outline", "border", "mOutline"}, mix(surface, accent, .25));
    const QColor muted = pick(palette, {"foregroundInactive", "on_surface_variant", "onSurfaceVariant", "mOnSurfaceVariant", "muted"},
                              mix(background, text, .62));
    const QColor accentText = pick(palette, {"on_primary", "onPrimary", "mOnPrimary", "onAccent", "accentText"},
                                   luminance(accent) > .42 ? QColor("#181818") : QColor("#ffffff"));
    Theme theme;
    theme.name = name;
    theme.colors = {
        {"background", background}, {"surface", surface}, {"surface_alt", surfaceAlt},
        {"text", text}, {"muted", muted}, {"accent", accent},
        {"accent_text", accentText}, {"border", border},
        {"selection", pick(palette, {"selection", "selected", "highlight", "selectionBg"},
                           mix(surface, accent, .28))},
        {"danger", pick(palette, {"danger", "error", "mError", "red", "color1"}, QColor("#ef6b73"))},
        {"progress", pick(palette, {"progress", "primary", "accent", "mPrimary"}, accent)},
        {"control", pick(palette, {"control", "surface", "mSurface"}, surface)},
        {"control_hover", pick(palette, {"controlHover", "hover", "mHover"}, surfaceAlt)},
        {"video_background", pick(palette, {"videoBackground", "video_background"}, QColor("#000000"))}
    };
    return theme;
}

QString paletteFile(const QString &path) {
    const QFileInfo info(path);
    if (!info.isDir())
        return expandPath(path);
    for (const QString &name : {"Colors.qml", "noctalia.json", "palette.json", "colors.json", "theme.json"}) {
        const QString candidate = QDir(path).filePath(name);
        if (QFileInfo(candidate).isFile() || QFileInfo(candidate).isSymLink())
            return candidate;
    }
    return QDir(path).filePath("Colors.qml");
}

QString themeName(const QString &path) {
    const QString directory = QFileInfo(path).dir().dirName();
    return directory == ".caelestia-use" ? "Active" : directory.left(60);
}

Theme importPaletteFile(const QString &requestedPath) {
    const QString path = paletteFile(requestedPath);
    if (path.endsWith(".qml", Qt::CaseInsensitive))
        return mappedTheme(readQmlPalette(path), themeName(path));
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(readBounded(path), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        fail("Invalid palette JSON in " + path + ": " + parseError.errorString());
    const QJsonObject data = document.object();
    if (data.value("version").toInt(-1) == 1 && data.value("colors").isObject()) {
        return parseTheme(data);
    }
    return mappedTheme(readJsonPalette(path), themeName(path));
}

}

QString sharedThemesDirectory() {
    const QString overridePath = qEnvironmentVariable("MPVIDEO_THEMES_DIR");
    if (!overridePath.isEmpty())
        return expandPath(overridePath);
    return QDir(configHome()).filePath("themes");
}

Theme importPalette(const QString &sourcePath, const QString &name) {
    Theme theme = importPaletteFile(expandPath(sourcePath));
    if (!name.isEmpty())
        theme.name = name;
    theme.sourceKind.clear();
    theme.sourcePath.clear();
    return theme;
}

Theme followTheme(const QString &kind, const QString &requestedPath) {
    const QString themes = sharedThemesDirectory();
    QString binding;
    if (kind == "active") {
        binding = expandPath(requestedPath.isEmpty()
            ? QDir(themes).filePath(".caelestia-use") : requestedPath);
    } else if (kind == "static") {
        if (requestedPath.isEmpty())
            fail("static source needs a theme name");
        QString requested = requestedPath;
        if (!QFileInfo(requested).isAbsolute()
            && !requested.startsWith("~/") && !requested.startsWith("./")
            && !requested.startsWith("../")) {
            if (requested == "current" || requested.startsWith('.')
                || requested.contains('/') || requested.contains('\\'))
                fail("Invalid static theme name: " + requested);
            requested = QDir(themes).filePath(requested);
        }
        binding = expandPath(requested);
    } else if (kind == "caelestia" || kind == "noctalia") {
        binding = expandPath(requestedPath.isEmpty()
            ? QDir(themes).filePath(".dynamic/" + kind) : requestedPath);
    } else if (kind == "file") {
        if (requestedPath.isEmpty())
            fail("file source needs a path");
        binding = expandPath(requestedPath);
    } else {
        fail("Unknown source. Use active or static NAME.");
    }

    Theme theme = importPaletteFile(binding);
    const QString resolved = QFileInfo(binding).canonicalFilePath();
    if (kind == "static")
        theme.name = QFileInfo(binding).fileName();
    else if (kind == "active") {
        const QString directoryName = QFileInfo(resolved).fileName();
        theme.name = directoryName == "noctalia" || directoryName == "caelestia"
            ? directoryName : directoryName;
    }
    theme.sourceKind = kind;
    theme.sourcePath = binding;
    return theme;
}

Theme refreshSource(const Theme &theme) {
    if (theme.sourceKind.isEmpty())
        return theme;
    return followTheme(theme.sourceKind, theme.sourcePath);
}

QStringList sourceWatchPaths(const Theme &theme) {
    if (theme.sourceKind.isEmpty() || theme.sourcePath.isEmpty())
        return {};
    QStringList paths;
    const auto collect = [&paths](QString path, int levels) {
        for (int level = 0; level < levels && !path.isEmpty(); ++level) {
            const QFileInfo info(path);
            if (info.exists() || info.isSymLink())
                paths.append(info.absoluteFilePath());
            const QString parent = info.absoluteDir().absolutePath();
            if (parent == path)
                break;
            path = parent;
        }
    };
    collect(theme.sourcePath, 5);
    const QString canonical = QFileInfo(theme.sourcePath).canonicalFilePath();
    if (!canonical.isEmpty())
        collect(canonical, 5);
    const QString selectedPalette = paletteFile(theme.sourcePath);
    collect(selectedPalette, 3);
    paths.removeDuplicates();
    return paths;
}

bool sourceTargetsFile(const Theme &theme, const QString &path) {
    const auto comparable = [](const QString &candidate) {
        const QFileInfo info(candidate);
        const QString canonical = info.canonicalFilePath();
        return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
    };
    const QString target = comparable(path);
    const QFileInfo source(theme.sourcePath);
    return (source.isFile() || source.isSymLink()) && comparable(theme.sourcePath) == target;
}

}
