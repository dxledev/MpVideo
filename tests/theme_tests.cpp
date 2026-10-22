#include "cli.h"
#include "palette/theme.h"
#include "palette/theme_manager.h"
#include "palette/theme_source.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

using namespace mpvideo;

namespace {

bool writeFile(const QString &path, const QByteArray &contents) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(contents) == contents.size();
}

QByteArray paletteQml(const QByteArray &background, const QByteArray &accent = "#c3a7ff") {
    const QByteArray backgroundValue = background.startsWith('#') ? "\"" + background + "\"" : background;
    return "import QtQuick\nQtObject {\n"
           "property color background: " +
           backgroundValue +
           "\n"
           "property color text: \"#f1f2f4\"\n"
           "property color accent: \"" +
           accent +
           "\"\n"
           "}\n";
}

void setConfigHome(const QTemporaryDir &temporary, QByteArray *oldValue) {
    *oldValue = qgetenv("XDG_CONFIG_HOME");
    qputenv("XDG_CONFIG_HOME", temporary.path().toUtf8());
}

void restoreConfigHome(const QByteArray &oldValue) {
    if (oldValue.isNull())
        qunsetenv("XDG_CONFIG_HOME");
    else
        qputenv("XDG_CONFIG_HOME", oldValue);
}

}

class ThemeTests final : public QObject {
    Q_OBJECT
  private slots:
    void canonicalJsonPreservesArgb();
    void themeJsonAcceptsQtRgbaColors();
    void cliSetAcceptsQtRgbaAndDryRunDoesNotWrite();
    void qmlRgbaUsesCorrectChannelOrderAndAlpha();
    void activeSelectorRetargetsAndRejectsInvalidSource();
    void staticThemeUsesNamedDirectory();
    void managerFollowsActiveAndPersistsChanges();
    void managerFollowsStaticSymlinkRetarget();
};

void ThemeTests::canonicalJsonPreservesArgb() {
    Theme source = defaultTheme();
    source.colors.insert("background", QColor::fromString("#80402010"));
    Theme parsed;
    QString error;
    QVERIFY2(parseTheme(source.toJson(), &parsed, &error), qPrintable(error));
    QCOMPARE(parsed.color("background").alpha(), 128);
    QCOMPARE(parsed.color("background").red(), 64);
    QCOMPARE(parsed.color("background").green(), 32);
    QCOMPARE(parsed.color("background").blue(), 16);
    QCOMPARE(parsed.toJson().value("colors").toObject().value("background").toString(), "#80402010");
}

void ThemeTests::themeJsonAcceptsQtRgbaColors() {
    QJsonObject data = defaultTheme().toJson();
    QJsonObject colors = data.value("colors").toObject();
    colors.insert("background", "Qt.rgba(14 /255, 30/255, 54 / 255, 0.88)");
    data.insert("colors", colors);
    Theme parsed;
    QString error;
    QVERIFY2(parseTheme(data, &parsed, &error), qPrintable(error));
    QCOMPARE(parsed.color("background").name(QColor::HexArgb), "#e00e1e36");

    QColor color;
    QVERIFY2(parsePaletteColor("Qt.rgba(0.1, 0.2, 0.3, 1.2)", &color, &error) == false, qPrintable(error));
}

void ThemeTests::cliSetAcceptsQtRgbaAndDryRunDoesNotWrite() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString configDirectory = temporary.filePath("config");
    const QString configPath = configDirectory + "/theme.json";
    const QStringList dryRun{"theme",
                             "--config-dir",
                             configDirectory,
                             "set",
                             "background=Qt.rgba(14 /255, 30/255, 54 / 255, 0.88)",
                             "--dry-run"};
    QCOMPARE(runCli(dryRun), 0);
    QVERIFY(!QFileInfo::exists(configPath));

    QStringList update = dryRun;
    update.removeLast();
    QCOMPARE(runCli(update), 0);
    Theme saved;
    QString error;
    QVERIFY2(loadTheme(configPath, &saved, &error), qPrintable(error));
    QCOMPARE(saved.color("background").name(QColor::HexArgb), "#e00e1e36");
}

void ThemeTests::qmlRgbaUsesCorrectChannelOrderAndAlpha() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString path = temporary.filePath("Colors.qml");
    QVERIFY(writeFile(path, paletteQml("Qt.rgba(14 /255, 30/255, 54 / 255, 0.88)")));
    Theme imported;
    QString error;
    QVERIFY2(importTheme(path, &imported, &error), qPrintable(error));
    QCOMPARE(imported.color("background").name(QColor::HexArgb), "#e00e1e36");

    QVERIFY(writeFile(path, paletteQml("Qt.rgba(0.1, 0.2, 0.3, 1.2)")));
    QVERIFY(!importTheme(path, &imported, &error));
    QCOMPARE(imported.color("background").name(QColor::HexArgb), "#e00e1e36");
}

