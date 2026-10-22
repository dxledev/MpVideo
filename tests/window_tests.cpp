#include "desktop_file_picker.h"
#include "modal.h"
#include "video_widget.h"
#include "window.h"
#include <QApplication>
#include <QCheckBox>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlatformSurfaceEvent>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalSpy>
#include <QSlider>
#include <QTemporaryDir>
#include <QWindow>
#include <QtTest>
#include <memory>
using namespace mpvideo;
class PortalRequest : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Request")
  public:
    int closed = 0;
  public slots:
    void Close() { ++closed; }
};
class FileChooserPortal : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.FileChooser")
  public:
    QDBusConnection connection = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "test-portal");
    QString handle;
    QString title;
    QVariantMap options;
    PortalRequest request;
    int opened = 0;
    bool immediateResponse = false;
    bool respond(uint code, const QStringList &uris = {}) {
        auto signal = QDBusMessage::createSignal(handle, "org.freedesktop.portal.Request", "Response");
        signal.setArguments({code, QVariantMap{{"uris", uris}}});
        return connection.send(signal);
    }
  public slots:
    QDBusObjectPath OpenFile(const QString &, const QString &caption, const QVariantMap &choices) {
        auto bus = connection;
        if (!handle.isEmpty())
            bus.unregisterObject(handle);
        auto sender = message().service().mid(1);
        sender.replace('.', '_');
        handle = "/org/freedesktop/portal/desktop/request/" + sender + "/" +
                 choices.value("handle_token").toString();
        title = caption;
        options = choices;
        ++opened;
        bus.registerObject(handle, &request, QDBusConnection::ExportAllSlots);
        if (immediateResponse)
            respond(1);
        return QDBusObjectPath(handle);
    }
};
class SurfaceEvents : public QObject {
  public:
    int destroyed = 0;

