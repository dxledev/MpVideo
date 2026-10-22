#include "theme_source.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStandardPaths>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace mpvideo {
namespace {

constexpr qint64 maximumPaletteSize = 1024 * 1024;
using Palette = QMap<QString, QColor>;

[[noreturn]] void fail(const QString &message) { throw std::invalid_argument(message.toStdString()); }

void setError(QString *error, const QString &message) {
    if (error)
        *error = message;
}

template <typename Operation> bool perform(QString *error, Operation operation) {
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
    if (bytes.size() > maximumPaletteSize || file.error() != QFileDevice::NoError)
        fail("Palette input exceeds 1 MiB or cannot be read: " + path);
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
    QColor color;
    QString error;
    if (!parsePaletteColor(value, &color, &error))
        fail("Unsupported palette color: " + error);
    return color;
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
        const QString quoted = !match.captured(2).isEmpty() ? match.captured(2) : match.captured(3);
        const QString value = quoted.isEmpty() ? "Qt.rgba(" + match.captured(4) + ")" : quoted;
        palette.insert(match.captured(1), externalColor(value));
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
        if (value.value().isString())
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
    for (const QString &alias : aliases) {
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
    const auto linear = [](double value) {
        return value <= .04045 ? value / 12.92 : std::pow((value + .055) / 1.055, 2.4);
    };
    return .2126 * linear(color.redF()) + .7152 * linear(color.greenF()) + .0722 * linear(color.blueF());
}

Theme mappedTheme(const Palette &palette, const QString &name) {
    const QColor background =
        pick(palette, {"background", "base", "m3background", "mBackground", "mSurface", "surface"});
    const QColor text = pick(palette, {"foreground", "onBackground", "text", "mOnSurface", "onSurface",
                                       "on_surface", "m3onSurface"});
    const QColor accent = pick(palette, {"primary", "accent", "mPrimary", "m3primary"});
    const QColor surface =
        pick(palette, {"surface_container_low", "surfaceContainerLow", "backgroundAlt", "mantle", "surface0"},
             mix(background, text, .035));
    const QColor surfaceAlt = pick(
        palette, {"surface_container", "surfaceContainer", "backgroundGray", "surface1", "mSurfaceVariant"},
        mix(background, text, .075));
    const QColor border = pick(palette, {"outline", "border", "mOutline"}, mix(surface, accent, .25));
    const QColor muted =
        pick(palette,
             {"foregroundInactive", "on_surface_variant", "onSurfaceVariant", "mOnSurfaceVariant", "muted"},
             mix(background, text, .62));
    const QColor accentText =
        pick(palette, {"on_primary", "onPrimary", "mOnPrimary", "onAccent", "accentText"},
             luminance(accent) > .42 ? QColor("#181818") : QColor("#ffffff"));
    Theme theme;
    theme.name = name;
    theme.colors = {
        {"background", background}, {"surface", surface}, {"surface_alt", surfaceAlt}, {"text", text},
        {"muted", muted},           {"accent", accent},   {"accent_text", accentText}, {"border", border}};
    return theme;
}

QString paletteFile(const QString &path) {
    const QFileInfo info(path);
    if (!info.isDir())
        return expandPath(path);
    for (const char *name : {"Colors.qml", "noctalia.json", "palette.json", "colors.json", "theme.json"}) {
        const QString candidate = QDir(path).filePath(QString::fromLatin1(name));
        if (QFileInfo(candidate).isFile() || QFileInfo(candidate).isSymLink())
            return candidate;
    }
    return QDir(path).filePath("Colors.qml");
}

QString paletteName(const QString &path) {
    const QString directory = QFileInfo(path).dir().dirName();
    return directory == ".caelestia-use" ? "Active" : directory.left(60);
}

Theme importFile(const QString &requestedPath) {
    const QString path = paletteFile(requestedPath);
    if (path.endsWith(".qml", Qt::CaseInsensitive))
        return mappedTheme(readQmlPalette(path), paletteName(path));
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(readBounded(path), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        fail("Invalid palette JSON in " + path + ": " + parseError.errorString());
    const QJsonObject data = document.object();
    const QJsonObject colors = data.value("colors").toObject();
    if (data.value("version").toInt(-1) == 1 && colors.contains("surface_alt")) {
        Theme canonical;
        QString error;
        if (!parseTheme(data, &canonical, &error))
            fail(error);
        return canonical;
    }
    return mappedTheme(readJsonPalette(path), paletteName(path));
}

}

QString activePalettePath() { return QDir(configHome()).filePath("themes/.caelestia-use/Colors.qml"); }

QString staticPalettePath(const QString &name) {
    if (name.isEmpty() || name == "." || name == ".." || name.contains('/') || name.contains('\\'))
        fail("Static theme name must be a single directory name");
    return paletteFile(QDir(configHome()).filePath("themes/" + name));
}

QStringList sourceWatchPaths(const Theme &theme) {
    if (theme.sourcePath.isEmpty())
        return {};
    QStringList paths;
    const auto collect = [&paths](QString path, int levels) {
        for (int level = 0; level < levels && !path.isEmpty(); ++level) {
            const QFileInfo info(path);
            if (info.exists() || info.isSymLink())
                paths.append(info.absoluteFilePath());
            const QString parent = info.dir().absolutePath();
            if (parent == path)
                break;
            path = parent;
        }
    };
    collect(theme.sourcePath, 4);
    const QString canonical = QFileInfo(theme.sourcePath).canonicalFilePath();
    if (!canonical.isEmpty())
        collect(canonical, 4);
    const QString resolvedPalette = paletteFile(theme.sourcePath);
    if (resolvedPalette != theme.sourcePath)
        collect(resolvedPalette, 3);
    paths.removeDuplicates();
    return paths;
}

bool importTheme(const QString &sourcePath, Theme *theme, QString *error) {
    return perform(error, [&] {
        if (!theme)
            fail("Theme output cannot be null");
        *theme = importFile(expandPath(sourcePath));
    });
}

bool followActiveTheme(Theme *theme, QString *error) {
    return perform(error, [&] {
        if (!theme)
            fail("Theme output cannot be null");
        const QString path = activePalettePath();
        Theme followed = importFile(path);
        followed.name = "Active";
        followed.sourceKind = "active";
        followed.sourcePath = path;
        *theme = std::move(followed);
    });
}

bool followStaticTheme(const QString &name, Theme *theme, QString *error) {
    return perform(error, [&] {
        if (!theme)
            fail("Theme output cannot be null");
        const QString path = staticPalettePath(name);
        Theme followed = importFile(path);
        followed.name = name;
        followed.sourceKind = "static";
        followed.sourcePath = path;
        *theme = std::move(followed);
    });
}

bool refreshFollowedTheme(Theme *theme, QString *error) {
    return perform(error, [&] {
        if (!theme)
            fail("Theme output cannot be null");
        const QString kind = theme->sourceKind;
        const QString path = theme->sourcePath;
        const QString name = theme->name;
        if (kind != "active" && kind != "static" && kind != "file")
            fail("No theme source is being followed");
        Theme refreshed = importFile(path);
        refreshed.name = name;
        refreshed.sourceKind = kind;
        refreshed.sourcePath = path;
        *theme = std::move(refreshed);
    });
}

}
