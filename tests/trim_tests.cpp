#include "trim_timeline.h"
#include <QtTest>
using namespace mpvideo;
class TrimTests : public QObject {
    Q_OBJECT
  private slots:
    void splitAloneRemovesNothing() {
        TrimTimeline t;
        t.resize(600, 48);
        t.reset(60);
        QVERIFY(t.splitAt(20));
        QCOMPARE(t.segments().size(), 2);
        QVERIFY(t.cuts().isEmpty());
        QVERIFY(!t.isModified());
    }
    void deleteMiddleCutsOnce() {
        TrimTimeline t;
        t.resize(600, 48);
        t.reset(60);
        t.splitAt(20);
        t.splitAt(40);
        QVERIFY(t.removeAt(30));
        QCOMPARE(t.cuts().size(), 1);
        QCOMPARE(t.cuts()[0].first, 20.0);
        QCOMPARE(t.cuts()[0].second, 40.0);
        QCOMPARE(t.keptSeconds(), 40.0);
    }
    void deleteEndsGivesEdgeCuts() {
        TrimTimeline t;
        t.resize(600, 48);
        t.reset(60);
        t.splitAt(10);
        t.splitAt(50);
        t.removeAt(5);
        t.removeAt(55);
        QCOMPARE(t.cuts().size(), 2);
        QCOMPARE(t.cuts()[0].first, 0.0);
        QCOMPARE(t.cuts()[1].second, 60.0);
    }
    void cannotDeleteLastSegment() {
        TrimTimeline t;
        t.resize(600, 48);
        t.reset(60);
        QVERIFY(!t.removeAt(30));
        QVERIFY(!t.isModified());
    }
    void restoreAndUndo() {
        TrimTimeline t;
        t.resize(600, 48);
        t.reset(60);
        t.splitAt(30);
        t.removeAt(45);
        QVERIFY(t.isModified());
        QVERIFY(t.restoreAt(45));
        QVERIFY(!t.isModified());
        t.undo();
        QVERIFY(t.isModified());
        t.undo();
        QVERIFY(!t.isModified());
        QCOMPARE(t.segments().size(), 2);
    }
    void playheadWinsOverSplitLine() {
        TrimTimeline t;
        t.resize(600, 48);
        t.reset(60);
        t.splitAt(30);
        t.setPlayhead(30);
        // Press on the split with the playhead parked there, drag right: the split must stay put.
        QTest::mousePress(&t, Qt::LeftButton, {}, QPoint(300, 24));
        QTest::mouseMove(&t, QPoint(400, 24));
        QTest::mouseRelease(&t, Qt::LeftButton, {}, QPoint(400, 24));
        QCOMPARE(t.segments()[0].end, 30.0);
        QCOMPARE(t.segments()[1].start, 30.0);
        // With the playhead elsewhere, the same drag moves the split.
        // Dragging a split right pulls only the right clip's start; the left clip stays put.
        t.setPlayhead(5);
        QTest::mousePress(&t, Qt::LeftButton, {}, QPoint(300, 24));
        QTest::mouseMove(&t, QPoint(400, 24));
        QTest::mouseRelease(&t, Qt::LeftButton, {}, QPoint(400, 24));
        QCOMPARE(t.segments()[0].end, 30.0);
        QVERIFY(qAbs(t.segments()[1].start - 40.0) < 0.5);
        QCOMPARE(t.cuts().size(), 1);
        // Dragging a split left pulls only the left clip's end.
        t.reset(60);
        t.splitAt(30);
        t.setPlayhead(5);
        QTest::mousePress(&t, Qt::LeftButton, {}, QPoint(300, 24));
        QTest::mouseMove(&t, QPoint(200, 24));
        QTest::mouseRelease(&t, Qt::LeftButton, {}, QPoint(200, 24));
        QVERIFY(qAbs(t.segments()[0].end - 20.0) < 0.5);
        QCOMPARE(t.segments()[1].start, 30.0);
    }
    void tooShortSplitRefused() {
        TrimTimeline t;
        t.resize(600, 48);
        t.reset(60);
        QVERIFY(!t.splitAt(0.02));
        QVERIFY(!t.splitAt(59.99));
    }
};
QTEST_MAIN(TrimTests)
#include "trim_tests.moc"
