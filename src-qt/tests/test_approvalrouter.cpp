#include <QtTest/QtTest>
#include "backend/approvalrouter.h"
#include <QUuid>

class TestApprovalRouter : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void testRegisterApproval();
    void testRegisterApproval_emptyId();
    void testRegisterApproval_emptySession();
    void testResolveApproval();
    void testPendingApprovals();
    void testPendingApprovals_sessionFilter();
    void testPendingIds();
    void testHas();
    void testPendingCountChanged();
    void testDuplicateReject();
    void testResolveNonExistent();
    void testDoubleResolve();

private:
    ApprovalRouter *m_router;
};

void TestApprovalRouter::init() {
    m_router = new ApprovalRouter(nullptr);
}

void TestApprovalRouter::cleanup() {
    delete m_router;
    m_router = nullptr;
}

// Register a valid approval request
void TestApprovalRouter::testRegisterApproval() {
    ApprovalRequest req;
    req.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    req.sessionId = "session-1";
    req.command = "ls -la";
    req.workingDir = "/home/user/project";
    req.timestamp = QDateTime::currentMSecsSinceEpoch();

    bool result = m_router->registerApproval(req);
    QVERIFY(result);
    QCOMPARE(m_router->pendingCount(), 1);
}

// Register with empty ID should fail
void TestApprovalRouter::testRegisterApproval_emptyId() {
    ApprovalRequest req;
    req.id = "";
    req.sessionId = "session-1";
    req.command = "ls -la";

    bool result = m_router->registerApproval(req);
    QVERIFY(!result);
    QCOMPARE(m_router->pendingCount(), 0);
}

// Register with empty sessionId should fail
void TestApprovalRouter::testRegisterApproval_emptySession() {
    ApprovalRequest req;
    req.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    req.sessionId = "";
    req.command = "ls -la";

    bool result = m_router->registerApproval(req);
    QVERIFY(!result);
    QCOMPARE(m_router->pendingCount(), 0);
}

// Resolve an existing approval
void TestApprovalRouter::testResolveApproval() {
    ApprovalRequest req;
    req.id = "test-approve-1";
    req.sessionId = "session-1";
    req.command = "rm -rf /tmp/test";
    req.timestamp = QDateTime::currentMSecsSinceEpoch();

    m_router->registerApproval(req);
    QCOMPARE(m_router->pendingCount(), 1);

    bool result = m_router->resolve("test-approve-1", true);
    QVERIFY(result);
    QCOMPARE(m_router->pendingCount(), 0);
}

// Pending approvals list
void TestApprovalRouter::testPendingApprovals() {
    ApprovalRequest req1;
    req1.id = "req-1";
    req1.sessionId = "session-1";
    req1.command = "cmd1";
    req1.timestamp = 1000;

    ApprovalRequest req2;
    req2.id = "req-2";
    req2.sessionId = "session-1";
    req2.command = "cmd2";
    req2.timestamp = 2000;

    m_router->registerApproval(req1);
    m_router->registerApproval(req2);

    auto pending = m_router->pendingApprovals();
    QCOMPARE(pending.size(), 2);

    // Should be sorted by timestamp descending
    QCOMPARE(pending[0].command, QString("cmd2"));
    QCOMPARE(pending[1].command, QString("cmd1"));
}

// Pending approvals filtered by session
void TestApprovalRouter::testPendingApprovals_sessionFilter() {
    ApprovalRequest req1;
    req1.id = "req-1";
    req1.sessionId = "session-1";
    req1.command = "cmd1";
    req1.timestamp = 1000;

    ApprovalRequest req2;
    req2.id = "req-2";
    req2.sessionId = "session-2";
    req2.command = "cmd2";
    req2.timestamp = 2000;

    m_router->registerApproval(req1);
    m_router->registerApproval(req2);

    auto pending = m_router->pendingApprovals("session-1");
    QCOMPARE(pending.size(), 1);
    QCOMPARE(pending[0].command, QString("cmd1"));
}

// Pending IDs list
void TestApprovalRouter::testPendingIds() {
    ApprovalRequest req;
    req.id = "id-abc";
    req.sessionId = "session-1";
    req.command = "ls";
    req.timestamp = 1000;

    m_router->registerApproval(req);

    auto ids = m_router->pendingIds();
    QVERIFY(ids.contains("id-abc"));
    QCOMPARE(ids.size(), 1);
}

// Has check
void TestApprovalRouter::testHas() {
    ApprovalRequest req;
    req.id = "has-test";
    req.sessionId = "session-1";
    req.command = "ls";
    req.timestamp = 1000;

    QVERIFY(!m_router->has("has-test"));

    m_router->registerApproval(req);
    QVERIFY(m_router->has("has-test"));

    m_router->resolve("has-test", true);
    QVERIFY(!m_router->has("has-test"));
}

// Pending count changed signal
void TestApprovalRouter::testPendingCountChanged() {
    QSignalSpy spy(m_router, &ApprovalRouter::pendingCountChanged);
    QCOMPARE(spy.count(), 0);

    ApprovalRequest req;
    req.id = "count-1";
    req.sessionId = "session-1";
    req.command = "ls";
    req.timestamp = 1000;

    m_router->registerApproval(req);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy[0][0].toInt(), 1);

    ApprovalRequest req2;
    req2.id = "count-2";
    req2.sessionId = "session-1";
    req2.command = "pwd";
    req2.timestamp = 2000;

    m_router->registerApproval(req2);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy[1][0].toInt(), 2);

    m_router->resolve("count-1", true);
    QCOMPARE(spy.count(), 3);
    QCOMPARE(spy[2][0].toInt(), 1);
}

// Duplicate registration should fail
void TestApprovalRouter::testDuplicateReject() {
    ApprovalRequest req;
    req.id = "dup-test";
    req.sessionId = "session-1";
    req.command = "ls";
    req.timestamp = 1000;

    QVERIFY(m_router->registerApproval(req));
    QVERIFY(!m_router->registerApproval(req)); // duplicate should fail
    QCOMPARE(m_router->pendingCount(), 1);
}

// Resolve non-existent approval should fail
void TestApprovalRouter::testResolveNonExistent() {
    bool result = m_router->resolve("non-existent-id", true);
    QVERIFY(!result);
    QCOMPARE(m_router->pendingCount(), 0);
}

// Double resolve should fail
void TestApprovalRouter::testDoubleResolve() {
    ApprovalRequest req;
    req.id = "double-resolve";
    req.sessionId = "session-1";
    req.command = "ls";
    req.timestamp = 1000;

    m_router->registerApproval(req);
    QVERIFY(m_router->resolve("double-resolve", true));
    QVERIFY(!m_router->resolve("double-resolve", false)); // double resolve fails
}

QTEST_MAIN(TestApprovalRouter)
#include "test_approvalrouter.moc"
