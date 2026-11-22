// The measurements Consiglio keeps for itself: turn records, context trend and
// the small trend widget. Every number asserted here is one Hermes reported
// over ACP; the store is capped, versioned and separate from ~/.hermes.
#include "sessionmetrics.h"
#include "sparkline.h"
#include "theme.h"

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QPixmap>
#include <QScopedPointer>
#include <QTemporaryDir>
#include <QtTest>

class SessionMetricsTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        Theme::instance().setMode(Theme::Mode::Dark);
        m_dir.reset(new QTemporaryDir);
        QVERIFY(m_dir->isValid());
        auto &m = SessionMetrics::instance();
        m.setStorePath(m_dir->filePath(QStringLiteral("metrics.json")));
        m.clear();
    }

    void cleanup() {
        // Every test starts from an empty store.
        SessionMetrics::instance().clear();
    }

    void recordsTurnsAndSummarises() {
        auto &m = SessionMetrics::instance();
        m.beginSession(QStringLiteral("s1"));

        m.beginTurn(1000);
        m.noteToolCall();
        m.noteContext(100, 1000);
        m.endTurn(QStringLiteral("end_turn"), 10, 200, 3000);

        m.beginTurn(4000);
        m.endTurn(QStringLiteral("cancelled"), 5, 50, 5000);

        const auto turns = m.turns(QStringLiteral("s1"));
        QCOMPARE(turns.size(), 2);
        QCOMPARE(turns.at(0).inputTokens, qint64(10));
        QCOMPARE(turns.at(0).outputTokens, qint64(200));
        QCOMPARE(turns.at(0).toolCalls, 1);
        QCOMPARE(turns.at(0).seconds, 2.0);
        QCOMPARE(turns.at(1).stopReason, QString("cancelled"));

        const auto sum = m.summary(QStringLiteral("s1"));
        QCOMPARE(sum.turns, 2);
        QCOMPARE(sum.inputTokens, qint64(15));
        QCOMPARE(sum.outputTokens, qint64(250));
        QCOMPARE(sum.toolCalls, 1);
        QCOMPARE(sum.totalSeconds, 3.0);
        QCOMPARE(sum.avgSeconds, 1.5);
        QCOMPARE(sum.fastestSeconds, 1.0);
        QCOMPARE(qRound(sum.avgOutputTokensPerSecond), 83);
        QCOMPARE(sum.cancelledTurns, 1);
        QCOMPARE(sum.peakContextUsed, 100);
        QCOMPARE(sum.peakContextSize, 1000);
    }

    void contextSeriesIsPercentOfTheWindow() {
        auto &m = SessionMetrics::instance();
        m.beginSession(QStringLiteral("s2"));
        // Four readings of a 1000-token window: 10%, 30%, 60%, 90%.
        for (int used : {100, 300, 600, 900}) m.noteContext(used, 1000);
        const auto series = m.contextPercentSeries(QStringLiteral("s2"));
        QCOMPARE(series.size(), 4);
        QCOMPARE(series.first(), 10.0);
        QCOMPARE(series.last(), 90.0);
        const auto sum = m.summary(QStringLiteral("s2"));
        QCOMPARE(sum.peakContextUsed, 900);
    }

    void samplesAreNotRecordedForEveryToken() {
        auto &m = SessionMetrics::instance();
        m.beginSession(QStringLiteral("s3"));
        // A 1% wobble is not a new sample; a 3% move is.
        m.noteContext(100, 1000);
        m.noteContext(110, 1000);
        QCOMPARE(m.contextSamples(QStringLiteral("s3")).size(), 1);
        m.noteContext(140, 1000);
        QCOMPARE(m.contextSamples(QStringLiteral("s3")).size(), 2);
    }

    void turnCapAndPersistence() {
        auto &m = SessionMetrics::instance();
        m.beginSession(QStringLiteral("s4"));
        const int total = SessionMetrics::kMaxTurnsPerSession + 100;
        for (int i = 0; i < total; ++i) {
            m.beginTurn(i * 1000);
            m.endTurn(QStringLiteral("end_turn"), 2, i, i * 1000 + 500);
        }
        QCOMPARE(m.turns(QStringLiteral("s4")).size(), SessionMetrics::kMaxTurnsPerSession);
        // The newest turn survived, the oldest were dropped.
        QCOMPARE(m.turns(QStringLiteral("s4")).last().outputTokens, qint64(total - 1));

        const QString path = m.storePath();
        m.save();
        m.setStorePath(path);   // forgets the in-memory copy
        QVERIFY(m.turns(QStringLiteral("s4")).isEmpty());
        m.load();
        QCOMPARE(m.turns(QStringLiteral("s4")).size(), SessionMetrics::kMaxTurnsPerSession);
        QCOMPARE(m.turns(QStringLiteral("s4")).last().outputTokens, qint64(total - 1));
        QVERIFY(QFileInfo::exists(path));
    }

    void unknownSchemaIsIgnoredNotGuessed() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("metrics.json"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{\"version\": 99, \"sessions\": {\"x\": {\"turns\": [{\"out\": 1}]}}}");
        f.close();
        auto &m = SessionMetrics::instance();
        m.setStorePath(path);
        m.load();
        QVERIFY(m.turns(QStringLiteral("x")).isEmpty());
    }

    void sparklineRendersValuesAndEmptyState() {
        ui::Sparkline s;
        s.setValues({10.0, 42.0, 18.0, 71.0});
        s.resize(72, 16);
        QPixmap pm(s.size());
        pm.fill(Qt::transparent);
        s.render(&pm);
        QVERIFY(!pm.isNull());
        QCOMPARE(s.values().size(), 4);

        // A single value, and no values at all, must both paint without dying.
        s.setValues({55.0});
        s.render(&pm);
        s.setValues({});
        s.render(&pm);
        QVERIFY(s.values().isEmpty());
    }

    void metricsSurviveAnUnwritableStore() {
        auto &m = SessionMetrics::instance();
        m.setStorePath(QStringLiteral("/proc/consiglio-should-not-exist/metrics.json"));
        m.beginSession(QStringLiteral("s5"));
        m.beginTurn(1000);
        m.endTurn(QStringLiteral("end_turn"), 1, 1, 2000);   // save() must not crash
        QCOMPARE(m.turns(QStringLiteral("s5")).size(), 1);
    }

private:
    QScopedPointer<QTemporaryDir> m_dir;
};

QTEST_MAIN(SessionMetricsTest)
#include "test_sessionmetrics.moc"
