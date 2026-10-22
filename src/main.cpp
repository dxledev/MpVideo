#include "cli.h"
#include "tooltip.h"
#include "window.h"
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QIcon>
#include <QSurfaceFormat>
#include <QTextStream>
#include <QTimer>
int main(int argc, char **argv) {
    QCoreApplication::setApplicationName("mpvideo");
    QCoreApplication::setApplicationVersion("1.0.0");
    QCoreApplication::setOrganizationName("MpVideo");
    QStringList args;
    for (int i = 1; i < argc; ++i)
        args << QString::fromLocal8Bit(argv[i]);
    try {
        if (!args.isEmpty() && (args.first() == "theme" || args.first() == "--help" || args.first() == "-h" ||
                                args.first() == "--version")) {
            QCoreApplication app(argc, argv);
            return mpvideo::runCli(args);
        }
        QString config, screenshot;
        int quitAfter = 0;
        QStringList paths, mpvOptions;
        bool positional = false;
        for (int i = 0; i < args.size(); ++i) {
            auto arg = args[i];
            if (arg == "--") {
                positional = true;
                continue;
            }
            if (!positional && (arg == "--config-dir" || arg == "--screenshot" || arg == "--quit-after" ||
                                arg == "--mpv-option")) {
                if (++i >= args.size())
                    throw std::runtime_error("Missing option value");
                if (arg == "--mpv-option")
                    mpvOptions << args[i];
                else if (arg == "--config-dir")
                    config = QDir(args[i]).filePath("theme.json");
                else if (arg == "--screenshot")
                    screenshot = args[i];
                else {
                    bool ok = false;
                    quitAfter = args[i].toInt(&ok);
                    if (!ok || quitAfter < 100)
                        throw std::runtime_error("--quit-after must be at least 100 milliseconds");
                }
            } else if (!positional && arg.startsWith('-'))
                throw std::runtime_error("Unknown option; use --help");
            else
                paths << arg;
        }
        if (!qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
            qputenv("QT_QPA_PLATFORM", "wayland");
        QSurfaceFormat format;
        format.setDepthBufferSize(0);
        format.setStencilBufferSize(0);
        format.setSwapInterval(1);
        QSurfaceFormat::setDefaultFormat(format);
        QApplication app(argc, argv);
        mpvideo::TooltipFilter tooltips(&app);
        app.installEventFilter(&tooltips);
        app.setStyle("Fusion");
        app.setDesktopFileName("io.github.mpvideo");
        app.setWindowIcon(QIcon::fromTheme("io.github.mpvideo"));
        mpvideo::Window window(config, mpvOptions);
        window.show();
        if (!paths.isEmpty())
            QTimer::singleShot(0, &window, [&window, paths] { window.open(paths); });
        if (!screenshot.isEmpty())
            QTimer::singleShot(quitAfter > 200 ? qMax(100, quitAfter - 100) : 1500, &window,
                               [&window, screenshot] { window.grab().save(screenshot); });
        if (quitAfter)
            QTimer::singleShot(quitAfter, &app, &QApplication::quit);
        return app.exec();
    } catch (const std::exception &e) {
        QTextStream(stderr) << "mpvideo: " << e.what() << '\n';
        return 1;
    }
}