  protected:
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::PlatformSurface &&
            static_cast<QPlatformSurfaceEvent *>(event)->surfaceEventType() ==
                QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)
            ++destroyed;
        return false;
    }
};
class WindowTests : public QObject {
    Q_OBJECT
  private:
    QTemporaryDir temporary_;
    FileChooserPortal portal_;
    void capture(Window &window, const QString &name) {
        auto path = qEnvironmentVariable("MPVIDEO_TEST_SCREENSHOTS");
        if (!path.isEmpty()) {
            QDir().mkpath(path);
            window.grab().save(path + "/" + name + ".png");
        }
    }
    QString createClip(const QString &name) {
        const auto path = temporary_.filePath(name + ".mp4");
        QProcess ffmpeg;
        ffmpeg.start("/usr/bin/ffmpeg", {"-v", "error", "-f", "lavfi", "-i", "testsrc2=size=320x180:rate=24",
                                         "-t", "3", "-c:v", "libx264", "-pix_fmt", "yuv420p", "-y", path});
        if (!ffmpeg.waitForFinished(15000) || ffmpeg.exitCode() != 0)
            return {};
        return path;
    }
    QStringList options() const {
        return qEnvironmentVariableIsSet("MPVIDEO_TEST_SOFTWARE")
                   ? QStringList{"hwdec=no", "gpu-hwdec-interop=no"}
                   : QStringList{};
    }
    QPushButton *button(Window &window, const QString &name) {
        for (auto *b : window.findChildren<QPushButton *>())
            if (b->accessibleName() == name)
                return b;
        return nullptr;
    }
    QCheckBox *builtInToggle(Modal *modal) {
        for (auto *check : modal->findChildren<QCheckBox *>())
            if (check->text() == "Use built-in file picker")
                return check;
        return nullptr;
    }
    QPushButton *speedButton(Modal *modal, const QString &label) {
        for (auto *action : modal->findChildren<QPushButton *>())
            if (action->text() == label)
                return action;
        return nullptr;
    }
  private slots:
    void initTestCase() {
        auto bus = portal_.connection;
        QVERIFY2(bus.registerService("org.freedesktop.portal.Desktop"),
                 "Run window tests with dbus-run-session");
        QVERIFY(
            bus.registerObject("/org/freedesktop/portal/desktop", &portal_, QDBusConnection::ExportAllSlots));
    }
    void settingsControls() {
        Window window(temporary_.filePath("controls/theme.json"), options());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *settings = button(window, "Settings");
        QVERIFY(settings);
        QCOMPARE(settings->focusPolicy(), Qt::TabFocus);
        QTest::mouseClick(settings, Qt::LeftButton);
        auto *modal = window.findChild<Modal *>();
        QVERIFY(modal);
        QVERIFY(!settings->hasFocus());
        QVERIFY(builtInToggle(modal));
        QVERIFY(builtInToggle(modal)->isChecked());
        auto *normal = speedButton(modal, "1×");
        auto *faster = speedButton(modal, "1.5×");
        QVERIFY(normal);
        QVERIFY(faster);
        QTRY_VERIFY(normal->isChecked());
        capture(window, "settings-desktop");
        QTest::mouseClick(faster, Qt::LeftButton);
        QTRY_VERIFY(faster->isChecked());
        QVERIFY(!normal->isChecked());
        QTest::qWait(100);
        QTest::keyClick(modal, Qt::Key_Escape);
        QTRY_VERIFY(window.findChild<Modal *>() == nullptr);
        QVERIFY(!settings->hasFocus());
        QVERIFY(!button(window, "Open media (Ctrl+O)")->hasFocus());
        capture(window, "header-after-settings");
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        settings->setFocus(Qt::TabFocusReason);
        QVERIFY(settings->hasFocus());
        QCOMPARE(settings->property("keyboardFocus").toBool(), true);
        QTest::mouseClick(settings, Qt::LeftButton);
        modal = window.findChild<Modal *>();
        QVERIFY(modal);
        QTRY_VERIFY(speedButton(modal, "1.5×")->isChecked());
        window.resize(680, 440);
        QTest::qWait(80);
        capture(window, "settings-controls-small");
        auto *scroll = modal->findChild<QScrollArea *>();
        QVERIFY(scroll);
        scroll->ensureWidgetVisible(builtInToggle(modal));
        const QRect bounds(builtInToggle(modal)->mapTo(scroll->viewport(), QPoint()),
                           builtInToggle(modal)->size());
        QVERIFY(scroll->viewport()->rect().contains(bounds));
    }
    void filePickerPreference() {
        const auto themePath = temporary_.filePath("picker/theme.json");
        {
            Window window(themePath, options());
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));
            QTest::mouseClick(button(window, "Settings"), Qt::LeftButton);
            auto *modal = window.findChild<Modal *>();
            QVERIFY(modal);
            auto *toggle = builtInToggle(modal);
            QVERIFY(toggle);
            QVERIFY(toggle->isChecked());
            QTest::mouseClick(toggle, Qt::LeftButton, Qt::NoModifier, QPoint(6, toggle->height() / 2));
            QVERIFY(!toggle->isChecked());
            capture(window, "settings-desktop-picker");
            QSettings saved(temporary_.filePath("picker/settings.ini"), QSettings::IniFormat);
            QCOMPARE(saved.value("files/useBuiltInPicker").toBool(), false);
        }
        Window window(themePath, options());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const int opened = portal_.opened;
        QTest::mouseClick(button(window, "Open media (Ctrl+O)"), Qt::LeftButton);
        QTRY_COMPARE(portal_.opened, opened + 1);
        QVERIFY(window.findChild<DesktopFilePicker *>());
        QVERIFY(!window.findChild<Modal *>());
        QCOMPARE(portal_.title, QString("Open media"));
        QCOMPARE(portal_.options.value("multiple").toBool(), true);
        QVERIFY(portal_.respond(1));
        QTRY_VERIFY(window.findChild<DesktopFilePicker *>() == nullptr);
        auto *idleVideo = window.findChild<VideoWidget *>();
        QVERIFY(idleVideo);
        QVERIFY(!idleVideo->isVisible());
        QVERIFY(!idleVideo->isValid());
        QVERIFY(!idleVideo->context());
        QTest::mouseClick(button(window, "Audio and subtitles"), Qt::LeftButton);
        auto *modal = window.findChild<Modal *>();
        QVERIFY(modal);
        auto *load = speedButton(modal, "Load subtitle file…");
        QVERIFY(load);
        QTest::mouseClick(load, Qt::LeftButton);
        QTRY_COMPARE(portal_.opened, opened + 2);
        QCOMPARE(portal_.options.value("multiple").toBool(), false);
        QCOMPARE(portal_.title, QString("Add subtitles"));
        QVERIFY(portal_.respond(1));
        QTRY_VERIFY(window.findChild<DesktopFilePicker *>() == nullptr);
        QTest::mouseClick(button(window, "Settings"), Qt::LeftButton);
        modal = window.findChild<Modal *>();
        QVERIFY(modal);
        auto *toggle = builtInToggle(modal);
        QVERIFY(toggle);
        QVERIFY(!toggle->isChecked());
        QTest::mouseClick(toggle, Qt::LeftButton, Qt::NoModifier, QPoint(6, toggle->height() / 2));
        QVERIFY(toggle->isChecked());
        QTest::keyClick(modal, Qt::Key_Escape);
        QTRY_VERIFY(window.findChild<Modal *>() == nullptr);
        QTest::mouseClick(button(window, "Open media (Ctrl+O)"), Qt::LeftButton);
        QVERIFY(window.findChild<Modal *>());
        QVERIFY(!window.findChild<DesktopFilePicker *>());
        QCOMPARE(portal_.opened, opened + 2);
    }
    void desktopPickerResponses() {
        DesktopFilePicker picker;
        QSignalSpy selected(&picker, &DesktopFilePicker::selected);
        QSignalSpy failed(&picker, &DesktopFilePicker::failed);
        QSignalSpy finished(&picker, &DesktopFilePicker::finished);
        int opened = portal_.opened;
        picker.open("Choose media", true);
        ++opened;
        QTRY_COMPARE(portal_.opened, opened);
        const QStringList paths{temporary_.filePath("video with spaces.mp4"),
                                temporary_.filePath("second.mkv")};
        QVERIFY(portal_.respond(
            0, {QUrl::fromLocalFile(paths[0]).toString(), QUrl::fromLocalFile(paths[1]).toString()}));
        QTRY_COMPARE(selected.count(), 1);
        QCOMPARE(selected.first().first().toStringList(), paths);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(failed.count(), 0);
        picker.open("Choose media", false);
        ++opened;
        QTRY_COMPARE(portal_.opened, opened);
        QVERIFY(portal_.respond(0, {"https://example.com/not-a-local-file"}));
        QTRY_COMPARE(failed.count(), 1);
        QCOMPARE(selected.count(), 1);
        const int closed = portal_.request.closed;
        picker.open("Choose media", false);
        ++opened;
        QTRY_COMPARE(portal_.opened, opened);
        picker.cancel();
        QTRY_COMPARE(portal_.request.closed, closed + 1);
        QCOMPARE(finished.count(), 3);
        portal_.immediateResponse = true;
        picker.open("Choose media", false);
        QTRY_COMPARE(finished.count(), 4);
        portal_.immediateResponse = false;
        QCOMPARE(selected.count(), 1);
    }
    void pendingDesktopPickerCancellation() {
        const int opened = portal_.opened;
        const int closed = portal_.request.closed;
        auto picker = std::make_unique<DesktopFilePicker>();
        picker->open("Choose media", false);
        picker.reset();
        QTRY_COMPARE(portal_.opened, opened + 1);
        QTRY_VERIFY(portal_.request.closed > closed);
        DesktopFilePicker replacement;
        QSignalSpy finished(&replacement, &DesktopFilePicker::finished);
        replacement.open("First request", false);
        replacement.cancel();
        portal_.immediateResponse = true;
        replacement.open("Replacement request", false);
        QTRY_COMPARE(finished.count(), 2);
        portal_.immediateResponse = false;
    }
    void embeddedDialogs() {
        Window window(temporary_.filePath("theme.json"), options());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *open = button(window, "Open media (Ctrl+O)");
        QVERIFY(open);
        QTest::mouseClick(open, Qt::LeftButton);
        auto *modal = window.findChild<Modal *>();
        QVERIFY(modal);
        QVERIFY(modal->isVisible());
        QVERIFY(!modal->isWindow());
        QCOMPARE(QApplication::topLevelWidgets().size(), 1);
        capture(window, "open-desktop");
        window.resize(680, 440);
        QTest::qWait(80);
        capture(window, "open-small");
        bool openActionVisible = false;
        for (auto *action : modal->findChildren<QPushButton *>()) {
            if (action->text() == "Open") {
                const QRect bounds(action->mapTo(modal, QPoint()), action->size());
                openActionVisible = action->isVisible() && modal->rect().contains(bounds);
            }
        }
        QVERIFY2(openActionVisible, "Open action must remain visible at the minimum window size");
        QVERIFY(window.rect().contains(modal->geometry()));
        for (auto *edit : modal->findChildren<QLineEdit *>())
            QVERIFY(modal->rect().contains(edit->mapTo(modal, QPoint(0, 0))));
        QTest::keyClick(modal, Qt::Key_Escape);
        QTRY_VERIFY(window.findChild<Modal *>() == nullptr);
        auto *settings = button(window, "Settings");
        QVERIFY(settings);
        QTest::mouseClick(settings, Qt::LeftButton);
        modal = window.findChild<Modal *>();
        QVERIFY(modal);
        capture(window, "settings-small");
        QTest::keyClick(modal, Qt::Key_Escape);
        QTRY_VERIFY(window.findChild<Modal *>() == nullptr);
        QTest::mouseClick(open, Qt::LeftButton);
        modal = window.findChild<Modal *>();
        QVERIFY(modal);
        window.close();
    }
    void reloadPalette() {
        auto path = temporary_.filePath("reload.json");
        Window window(path, options());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto theme = defaultTheme();
        theme.colors["accent"] = QColor("#4ba8ca");
        QString error;
        QVERIFY2(saveTheme(path, theme, &error), qPrintable(error));
        QTRY_COMPARE(qApp->palette().highlight().color(), QColor("#4ba8ca"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("invalid");
        file.close();
        QTest::qWait(1200);
        QCOMPARE(qApp->palette().highlight().color(), QColor("#4ba8ca"));
    }
    void unavailablePlayerKeepsError() {
        Window window(temporary_.filePath("unavailable.json"), {"invalid-mpvideo-option=yes"});
        window.open({"example.mp4"});
        QVERIFY(!window.findChild<VideoWidget *>());
        auto *status = window.findChild<QLabel *>("status");
        QVERIFY(status);
        QTRY_VERIFY(status->text().startsWith("Could not configure libmpv:"));
        const auto error = status->text();
        window.open({"example.mp4"});
        QCOMPARE(status->text(), error);
        QVERIFY(!window.findChild<VideoWidget *>());
    }
    void pickerPreservesWindow_data() {
        QTest::addColumn<bool>("builtIn");
        QTest::newRow("built-in") << true;
        QTest::newRow("desktop-portal") << false;
    }
    void pickerPreservesWindow() {
        QFETCH(bool, builtIn);
        const auto name = QString::fromLatin1(QTest::currentDataTag());
        const auto path = createClip(name);
        QVERIFY(!path.isEmpty());
        const auto configDirectory = temporary_.filePath(name);
        QSettings saved(configDirectory + "/settings.ini", QSettings::IniFormat);
        saved.setValue("files/useBuiltInPicker", builtIn);
        saved.sync();
        Window window(configDirectory + "/theme.json", options());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *idleVideo = window.findChild<VideoWidget *>();
        QVERIFY(idleVideo);
        QVERIFY(!idleVideo->context());
        const QPointer<QWindow> originalWindow = window.windowHandle();
        const auto originalId = window.winId();
        const auto originalGeometry = window.geometry();
        SurfaceEvents surfaces;
        originalWindow->installEventFilter(&surfaces);
        QSignalSpy destroyed(originalWindow, &QObject::destroyed);
        for (int selection = 0; selection < 2; ++selection) {
            const int opened = portal_.opened;
            QTest::mouseClick(button(window, "Open media (Ctrl+O)"), Qt::LeftButton);
            if (builtIn) {
                auto *modal = window.findChild<Modal *>();
                QVERIFY(modal);
                modal->findChild<QLineEdit *>()->setText(path);
                QTest::mouseClick(speedButton(modal, "Open"), Qt::LeftButton);
                QTRY_VERIFY(window.findChild<Modal *>() == nullptr);
            } else {
                QTRY_COMPARE(portal_.opened, opened + 1);
                QVERIFY(portal_.respond(0, {QUrl::fromLocalFile(path).toString()}));
                QTRY_VERIFY(window.findChild<DesktopFilePicker *>() == nullptr);
            }
            auto *video = window.findChild<VideoWidget *>();
            QCOMPARE(video, idleVideo);
            QTRY_VERIFY(video->isValid());
            QTRY_VERIFY(window.windowTitle().contains(name));
            QCOMPARE(surfaces.destroyed, 0);
            QCOMPARE(destroyed.count(), 0);
            QCOMPARE(window.windowHandle(), originalWindow.data());
            QCOMPARE(window.winId(), originalId);
            QCOMPARE(window.geometry(), originalGeometry);
            QVERIFY(window.isVisible());
            QCOMPARE(QApplication::topLevelWidgets().size(), 1);
        }
    }
    void rendersVideoAndReleases() {
        const auto path = createClip("clip");
        QVERIFY(!path.isEmpty());
        Window window(temporary_.filePath("play.json"), options());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.open({path});
        QTest::qWait(1300);
        auto *video = window.findChild<VideoWidget *>();
        QVERIFY(video);
        auto image = video->grabFramebuffer();
        QVERIFY(!image.isNull());
        bool colored = false;
        for (int y = 0; y < image.height(); y += 15)
            for (int x = 0; x < image.width(); x += 15) {
                auto c = image.pixelColor(x, y);
                if (std::max({c.red(), c.green(), c.blue()}) - std::min({c.red(), c.green(), c.blue()}) > 60)
                    colored = true;
            }
        QVERIFY2(colored, "Rendered video framebuffer must contain test pattern colors");
        QTest::mouseClick(button(window, "Play or pause (Space)"), Qt::LeftButton);
        for (auto *slider : window.findChildren<QSlider *>()) {
            if (slider->accessibleName() == "Seek position")
                QTest::mouseClick(slider, Qt::LeftButton, Qt::NoModifier,
                                  QPoint(slider->width() * 3 / 4, slider->height() / 2));
        }
        QTRY_VERIFY(window.findChild<QLabel *>("time")->text().startsWith("0:02"));
        window.grab().save(temporary_.filePath("playing.png"));
        if (!qEnvironmentVariableIsEmpty("MPVIDEO_TEST_SCREENSHOTS")) {
            QDir().mkpath(qEnvironmentVariable("MPVIDEO_TEST_SCREENSHOTS"));
            window.grab().save(qEnvironmentVariable("MPVIDEO_TEST_SCREENSHOTS") + "/playing.png");
            QTest::mouseClick(button(window, "Settings"), Qt::LeftButton);
            QTest::qWait(150);
            window.grab().save(qEnvironmentVariable("MPVIDEO_TEST_SCREENSHOTS") + "/settings.png");
        }
    }
};
QTEST_MAIN(WindowTests)
#include "window_tests.moc"
