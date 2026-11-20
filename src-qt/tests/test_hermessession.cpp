#include <QtTest/QtTest>
#include <QFile>
#include <QTemporaryDir>
#include "backend/sessionmanager.h"

// SessionManager's one-shot Hermes provider (`hermes chat -q`), driven
// against a fake `hermes` launcher on PATH. Regression tests for audit
// findings H4 (permission mode), M1 (session_id truncation), M2 (resume drops
// options) and M3 (session id captured before the turn exits).
class TestHermesSession : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void recordsProviderManagedPermission();
    void yoloIsRecordedAsSuch();
    void replyMentioningSessionIdIsNotTruncated();
    void trailingSessionIdLineIsStripped();
    void sessionIdCapturedFromStderrBeforeExit();
    void reconnectKeepsLaunchOptions();
    void persistedOptionsExcludeSecrets();

private:
    void writeFakeHermes(const QString &stdoutText, const QString &stderrText, int sleepSecs = 0);
    QStringList argvLog() const;
    QString startHermes(const QVariantMap &options);

    SessionManager *m_manager = nullptr;
    QTemporaryDir *m_dir = nullptr;
    QByteArray m_savedPath;
};

void TestHermesSession::init() {
    m_manager = new SessionManager(nullptr);
    m_dir = new QTemporaryDir();
    m_savedPath = qgetenv("PATH");
    qputenv("PATH", m_dir->path().toUtf8() + ":" + m_savedPath);
}

void TestHermesSession::cleanup() {
    m_manager->stopAllSessions();
    delete m_manager;
    m_manager = nullptr;
    qputenv("PATH", m_savedPath);
    delete m_dir;
    m_dir = nullptr;
}

