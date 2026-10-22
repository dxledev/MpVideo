#include "player.h"
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <clocale>
using namespace mpvideo;
class PlayerTests : public QObject {
    Q_OBJECT
  private slots:
    void desktopNumericLocale() {
        const QByteArray originalLocale(std::setlocale(LC_NUMERIC, nullptr));
        const char *locale = std::setlocale(LC_NUMERIC, "en_US.UTF-8");
        if (!locale)
            QSKIP("An en_US.UTF-8 locale is required for this regression test");
        bool ready = false;
        QByteArray numericLocale;
        {
            Player player(QStringList{"vo=null", "ao=null", "config=no"});
            ready = player.isReady();
            numericLocale = std::setlocale(LC_NUMERIC, nullptr);
        }
        std::setlocale(LC_NUMERIC, originalLocale.constData());
        QVERIFY(ready);
        QCOMPARE(numericLocale, QByteArray("C"));
    }
    void scalarPropertiesAndErrors() {
        Player player(QStringList{"vo=null", "ao=null", "config=no"});
        QVERIFY(player.isReady());
        QSignalSpy errors(&player, &Player::error);
        QVERIFY(player.setProperty("loop-file", QString("inf")));
        QTRY_COMPARE(player.property("loop-file").toString(), QString("inf"));
        QVERIFY(player.setProperty("loop-file", QString("no")));
        QTRY_COMPARE(player.property("loop-file").toString(), QString("no"));
        QVERIFY(player.setProperty("mute", true));
        QTRY_VERIFY(player.property("mute").toBool());
        QVERIFY(player.setProperty("volume", 37.0));
        QTRY_COMPARE(player.property("volume").toDouble(), 37.0);
        QVERIFY(player.setProperty("speed", 1.25));
        QTRY_COMPARE(player.property("speed").toDouble(), 1.25);
        QVERIFY(!player.setProperty("speed", std::numeric_limits<double>::infinity()));
        QVERIFY(!player.command({"not-a-command"}));
        QTRY_VERIFY(errors.size() >= 2);
    }
    void loadSeekQueueAndTeardown() {
        QTemporaryDir dir;
        auto path = dir.filePath("fixture.wav");
        QProcess ffmpeg;
        ffmpeg.start("/usr/bin/ffmpeg",
                     {"-v", "error", "-f", "lavfi", "-i", "sine=frequency=440:duration=4", "-y", path});
        QVERIFY(ffmpeg.waitForFinished(15000));
        QCOMPARE(ffmpeg.exitCode(), 0);
        for (int iteration = 0; iteration < 4; ++iteration) {
            Player player(QStringList{"vo=null", "ao=null", "config=no"});
            QVERIFY(player.isReady());
            QSignalSpy errors(&player, &Player::error);
            QVERIFY(player.command({"loadfile", path, "replace"}));
            QTRY_VERIFY(player.property("duration").toDouble() > 3);
            QTRY_COMPARE(player.playlist().size(), 1);
            QVERIFY(player.setProperty("pause", true));
            QTRY_VERIFY(player.property("pause").toBool());
            QVERIFY(player.command({"seek", "2", "absolute"}));
            QTRY_VERIFY(player.property("time-pos").toDouble() >= 1.8);
            QVERIFY(player.command({"loadfile", path, "append"}));
            QTRY_COMPARE(player.playlist().size(), 2);
            QVERIFY(player.setProperty("aid", QString("no")));
            QTest::qWait(60);
            QCOMPARE(errors.size(), 0);
        }
    }
};
QTEST_GUILESS_MAIN(PlayerTests)
#include "player_tests.moc"
