#include "theme_cli.hpp"

#include "theme/theme_source.hpp"
#include "theme/theme_storage.hpp"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTextStream>
#include <stdexcept>

namespace mpvideo {
namespace {

[[noreturn]] void fail(const QString &message) {
    throw std::invalid_argument(message.toStdString());
}

void requireCount(const QStringList &arguments, qsizetype count) {
    if (arguments.size() != count)
        fail("Invalid arguments. Run mpvideo theme --help for usage.");
}

Theme currentTheme(const QString &path) {
    auto theme = QFileInfo::exists(path) ? loadTheme(path) : defaultTheme();
    if (!theme.sourceKind.isEmpty())
        theme = refreshSource(theme);
    return theme;
}

Theme proposedTheme(const QString &path, const QString &command, const QStringList &arguments) {
    if (command == "reset") {
        requireCount(arguments, 0);
        return defaultTheme();
    }
    if (command == "set") {
        if (arguments.size() == 1 && presetNames().contains(arguments.first()))
            return presetTheme(arguments.first());
        return setThemeColors(currentTheme(path), arguments);
    }
    if (command == "import") {
        requireCount(arguments, 1);
        return importPalette(QFileInfo(arguments.first()).absoluteFilePath());
    }
    if (command == "follow") {
        if (arguments.size() == 1 && arguments.first() == "active")
            return followTheme("active");
        if (arguments.size() == 2 && arguments.first() == "static")
            return followTheme("static", arguments.last());
        fail("Expected theme follow active or theme follow static NAME");
    }
    if (command == "sync") {
        requireCount(arguments, 0);
        auto theme = currentTheme(path);
        if (theme.sourceKind.isEmpty())
            fail("The current theme does not follow a source");
        theme = refreshSource(theme);
        if (sourceTargetsFile(theme, path))
            fail("A theme cannot follow its own output file");
        return theme;
    }
    fail("Unknown theme command: " + command);
}

void printTheme(const Theme &theme) {
    QTextStream(stdout) << QJsonDocument(theme.toJson()).toJson(QJsonDocument::Indented);
}

void printHelp() {
    QTextStream(stdout) << R"(MpVideo theme commands

Usage:
  mpvideo theme show [--config-dir DIRECTORY]
  mpvideo theme set COLOR=VALUE [COLOR=VALUE ...] [--dry-run] [--config-dir DIRECTORY]
  mpvideo theme import FILE [--dry-run] [--config-dir DIRECTORY]
  mpvideo theme follow active [--dry-run] [--config-dir DIRECTORY]
  mpvideo theme follow static NAME [--dry-run] [--config-dir DIRECTORY]
  mpvideo theme sync [--dry-run] [--config-dir DIRECTORY]
  mpvideo theme reset [--dry-run] [--config-dir DIRECTORY]

Colors accept #RRGGBB and alpha-first #AARRGGBB. QML imports also accept
Qt.rgba(r, g, b, a). 'active' follows ~/.config/themes/.caelestia-use and
tracks static, Caelestia, Noctalia, and Island selector changes.
--dry-run prints the proposed theme without writing files.
)";
}

}

int runThemeCli(QStringList arguments) {
    try {
        if (!arguments.isEmpty() && arguments.first() == "theme")
            arguments.removeFirst();
        if (arguments.contains("--help") || arguments.contains("-h")) {
            printHelp();
            return 0;
        }

        const auto config = configDirectory(arguments);
        bool dryRun = false;
        QStringList commands;
        for (qsizetype index = 0; index < arguments.size(); ++index) {
            const auto &argument = arguments[index];
            if (argument == "--config-dir") {
                if (index + 1 >= arguments.size())
                    fail("--config-dir requires a directory");
                ++index;
            } else if (argument == "--dry-run") {
                dryRun = true;
            } else if (argument.startsWith('-')) {
                fail("Unknown option: " + argument);
            } else {
                commands.append(argument);
            }
        }
        if (commands.isEmpty())
            fail("Expected a theme command. Run mpvideo theme --help.");

        const QString outputPath = themeFilePath(config);
        const QString command = commands.first();
        const QStringList commandArguments = commands.mid(1);
        if (command == "show") {
            requireCount(commandArguments, 0);
            printTheme(currentTheme(outputPath));
            return 0;
        }

        if (dryRun) {
            printTheme(proposedTheme(outputPath, command, commandArguments));
            return 0;
        }

        updateThemeLocked(outputPath, [&](const Theme &current) {
            if (command == "reset") {
                requireCount(commandArguments, 0);
                return defaultTheme();
            }
            if (command == "set") {
                if (commandArguments.size() == 1 && presetNames().contains(commandArguments.first()))
                    return presetTheme(commandArguments.first());
                return setThemeColors(current, commandArguments);
            }
            if (command == "import") {
                requireCount(commandArguments, 1);
                return importPalette(QFileInfo(commandArguments.first()).absoluteFilePath());
            }
            if (command == "follow") {
                Theme theme;
                if (commandArguments.size() == 1 && commandArguments.first() == "active")
                    theme = followTheme("active");
                else if (commandArguments.size() == 2 && commandArguments.first() == "static")
                    theme = followTheme("static", commandArguments.last());
                else
                    fail("Expected theme follow active or theme follow static NAME");
                if (sourceTargetsFile(theme, outputPath))
                    fail("A theme cannot follow its own output file");
                return theme;
            }
            if (command == "sync") {
                requireCount(commandArguments, 0);
                if (current.sourceKind.isEmpty())
                    fail("The current theme does not follow a source");
                const auto theme = refreshSource(current);
                if (sourceTargetsFile(theme, outputPath))
                    fail("A theme cannot follow its own output file");
                return theme;
            }
            fail("Unknown theme command: " + command);
        });
        QTextStream(stdout) << "Saved " << outputPath << '\n';
        return 0;
    } catch (const std::exception &error) {
        QTextStream(stderr) << "mpvideo: " << error.what() << '\n';
        return 2;
    }
}

}
