#include "modal.h"
#include "video_widget.h"
#include "window.h"
#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProcess>
#include <QPushButton>
#include <QSlider>
#include <QTemporaryDir>
#include <QtTest>
using namespace mpvideo;
class WindowTests : public QObject {
    Q_OBJECT
  private:
    QTemporaryDir temporary_;
    void capture(Window &window, const QString &name) {
        auto path = qEnvironmentVariable("MPVIDEO_TEST_SCREENSHOTS");
        if (!path.isEmpty()) {
            QDir().mkpath(path);
            window.grab().save(path + "/" + name + ".png");
        }
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
  private slots:
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
        auto *settings = button(window, "Playback settings");
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
    void rendersVideoAndReleases() {
        auto path = temporary_.filePath("clip.mp4");
        QProcess ffmpeg;
        ffmpeg.start("/usr/bin/ffmpeg", {"-v", "error", "-f", "lavfi", "-i", "testsrc2=size=320x180:rate=24",
                                         "-t", "3", "-c:v", "libx264", "-pix_fmt", "yuv420p", "-y", path});
        QVERIFY(ffmpeg.waitForFinished(15000));
        QCOMPARE(ffmpeg.exitCode(), 0);
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
            QTest::mouseClick(button(window, "Playback settings"), Qt::LeftButton);
            QTest::qWait(150);
            window.grab().save(qEnvironmentVariable("MPVIDEO_TEST_SCREENSHOTS") + "/settings.png");
        }
    }
};
QTEST_MAIN(WindowTests)
#include "window_tests.moc"
