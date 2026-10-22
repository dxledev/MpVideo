#include "theme.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>
#include <stdexcept>

namespace mpvideo {
namespace {

const QStringList keys{
    "background", "surface", "surface_alt", "text", "muted", "accent",
    "accent_text", "border", "selection", "danger", "progress", "control",
    "control_hover", "video_background"
};

[[noreturn]] void fail(const QString &message) {
    throw std::invalid_argument(message.toStdString());
}

QColor parseColor(QString value) {
    value = value.trimmed();
    static const QRegularExpression valid(R"(^#?(?:[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$)");
    if (!valid.match(value).hasMatch())
        fail("Expected #RRGGBB or alpha-first #AARRGGBB: " + value);
    if (!value.startsWith('#'))
        value.prepend('#');
    const QColor color = QColor::fromString(value);
    if (!color.isValid())
        fail("Invalid theme color: " + value);
    return color;
}

QString colorString(const QColor &color) {
    if (!color.isValid())
        fail("Theme contains an invalid color");
    return color.alpha() == 255 ? color.name(QColor::HexRgb) : color.name(QColor::HexArgb);
}

QColor mix(const QColor &base, const QColor &tint, double amount) {
    return QColor::fromRgbF(base.redF() * (1.0 - amount) + tint.redF() * amount,
                            base.greenF() * (1.0 - amount) + tint.greenF() * amount,
                            base.blueF() * (1.0 - amount) + tint.blueF() * amount,
                            base.alphaF() * (1.0 - amount) + tint.alphaF() * amount);
}

QJsonObject readObject(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        fail(path + ": " + file.errorString());
    if (file.size() > 1024 * 1024)
        fail(path + ": theme file exceeds 1 MiB");
    const auto bytes = file.read(1024 * 1024 + 1);
    if (bytes.size() > 1024 * 1024 || file.error() != QFileDevice::NoError)
        fail(path + ": cannot read theme file within the 1 MiB limit");
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        fail(path + ": expected a valid JSON object (" + error.errorString() + ")");
    return document.object();
}

}

QColor Theme::color(const QString &key) const {
    return colors.value(key);
}

QJsonObject Theme::toJson() const {
    QJsonObject palette;
    for (const auto &key : keys)
        palette.insert(key, colorString(color(key)));
    QJsonObject result{{"version", 1}, {"name", name}, {"colors", palette}};
    if (!sourceKind.isEmpty())
        result.insert("source", QJsonObject{{"kind", sourceKind}, {"path", sourcePath}});
    return result;
}

QStringList themeColorKeys() {
    return keys;
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
        theme.colors.insert("danger", QColor("#ef6b73"));
        theme.colors.insert("video_background", QColor("#000000"));
    } else if (name == "light") {
        theme.colors = {
            {"background", QColor("#f4f3f1")}, {"surface", QColor("#ffffff")},
            {"surface_alt", QColor("#e9e7e4")}, {"text", QColor("#25262a")},
            {"muted", QColor("#686b73")}, {"accent", QColor("#6941c6")},
            {"accent_text", QColor("#ffffff")}, {"border", QColor("#d2cfd0")}
        };
        theme.colors.insert("danger", QColor("#b42318"));
        theme.colors.insert("video_background", QColor("#000000"));
    } else {
        fail("Unknown preset: " + name + ". Available: " + presetNames().join(", "));
    }
    const auto surface = theme.colors.value("surface");
    const auto alternate = theme.colors.value("surface_alt");
    const auto accent = theme.colors.value("accent");
    theme.colors.insert("selection", mix(surface, accent, .28));
    theme.colors.insert("progress", accent);
    theme.colors.insert("control", surface);
    theme.colors.insert("control_hover", alternate);
    return theme;
}

Theme parseTheme(const QJsonObject &data) {
    if (data.value("version").toInt(-1) != 1)
        fail("Theme version must be 1");
    const QString name = data.value("name").toString().trimmed();
    if (name.isEmpty() || name.size() > 60)
        fail("Theme name must contain 1–60 characters");
    if (!data.value("colors").isObject())
        fail("Theme must contain a colors object");

    Theme theme;
    if (presetNames().contains(name))
        theme = presetTheme(name);
    else
        theme = defaultTheme();
    theme.name = name;

    const auto palette = data.value("colors").toObject();
    for (auto entry = palette.constBegin(); entry != palette.constEnd(); ++entry) {
        if (!keys.contains(entry.key()))
            fail("Unknown theme color: " + entry.key());
        if (!entry.value().isString())
            fail(entry.key() + ": expected a color string");
        theme.colors.insert(entry.key(), parseColor(entry.value().toString()));
    }

    if (data.contains("source")) {
        if (!data.value("source").isObject())
            fail("Theme source must be an object");
        const auto source = data.value("source").toObject();
        theme.sourceKind = source.value("kind").toString();
        theme.sourcePath = source.value("path").toString();
        if (!QStringList{"active", "static", "caelestia", "noctalia", "file"}.contains(theme.sourceKind)
            || theme.sourcePath.isEmpty() || !QFileInfo(theme.sourcePath).isAbsolute())
            fail("Theme source needs a supported kind and an absolute path");
    }
    return theme;
}

Theme loadTheme(const QString &path) {
    return parseTheme(readObject(path));
}

void saveTheme(const QString &path, const Theme &theme) {
    const auto validated = parseTheme(theme.toJson());
    const auto directory = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(directory))
        fail("Cannot create " + directory);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        fail(path + ": " + file.errorString());
    const auto bytes = QJsonDocument(validated.toJson()).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit())
        fail(path + ": " + file.errorString());
}

Theme setThemeColors(Theme theme, const QStringList &assignments) {
    if (assignments.isEmpty())
        fail("Provide at least one COLOR=VALUE assignment");
    auto data = theme.toJson();
    auto palette = data.value("colors").toObject();
    for (const auto &assignment : assignments) {
        const auto separator = assignment.indexOf('=');
        if (separator < 1)
            fail("Expected COLOR=VALUE. Colors: " + keys.join(", "));
        const auto key = assignment.left(separator);
        if (!keys.contains(key))
            fail("Unknown color: " + key);
        palette.insert(key, assignment.mid(separator + 1));
    }
    data.insert("name", "custom");
    data.remove("source");
    data.insert("colors", palette);
    return parseTheme(data);
}

}
