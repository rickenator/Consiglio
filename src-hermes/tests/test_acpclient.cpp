#include <QtTest>
#include <QSignalSpy>
#include <QTimer>
#include <QStandardPaths>
#include "acpclient.h"

// Integration test: drives a real `hermes acp` process and asserts the
// streaming events that make the UI feel like Hermes actually arrive —
// ready(), streamed answer chunks, context usage, and turn completion.
// Skips cleanly when the hermes binary is not on PATH.
class AcpClientTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void streamsAnswerAndUsage();
    void reportsErrorWhenHermesMissing();

private:
    QString m_hermesPath;
};

void AcpClientTest::initTestCase() {
    m_hermesPath = QStandardPaths::findExecutable("hermes");
    if (m_hermesPath.isEmpty()) {
        // Not fatal — the missing-binary test below handles it.
    }
}

void AcpClientTest::streamsAnswerAndUsage() {
    if (m_hermesPath.isEmpty()) {
        QSKIP("hermes binary not on PATH; skipping live ACP integration test");
    }

    AcpClient client;
    QSignalSpy connectedSpy(&client, &AcpClient::connected);
    QSignalSpy readySpy(&client, &AcpClient::ready);
    QSignalSpy errorSpy(&client, &AcpClient::errorOccurred);
    QSignalSpy msgSpy(&client, &AcpClient::messageChunk);
    QSignalSpy usageSpy(&client, &AcpClient::contextUsage);
    QSignalSpy doneSpy(&client, &AcpClient::turnCompleted);

    // Connect the persistent process (initialize handshake).
    QVERIFY(client.launch(m_hermesPath));

    // Wait for the process to come up (hermes loads plugins/models: a few seconds).
    int waited = 0;
    while (connectedSpy.isEmpty() && errorSpy.isEmpty() && waited < 60) {
        QTest::qWait(250);
        ++waited;
    }
    if (!errorSpy.isEmpty()) {
        const QString err = errorSpy.first().at(0).toString();
        qWarning() << "ACP error during connect:" << err;
        QVERIFY2(false, qPrintable(err.toUtf8()));
    }
    QVERIFY2(connectedSpy.count() >= 1, "process never connected");

    // Open a fresh session rooted at /tmp.
    QVERIFY(client.newSession("/tmp"));

    // Wait for the session to become ready.
    waited = 0;
    while (readySpy.isEmpty() && errorSpy.count() < 2 && waited < 60) {
        QTest::qWait(250);
        ++waited;
    }
    if (errorSpy.count() >= 2) {
        const QString err = errorSpy.at(1).at(0).toString();
        qWarning() << "ACP error during session/new:" << err;
        QVERIFY2(false, qPrintable(err.toUtf8()));
    }
    QVERIFY2(readySpy.count() >= 1, "session never became ready");
    QVERIFY(!client.sessionId().isEmpty());

    // Send a prompt and collect the streamed answer until the turn completes.
    QString answer;
    bool gotUsage = false;
    bool completed = false;
    QEventLoop loop;
    QObject::connect(&client, &AcpClient::messageChunk, [&](const QString &t) {
        answer += t;
    });
    QObject::connect(&client, &AcpClient::contextUsage, [&](int used, int size) {
        Q_UNUSED(used); Q_UNUSED(size);
        gotUsage = true;
    });
    QObject::connect(&client, &AcpClient::turnCompleted, [&](const QString &, qint64, qint64) {
        completed = true;
        loop.quit();
    });

    QVERIFY(client.sendPrompt("Reply with exactly: PING"));

    // Safety timeout so the test can't hang forever.
    QTimer safety;
    safety.setSingleShot(true);
    QObject::connect(&safety, &QTimer::timeout, &loop, [&]() { loop.quit(); });
    safety.start(90000);
    loop.exec();

    QVERIFY2(completed, "turn did not complete in time");
    // The streamed chunks must reconstruct the answer (contains PING).
    QVERIFY2(answer.contains("PING"), qPrintable(answer.toUtf8()));
    // A real Hermes session reports context usage during a turn.
    QVERIFY(gotUsage);
}

void AcpClientTest::reportsErrorWhenHermesMissing() {
    AcpClient client;
    QSignalSpy errorSpy(&client, &AcpClient::errorOccurred);
    // A nonexistent executable must surface an error, not hang or crash.
    client.launch("/nonexistent/hermes-binary");
    int waited = 0;
    while (errorSpy.isEmpty() && waited < 20) {
        QTest::qWait(250);
        ++waited;
    }
    QVERIFY(!errorSpy.isEmpty());
}

QTEST_MAIN(AcpClientTest)
#include "test_acpclient.moc"