void TestHermesSession::writeFakeHermes(const QString &stdoutText, const QString &stderrText,
                                        int sleepSecs) {
    QFile out(m_dir->filePath("stdout.txt"));
    QVERIFY(out.open(QIODevice::WriteOnly));
    out.write(stdoutText.toUtf8());
    out.close();
    QFile err(m_dir->filePath("stderr.txt"));
    QVERIFY(err.open(QIODevice::WriteOnly));
    err.write(stderrText.toUtf8());
    err.close();
    QFile f(m_dir->filePath("hermes"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    const QString d = m_dir->path();
    f.write(QString("#!/bin/sh\n"
                    "printf '%s\\n' \"$*\" >> '%1/argv.log'\n"
                    "cat '%1/stderr.txt' >&2\n"
                    "sleep %2\n"
                    "cat '%1/stdout.txt'\n").arg(d).arg(sleepSecs).toUtf8());
    f.close();
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
}

QStringList TestHermesSession::argvLog() const {
    QFile f(m_dir->filePath("argv.log"));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(f.readAll()).split('\n', Qt::SkipEmptyParts);
}

QString TestHermesSession::startHermes(const QVariantMap &options) {
    return m_manager->startSession("hermes", m_dir->path(), "", options);
}

void TestHermesSession::recordsProviderManagedPermission() {
    QVariantMap o;
    o["sandboxMode"] = "read-only";  // not enforceable for Hermes
    o["model"] = "anthropic/x";
    const QString id = startHermes(o);
    QVERIFY(!id.isEmpty());
    QCOMPARE(m_manager->listSessions().first().permissionMode, QString("provider-managed"));
}

void TestHermesSession::yoloIsRecordedAsSuch() {
    QVariantMap o;
    o["yolo"] = true;
    startHermes(o);
    QCOMPARE(m_manager->listSessions().first().permissionMode, QString("hermes-yolo"));
}

void TestHermesSession::replyMentioningSessionIdIsNotTruncated() {
    writeFakeHermes("Here is the config you asked for:\n  session_id: must be a UUID\n"
                    "  timeout: 30\nEnd of answer.\n",
                    "banner\nsession_id: 20261020_abc\n");
    const QString id = startHermes({});
    QSignalSpy msg(m_manager, &SessionManager::assistantMessageReceived);
    QVERIFY(m_manager->sendCommand(id, "hello"));
    QTRY_COMPARE_WITH_TIMEOUT(msg.count(), 1, 5000);
    const QString text = msg.first().at(1).toString();
    QVERIFY2(text.contains("session_id: must be a UUID"), qPrintable(text));
    QVERIFY(text.endsWith("End of answer."));
}

void TestHermesSession::trailingSessionIdLineIsStripped() {
    writeFakeHermes("The answer is 42.\nsession_id: 20261020_xyz\n", "");
    const QString id = startHermes({});
    QSignalSpy msg(m_manager, &SessionManager::assistantMessageReceived);
    QVERIFY(m_manager->sendCommand(id, "q"));
    QTRY_COMPARE_WITH_TIMEOUT(msg.count(), 1, 5000);
    QCOMPARE(msg.first().at(1).toString(), QString("The answer is 42."));
}

void TestHermesSession::sessionIdCapturedFromStderrBeforeExit() {
    // The turn is still running (sleep) when the id must already be known.
    writeFakeHermes("done\n", "session_id: 20261020_early\n", 3);
    const QString id = startHermes({});
    QSignalSpy tid(m_manager, &SessionManager::codexThreadIdReceived);
    QSignalSpy msg(m_manager, &SessionManager::assistantMessageReceived);
    QVERIFY(m_manager->sendCommand(id, "q"));
    QTRY_COMPARE_WITH_TIMEOUT(tid.count(), 1, 2500);
    QCOMPARE(msg.count(), 0);
    QCOMPARE(tid.first().at(1).toString(), QString("20261020_early"));
}

void TestHermesSession::reconnectKeepsLaunchOptions() {
    writeFakeHermes("ok\n", "session_id: 20261020_abc\n");
    QVariantMap o;
    o["model"] = "anthropic/x";
    o["providerName"] = "anthropic";
    o["reasoning"] = "high";
    o["toolsets"] = "web,terminal";
    o["maxTurns"] = 5;
    const QString id = startHermes(o);
    QSignalSpy msg(m_manager, &SessionManager::assistantMessageReceived);
    QVERIFY(m_manager->sendCommand(id, "hello"));
    QTRY_COMPARE_WITH_TIMEOUT(msg.count(), 1, 5000);

    SessionRecord rec = m_manager->listSessions().first();
    QCOMPARE(rec.codexThreadId, QString("20261020_abc"));
    QVERIFY(!rec.options.isEmpty());
    m_manager->stopSession(id);
    QVERIFY(m_manager->reconnectSession(rec));
    QVERIFY(m_manager->sendCommand(rec.id, "again"));
    QTRY_COMPARE_WITH_TIMEOUT(msg.count(), 2, 5000);

    const QStringList log = argvLog();
    QCOMPARE(log.size(), 2);
    const QString resumed = log.at(1);
    for (const char *needle : {"-m anthropic/x", "--provider anthropic", "--reasoning high",
                               "-t web,terminal", "--max-turns 5", "--resume 20261020_abc", "-q again"}) {
        QVERIFY2(resumed.contains(needle), qPrintable(resumed + " missing " + needle));
    }
}

void TestHermesSession::persistedOptionsExcludeSecrets() {
    QVariantMap o;
    o["apiKey"] = "sk-secret";
    o["historyMcpScript"] = "/opt/old/consiglio_history_mcp.py";
    o["model"] = "m";
    m_manager->startSession("remote_llamacpp", m_dir->path(), "", o);
    const SessionRecord rec = m_manager->listSessions().first();
    QVERIFY(!rec.options.contains("apiKey"));
    QVERIFY(!rec.options.contains("historyMcpScript"));
    QCOMPARE(rec.options.value("model").toString(), QString("m"));
}

QTEST_MAIN(TestHermesSession)
#include "test_hermessession.moc"
