#include "cli.h"
#include "palette/theme.h"
#include "palette/theme_source.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QTextStream>
#include <memory>
#include <stdexcept>
#include <utility>

namespace mpvideo {
namespace {

struct Options {
    QString configDirectory;
    QStringList command;
    bool dryRun = false;
};

[[noreturn]] void fail(const QString &message) { throw std::runtime_error(message.toStdString()); }

QString expand(QString path) {
    if (path.startsWith("~/"))
        path = QDir::homePath() + path.mid(1);
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

Options parseOptions(const QStringList &arguments) {
    Options options;
    const QString defaultPath = defaultConfigPath();
    options.configDirectory = QFileInfo(defaultPath).absolutePath();
    if (arguments.isEmpty() || arguments.first() != "theme")
        fail("Expected a theme command; see --help");

    for (qsizetype index = 1; index < arguments.size(); ++index) {
        const QString arg = arguments.at(index);
        if (arg == "--config-dir") {
            if (++index >= arguments.size() || arguments.at(index).isEmpty())
                fail("--config-dir requires a directory");
            options.configDirectory = expand(arguments.at(index));
        } else if (arg == "--dry-run") {
            options.dryRun = true;
        } else if (arg.startsWith("--")) {
            fail("Unknown option: " + arg);
        } else {
            options.command.append(arg);
        }
    }
    if (options.command.isEmpty())
        fail("Expected a theme operation; see --help");
    options.configDirectory = expand(options.configDirectory);
    return options;
}

Theme readCurrent(const QString &path) {
    if (!QFileInfo::exists(path))
        return defaultTheme();
    Theme theme;
    QString error;
    if (!loadTheme(path, &theme, &error))
        fail(error);
    return theme;
}

Theme assignedTheme(Theme theme, const QStringList &assignments) {
    if (assignments.isEmpty())
        fail("Provide a preset name or at least one COLOR=#RRGGBB assignment");
    QJsonObject data = theme.toJson();
    QJsonObject colors = data.value("colors").toObject();
    for (const QString &assignment : assignments) {
        const qsizetype separator = assignment.indexOf('=');
        const QString key = assignment.left(separator);
        if (separator < 1 || !colorKeys().contains(key))
            fail("Expected COLOR=#RRGGBB. Colors: " + colorKeys().join(", "));
        colors.insert(key, assignment.mid(separator + 1));
    }
    data.insert("version", 1);
    data.insert("name", "Custom");
    data.insert("colors", colors);
    data.remove("source");
    Theme next;
    QString error;
    if (!parseTheme(data, &next, &error))
        fail(error);
    return next;
}

QString lockFilePath(const QString &configPath) {
    return QDir(QFileInfo(configPath).absolutePath()).filePath(".locks/theme.lock");
}

}

void printCliHelp() {
    QTextStream(stdout)
        << "MpVideo 1.0.0\n"
        << "Usage: mpvideo [FILE|URL ...]\n"
        << "       mpvideo [--config-dir DIR] [--mpv-option NAME=VALUE] [--quit-after MS] [--screenshot "
           "FILE] [FILE|URL ...]\n"
        << "       mpvideo theme [--config-dir DIR] OPERATION [--dry-run]\n"
        << "       mpvideo --help | --version\n\n"
        << "Theme operations:\n"
        << "  show                         Print the saved palette\n"
        << "  set PRESET                   Select dark or light\n"
        << "  set COLOR=#RRGGBB [...]      Set palette colors\n"
        << "  import FILE                  Import a QML or JSON palette once\n"
        << "  follow active                Follow the active shared palette\n"
        << "  follow static NAME           Follow a named palette\n"
        << "  sync                         Refresh the followed palette\n"
        << "  reset                        Restore the built-in dark palette\n\n"
        << "Colors: background, surface, surface_alt, text, muted, accent, accent_text, border\n"
        << "Formats: #RRGGBB, #AARRGGBB, Qt.rgba(r, g, b, a)\n"
        << "--dry-run validates and prints a proposed update without writing files.\n";
}

int runCli(const QStringList &arguments) {
    try {
        if (arguments == QStringList{"--help"} || arguments == QStringList{"-h"}) {
            printCliHelp();
            return 0;
        }
        if (arguments == QStringList{"--version"} || arguments == QStringList{"theme", "--version"}) {
            QTextStream(stdout) << "MpVideo 1.0.0\n";
            return 0;
        }
        if (arguments == QStringList{"theme", "--help"} || arguments == QStringList{"theme", "-h"}) {
            printCliHelp();
            return 0;
        }
        const Options options = parseOptions(arguments);
        const QString configPath = QDir(options.configDirectory).filePath("theme.json");
        const QStringList args = options.command;
        const QString action = args.first();
        std::unique_ptr<QLockFile> lock;
        if (action != "show" && !options.dryRun) {
            const QString lockPath = lockFilePath(configPath);
            if (!QDir().mkpath(QFileInfo(lockPath).absolutePath()))
                fail("Cannot create theme lock directory");
            lock = std::make_unique<QLockFile>(lockPath);
            if (!lock->tryLock(2000))
                fail("Theme is being edited by another process");
        }
        Theme theme;
        QString error;

        if (action == "show" && args.size() == 1) {
            theme = readCurrent(configPath);
            if (!theme.sourceKind.isEmpty()) {
                Theme refreshed = theme;
                if (refreshFollowedTheme(&refreshed, &error))
                    theme = std::move(refreshed);
            }
            QTextStream(stdout) << QJsonDocument(theme.toJson()).toJson(QJsonDocument::Indented);
            return 0;
        }
        if (action == "reset" && args.size() == 1) {
            theme = defaultTheme();
        } else if (action == "import" && args.size() == 2) {
            if (!importTheme(args.at(1), &theme, &error))
                fail(error);
        } else if (action == "follow" && args.size() == 2 && args.at(1) == "active") {
            if (!followActiveTheme(&theme, &error))
                fail(error);
        } else if (action == "follow" && args.size() == 3 && args.at(1) == "static") {
            if (!followStaticTheme(args.at(2), &theme, &error))
                fail(error);
        } else if (action == "sync" && args.size() == 1) {
            theme = readCurrent(configPath);
            if (!refreshFollowedTheme(&theme, &error))
                fail(error);
        } else if (action == "set" && args.size() >= 2) {
            theme = readCurrent(configPath);
            if (args.size() == 2 && presetNames().contains(args.at(1)))
                theme = presetTheme(args.at(1));
            else
                theme = assignedTheme(std::move(theme), args.mid(1));
        } else {
            fail("Invalid theme operation; see --help");
        }

        if (options.dryRun) {
            QTextStream(stdout) << QJsonDocument(theme.toJson()).toJson(QJsonDocument::Indented);
            return 0;
        }

        if (!saveTheme(configPath, theme, &error))
            fail(error);
        QTextStream(stdout) << "Theme saved to " << configPath << '\n';
        return 0;
    } catch (const std::exception &exception) {
        QTextStream(stderr) << "mpvideo: " << exception.what() << '\n';
        return 2;
    }
}

}
