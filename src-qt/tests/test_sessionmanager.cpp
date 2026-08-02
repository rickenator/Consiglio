#include <QtTest/QtTest>
#include <QDir>
#include "backend/sessionmanager.h"

class TestSessionManager : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void testStartSession();
    void testListSessions();
    void testStopSession();
    void testHasSession();
    void testSendCommand();
    void testMultipleSessions();
    void testSendCommand_nonExistent();
    void testCodexSessionDoesNotEmitTerminalUI();
    void testCodexPermissionSelection();

private:
    SessionManager *m_manager;
};

void TestSessionManager::init() {
    m_manager = new SessionManager(nullptr);
}

void TestSessionManager::cleanup() {
    m_manager->stopAllSessions();
    delete m_manager;
    m_manager = nullptr;
}

// Start a session with echo provider (always succeeds)
void TestSessionManager::testStartSession() {
    QString sessionId = m_manager->startSession("echo");
    QVERIFY(!sessionId.isEmpty());

    auto sessions = m_manager->listSessions();
    QCOMPARE(sessions.size(), 1);
    QCOMPARE(sessions[0].provider, QString("echo"));
    QCOMPARE(sessions[0].status, QString("running"));
}

// List sessions should return all active sessions
void TestSessionManager::testListSessions() {
    // Start with empty list
    QCOMPARE(m_manager->listSessions().size(), 0);

    m_manager->startSession("echo");
    QCOMPARE(m_manager->listSessions().size(), 1);

    m_manager->startSession("echo");
    QCOMPARE(m_manager->listSessions().size(), 2);
}

// Stop a session
void TestSessionManager::testStopSession() {
    QString sessionId = m_manager->startSession("echo");
    QVERIFY(m_manager->hasSession(sessionId));

    bool result = m_manager->stopSession(sessionId);
    QVERIFY(result);
    QVERIFY(!m_manager->hasSession(sessionId));
}

// hasSession should return correct state
void TestSessionManager::testHasSession() {
    QVERIFY(!m_manager->hasSession("non-existent"));

    QString sessionId = m_manager->startSession("echo");
    QVERIFY(m_manager->hasSession(sessionId));

    m_manager->stopSession(sessionId);
    QVERIFY(!m_manager->hasSession(sessionId));
}

// sendCommand should work on running sessions
void TestSessionManager::testSendCommand() {
    QString sessionId = m_manager->startSession("echo");
    QVERIFY(m_manager->hasSession(sessionId));

    bool result = m_manager->sendCommand(sessionId, "hello world");
    QVERIFY(result);

    m_manager->stopSession(sessionId);
}

// sendCommand on non-existent session should fail
void TestSessionManager::testSendCommand_nonExistent() {
    bool result = m_manager->sendCommand("non-existent", "ls");
    QVERIFY(!result);
}

// Multiple sessions can run concurrently
void TestSessionManager::testMultipleSessions() {
    QString id1 = m_manager->startSession("echo");
    QString id2 = m_manager->startSession("echo");
    QString id3 = m_manager->startSession("echo");

    QCOMPARE(m_manager->listSessions().size(), 3);
    QVERIFY(m_manager->hasSession(id1));
    QVERIFY(m_manager->hasSession(id2));
    QVERIFY(m_manager->hasSession(id3));

    // Stop one, others should remain
    m_manager->stopSession(id2);
    QCOMPARE(m_manager->listSessions().size(), 2);
    QVERIFY(m_manager->hasSession(id1));
    QVERIFY(!m_manager->hasSession(id2));
    QVERIFY(m_manager->hasSession(id3));

    m_manager->stopAllSessions();
}

void TestSessionManager::testCodexSessionDoesNotEmitTerminalUI() {
    QSignalSpy outputSpy(m_manager, &SessionManager::outputReceived);
    QSignalSpy assistantSpy(m_manager, &SessionManager::assistantMessageReceived);

    const QString sessionId = m_manager->startSession("codex", QDir::currentPath());
    QVERIFY(m_manager->hasSession(sessionId));
    QTest::qWait(300);
    QCOMPARE(outputSpy.count(), 0);
    QCOMPARE(assistantSpy.count(), 0);
    QVERIFY(m_manager->stopSession(sessionId));
}

void TestSessionManager::testCodexPermissionSelection() {
    QVariantMap options;
    options.insert("sandboxMode", "danger-full-access");
    const QString fullAccess = m_manager->startSession(
        "codex", QDir::currentPath(), {}, options);
    QCOMPARE(m_manager->listSessions().size(), 1);
    QCOMPARE(m_manager->listSessions().first().permissionMode,
             QString("danger-full-access"));
    QVERIFY(m_manager->stopSession(fullAccess));

    options.insert("sandboxMode", "not-a-real-mode");
    const QString fallback = m_manager->startSession(
        "codex", QDir::currentPath(), {}, options);
    QCOMPARE(m_manager->listSessions().first().permissionMode,
             QString("workspace-write"));
    QVERIFY(m_manager->stopSession(fallback));
}

QTEST_MAIN(TestSessionManager)
#include "test_sessionmanager.moc"