void ThemeTests::activeSelectorRetargetsAndRejectsInvalidSource() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QByteArray oldConfig;
    setConfigHome(temporary, &oldConfig);
    const QString first = temporary.filePath("themes/static-first");
    const QString second = temporary.filePath("themes/.dynamic/active-two");
    const QString selector = temporary.filePath("themes/.caelestia-use");
    QVERIFY(writeFile(first + "/Colors.qml", paletteQml("#101010", "#ff0000")));
    QVERIFY(writeFile(second + "/Colors.qml", paletteQml("#202020", "#00ff00")));
    QVERIFY(QDir().mkpath(QFileInfo(selector).absolutePath()));
    QVERIFY(QFile::link(first, selector));

    Theme followed;
    QString error;
    QVERIFY2(followActiveTheme(&followed, &error), qPrintable(error));
    QCOMPARE(followed.sourceKind, "active");
    QCOMPARE(followed.sourcePath, selector + "/Colors.qml");
    QCOMPARE(followed.color("accent"), QColor("#ff0000"));

    QVERIFY(QFile::remove(selector));
    QVERIFY(QFile::link(second, selector));
    QVERIFY2(refreshFollowedTheme(&followed, &error), qPrintable(error));
    QCOMPARE(followed.color("accent"), QColor("#00ff00"));
    const Theme lastGood = followed;

    QVERIFY(writeFile(second + "/Colors.qml", "QtObject { property color background: Qt.rgba(2, 0, 0, 1) }"));
    QVERIFY(!refreshFollowedTheme(&followed, &error));
    QCOMPARE(followed.toJson(), lastGood.toJson());
    restoreConfigHome(oldConfig);
}

void ThemeTests::staticThemeUsesNamedDirectory() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QByteArray oldConfig;
    setConfigHome(temporary, &oldConfig);
    QVERIFY(writeFile(temporary.filePath("themes/cinema/Colors.qml"), paletteQml("#191919", "#f0a020")));
    Theme followed;
    QString error;
    QVERIFY2(followStaticTheme("cinema", &followed, &error), qPrintable(error));
    QCOMPARE(followed.name, "cinema");
    QCOMPARE(followed.sourceKind, "static");
    QCOMPARE(followed.color("accent"), QColor("#f0a020"));
    restoreConfigHome(oldConfig);
}

void ThemeTests::managerFollowsActiveAndPersistsChanges() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QByteArray oldConfig;
    setConfigHome(temporary, &oldConfig);
    const QString first = temporary.filePath("themes/preset-a");
    const QString second = temporary.filePath("themes/.dynamic/preset-b");
    const QString selector = temporary.filePath("themes/.caelestia-use");
    QVERIFY(writeFile(first + "/Colors.qml", paletteQml("#111111", "#abcdef")));
    QVERIFY(writeFile(second + "/Colors.qml", paletteQml("#222222", "#fedcba")));
    QVERIFY(QDir().mkpath(QFileInfo(selector).absolutePath()));
    QVERIFY(QFile::link(first, selector));

    const QString config = temporary.filePath("mpvideo/theme.json");
    ThemeManager manager(config);
    QCOMPARE(manager.theme().sourceKind, "active");
    QCOMPARE(manager.theme().color("accent"), QColor("#abcdef"));

    QVERIFY(writeFile(first + "/Colors.qml", paletteQml("#111111", "#ff8800")));
    QTRY_COMPARE_WITH_TIMEOUT(manager.theme().color("accent"), QColor("#ff8800"), 2500);
    Theme saved;
    QString error;
    QVERIFY2(loadTheme(config, &saved, &error), qPrintable(error));
    QCOMPARE(saved.color("accent"), QColor("#ff8800"));

    QVERIFY(QFile::remove(selector));
    QVERIFY(QFile::link(second, selector));
    QTRY_COMPARE_WITH_TIMEOUT(manager.theme().color("accent"), QColor("#fedcba"), 2500);
    QVERIFY(writeFile(second + "/Colors.qml", "QtObject { property color background: Qt.rgba(2, 0, 0, 1) }"));
    const Theme lastGood = manager.theme();
    QVERIFY(!manager.sync(&error));
    QCOMPARE(manager.theme().toJson(), lastGood.toJson());
    QVERIFY2(loadTheme(config, &saved, &error), qPrintable(error));
    QCOMPARE(saved.toJson(), lastGood.toJson());
    restoreConfigHome(oldConfig);
}

void ThemeTests::managerFollowsStaticSymlinkRetarget() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QByteArray oldConfig;
    setConfigHome(temporary, &oldConfig);
    const QString first = temporary.filePath("themes/film-dark");
    const QString second = temporary.filePath("themes/film-light");
    const QString selected = temporary.filePath("themes/cinema");
    QVERIFY(writeFile(first + "/Colors.qml", paletteQml("#181818", "#5522aa")));
    QVERIFY(writeFile(second + "/Colors.qml", paletteQml("#eeeeee", "#227755")));
    QVERIFY(QDir().mkpath(QFileInfo(selected).absolutePath()));
    QVERIFY(QFile::link(first, selected));

    ThemeManager manager(temporary.filePath("mpvideo/theme.json"));
    QString error;
    QVERIFY2(manager.followStatic("cinema", &error), qPrintable(error));
    QCOMPARE(manager.theme().color("accent"), QColor("#5522aa"));
    QVERIFY(QFile::remove(selected));
    QVERIFY(QFile::link(second, selected));
    QTRY_COMPARE_WITH_TIMEOUT(manager.theme().color("accent"), QColor("#227755"), 2500);
    Theme saved;
    QVERIFY2(loadTheme(manager.configPath(), &saved, &error), qPrintable(error));
    QCOMPARE(saved.color("accent"), QColor("#227755"));
    restoreConfigHome(oldConfig);
}

QTEST_GUILESS_MAIN(ThemeTests)
#include "theme_tests.moc"
