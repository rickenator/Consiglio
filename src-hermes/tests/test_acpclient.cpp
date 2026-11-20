#include <QtTest>
#include <QSignalSpy>
#include <QTimer>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include "acpclient.h"

#ifndef FAKE_ACP_SCRIPT
#error "FAKE_ACP_SCRIPT must point at tests/fake_hermes_acp.py"
#endif

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

    // Scripted-agent tests (tests/fake_hermes_acp.py). These cover the paths
    // the audit found untested: agent->client requests vs responses, the
    // permission round-trip, error recovery, and the Hermes self-update /
    // restart path. They need python3 and skip cleanly without it.
    void init();
    void cleanup();
    void resolvesExecutableFromPathAndNegotiates();
    void explicitAndEnvExecutableWin();
    void agentRequestIsNotTreatedAsResponse();
    void permissionCancelRoundTrip();
    void stopSessionCancelsTurnAndPermission();
    void promptErrorLeavesClientUsable();
    void relaunchWorksAfterAgentExit();
    void agentUpdateAndRestartRestoresSession();
    void manualRestartRestoresSession();
    void restartGivesUpAfterMaxAttempts();
    void protocolDriftIsTolerated();
    void resumeFallsBackToLoadSession();
    void legacyAgentWithoutCapabilities();
    void staleUpdatesAfterStopAreDropped();
    void slashCommandsClearedOnReset();
    void sendPromptWithoutSessionFails();

private:
    bool writeWrapper(const QString &version, const QString &caps, const QString &body = QString());
    QList<QJsonObject> agentLog() const;
    bool logHasMethod(const QString &method) const;
    void connectFresh(AcpClient &client, bool openSession = true);

    QString m_hermesPath;
    QString m_python;
    QTemporaryDir *m_dir = nullptr;
    QByteArray m_savedPath;
    QByteArray m_savedHermesBin;
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

// ─── Scripted-agent harness ────────────────────────────────────────────────

void AcpClientTest::init() {
    m_python = QStandardPaths::findExecutable("python3");
    m_dir = new QTemporaryDir();
    m_savedPath = qgetenv("PATH");
    m_savedHermesBin = qgetenv("HERMES_BIN");
    qunsetenv("HERMES_BIN");
    // The fake goes first on PATH so launch() with no argument resolves it,
    // exactly as a real install is resolved.
    qputenv("PATH", m_dir->path().toUtf8() + ":" + m_savedPath);
}

void AcpClientTest::cleanup() {
    qputenv("PATH", m_savedPath);
    if (m_savedHermesBin.isEmpty()) qunsetenv("HERMES_BIN");
    else qputenv("HERMES_BIN", m_savedHermesBin);
    delete m_dir;
    m_dir = nullptr;
}

bool AcpClientTest::writeWrapper(const QString &version, const QString &caps, const QString &body) {
    // A `hermes` launcher like the one `hermes update` keeps on PATH. Tests
    // rewrite it in place to simulate an update replacing the install.
    const QString path = m_dir->filePath("hermes");
    const QString tmp = path + ".new";
    QFile f(tmp);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    QString script = "#!/bin/sh\n";
    if (body.isEmpty()) {
        script += QString("export FAKE_ACP_VERSION='%1'\nexport FAKE_ACP_CAPS='%2'\n"
                          "export FAKE_ACP_LOG='%3'\nexec '%4' '%5' \"$@\"\n")
                      .arg(version, caps, m_dir->filePath("agent.log"), m_python, FAKE_ACP_SCRIPT);
    } else {
        script += body;
    }
    f.write(script.toUtf8());
    f.close();
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QFile::remove(path);
    return QFile::rename(tmp, path);  // atomic swap, like an installer
}

QList<QJsonObject> AcpClientTest::agentLog() const {
    QList<QJsonObject> out;
    QFile f(m_dir->filePath("agent.log"));
    if (!f.open(QIODevice::ReadOnly)) return out;
    for (const QByteArray &line : f.readAll().split('\n')) {
        if (line.trimmed().isEmpty()) continue;
        out.append(QJsonDocument::fromJson(line).object());
    }
    return out;
}

bool AcpClientTest::logHasMethod(const QString &method) const {
    for (const auto &m : agentLog()) if (m.value("method").toString() == method) return true;
    return false;
}

#define REQUIRE_PYTHON() \
    do { if (m_python.isEmpty()) QSKIP("python3 not found; scripted ACP agent unavailable"); } while (0)

void AcpClientTest::connectFresh(AcpClient &client, bool openSession) {
    QSignalSpy connected(&client, &AcpClient::connected);
    QSignalSpy ready(&client, &AcpClient::ready);
    QVERIFY(client.launch());
    QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 10000);
    if (openSession) {
        QVERIFY(client.newSession(m_dir->path()));
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    }
}

void AcpClientTest::resolvesExecutableFromPathAndNegotiates() {
    REQUIRE_PYTHON();
    QVERIFY(writeWrapper("1.0.0", "full"));
    AcpClient client;
    connectFresh(client, false);
    QCOMPARE(client.resolvedExecutable(), m_dir->filePath("hermes"));
    QCOMPARE(client.agentName(), QString("hermes-agent"));
    QCOMPARE(client.agentVersion(), QString("1.0.0"));
    QCOMPARE(client.agentProtocolVersion(), 1);
    QVERIFY(client.canListSessions());
    QVERIFY(client.canResumeSessions());
    // initialize carried our client info and the capabilities we support.
    const auto log = agentLog();
    QVERIFY(!log.isEmpty());
    const auto init = log.first();
    QCOMPARE(init.value("method").toString(), QString("initialize"));
    QCOMPARE(init.value("params").toObject().value("clientCapabilities").toObject()
                 .value("terminal").toBool(true), false);
    QVERIFY(!init.value("params").toObject().value("clientInfo").toObject().value("name").toString().isEmpty());
}

void AcpClientTest::explicitAndEnvExecutableWin() {
    QCOMPARE(AcpClient::resolveExecutable("/opt/custom/hermes"), QString("/opt/custom/hermes"));
    qputenv("HERMES_BIN", "/env/hermes");
    QCOMPARE(AcpClient::resolveExecutable(), QString("/env/hermes"));
    QCOMPARE(AcpClient::resolveExecutable("/explicit/hermes"), QString("/explicit/hermes"));
    qunsetenv("HERMES_BIN");
}

void AcpClientTest::agentRequestIsNotTreatedAsResponse() {
    REQUIRE_PYTHON();
    QVERIFY(writeWrapper("1.0.0", "full"));
    AcpClient client;
    connectFresh(client);
    QSignalSpy connected(&client, &AcpClient::connected);
    QSignalSpy perm(&client, &AcpClient::permissionRequested);
    QSignalSpy done(&client, &AcpClient::turnCompleted);
    QSignalSpy chunks(&client, &AcpClient::messageChunk);

    QVERIFY(client.sendPrompt("permission"));
    QTRY_COMPARE_WITH_TIMEOUT(perm.count(), 1, 5000);
    // The request shared the prompt's id but must not be read as its reply.
    QCOMPARE(connected.count(), 0);
    QCOMPARE(done.count(), 0);
    QVERIFY(client.isBusy());

    const auto req = perm.first().at(0).value<AcpClient::PermissionRequest>();
    QCOMPARE(req.title, QString("rm -rf build"));
    QCOMPARE(req.detail, QString("rm -rf build"));
    QCOMPARE(req.options.size(), 4);  // includes an option kind we don't know
    QCOMPARE(client.pendingPermissions().size(), 1);

    // The unsupported fs/read_text_file request got a JSON-RPC error back.
    bool sawMethodNotFound = false;
    for (const auto &m : agentLog()) {
        if (m.value("id").toString() == "fs-1" && m.value("error").toObject().value("code").toInt() == -32601)
            sawMethodNotFound = true;
    }
    QVERIFY(sawMethodNotFound);

    // Only offered option ids are accepted; the id is echoed verbatim.
    QVERIFY(!client.respondPermission(req.key, "allow_once"));
    QVERIFY(client.respondPermission(req.key, "opt-yes-7f3"));
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 5000);
    QVERIFY(!client.isBusy());
    QVERIFY(client.pendingPermissions().isEmpty());
    QStringList texts;
    for (const auto &c : chunks) texts << c.at(0).toString();
    QVERIFY2(texts.contains("permission:selected/opt-yes-7f3"), qPrintable(texts.join('|')));
    QVERIFY(!client.respondPermission(req.key, "opt-yes-7f3"));  // already answered
}

void AcpClientTest::permissionCancelRoundTrip() {
    REQUIRE_PYTHON();
    QVERIFY(writeWrapper("1.0.0", "full"));
    AcpClient client;
    connectFresh(client);
    QSignalSpy perm(&client, &AcpClient::permissionRequested);
    QSignalSpy done(&client, &AcpClient::turnCompleted);
    QSignalSpy chunks(&client, &AcpClient::messageChunk);
    QVERIFY(client.sendPrompt("permission"));
    QTRY_COMPARE_WITH_TIMEOUT(perm.count(), 1, 5000);
    const auto req = perm.first().at(0).value<AcpClient::PermissionRequest>();
    QVERIFY(client.cancelPermission(req.key));
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 5000);
    QCOMPARE(chunks.last().at(0).toString(), QString("permission:cancelled/"));
}

void AcpClientTest::stopSessionCancelsTurnAndPermission() {
    REQUIRE_PYTHON();
    QVERIFY(writeWrapper("1.0.0", "full"));
    AcpClient client;
    connectFresh(client);
    QSignalSpy perm(&client, &AcpClient::permissionRequested);
    QSignalSpy permGone(&client, &AcpClient::permissionCancelled);
    QVERIFY(client.sendPrompt("permission"));
    QTRY_COMPARE_WITH_TIMEOUT(perm.count(), 1, 5000);
    client.stopSession();
    QCOMPARE(permGone.count(), 1);
    QVERIFY(!client.isBusy());
    QTRY_VERIFY_WITH_TIMEOUT(logHasMethod("session/cancel"), 5000);
    bool sawCancelledOutcome = false;
    for (const auto &m : agentLog()) {
        if (m.value("result").toObject().value("outcome").toObject().value("outcome").toString() == "cancelled")
            sawCancelledOutcome = true;
    }
    QVERIFY(sawCancelledOutcome);
}

void AcpClientTest::promptErrorLeavesClientUsable() {
    REQUIRE_PYTHON();
    QVERIFY(writeWrapper("1.0.0", "full"));
    AcpClient client;
    connectFresh(client);
    QSignalSpy failed(&client, &AcpClient::turnFailed);
    QSignalSpy done(&client, &AcpClient::turnCompleted);
    QVERIFY(client.sendPrompt("fail"));
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
    QVERIFY(failed.first().at(0).toString().contains("provider exploded"));
    QVERIFY(!client.isBusy());
    QVERIFY(client.isReady());
    QVERIFY(client.sendPrompt("hello"));
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 5000);
}

void AcpClientTest::relaunchWorksAfterAgentExit() {
    REQUIRE_PYTHON();
    // Regression for the audit probe: launch() returned false forever after
    // the agent process exited.
    AcpClient client;
    client.setAutoRestart(false);
    QSignalSpy exited(&client, &AcpClient::processExited);
    QVERIFY(client.launch(QStandardPaths::findExecutable("true")));
    QTRY_COMPARE_WITH_TIMEOUT(exited.count(), 1, 5000);
    QVERIFY(writeWrapper("1.0.0", "full"));
    QSignalSpy connected(&client, &AcpClient::connected);
    QVERIFY(client.launch());
    QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 10000);
}

void AcpClientTest::agentUpdateAndRestartRestoresSession() {
    REQUIRE_PYTHON();
    QVERIFY(writeWrapper("1.0.0", "full"));
    AcpClient client;
    client.setRestartBackoff(50, 5);
    connectFresh(client);
    const QString sid = client.sessionId();
    QVERIFY(!sid.isEmpty());

    QSignalSpy connected(&client, &AcpClient::connected);
    QSignalSpy failed(&client, &AcpClient::turnFailed);
    QSignalSpy exited(&client, &AcpClient::processExited);
    QSignalSpy restarting(&client, &AcpClient::agentRestarting);
    QSignalSpy versionChanged(&client, &AcpClient::agentVersionChanged);
    QSignalSpy restored(&client, &AcpClient::sessionRestored);
    QSignalSpy chunks(&client, &AcpClient::messageChunk);
    QSignalSpy done(&client, &AcpClient::turnCompleted);

    // `hermes update` swaps the install under the same launcher path...
    QVERIFY(writeWrapper("1.1.0", "full"));
    // ...and the running agent goes away mid-turn.
    QVERIFY(client.sendPrompt("crash"));
    QTRY_COMPARE_WITH_TIMEOUT(exited.count(), 1, 5000);
    QCOMPARE(failed.count(), 1);
    QVERIFY(failed.first().at(0).toString().contains("not replayed"));
    QVERIFY(!client.isBusy());
    QVERIFY(restarting.count() >= 1);

    // The client relaunches through PATH, renegotiates, and re-opens the session.
    QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(restored.count(), 1, 10000);
    QCOMPARE(versionChanged.count(), 1);
    QCOMPARE(versionChanged.first().at(0).toString(), QString("1.0.0"));
    QCOMPARE(versionChanged.first().at(1).toString(), QString("1.1.0"));
    QCOMPARE(client.agentVersion(), QString("1.1.0"));
    QCOMPARE(client.sessionId(), sid);
    QCOMPARE(restored.first().at(0).toString(), sid);
    bool resumedSameId = false;
    for (const auto &m : agentLog()) {
        if (m.value("method").toString() == "session/resume"
            && m.value("params").toObject().value("sessionId").toString() == sid)
            resumedSameId = true;
    }
    QVERIFY(resumedSameId);

    // The interrupted turn was not replayed; a new prompt runs on the new agent.
    QVERIFY(client.sendPrompt("again"));
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 5000);
    QCOMPARE(chunks.last().at(0).toString(), QString("echo:again@1.1.0"));
}

void AcpClientTest::manualRestartRestoresSession() {
    REQUIRE_PYTHON();
    QVERIFY(writeWrapper("1.0.0", "full"));
    AcpClient client;
    connectFresh(client);
    const QString sid = client.sessionId();
    QVERIFY(writeWrapper("2.0.0", "full"));
    QSignalSpy restored(&client, &AcpClient::sessionRestored);
    QSignalSpy versionChanged(&client, &AcpClient::agentVersionChanged);
    client.restartAgent();
    QTRY_COMPARE_WITH_TIMEOUT(restored.count(), 1, 10000);
    QCOMPARE(client.sessionId(), sid);
    QCOMPARE(versionChanged.count(), 1);
}

void AcpClientTest::restartGivesUpAfterMaxAttempts() {
    REQUIRE_PYTHON();
    QVERIFY(writeWrapper("1.0.0", "full"));
    AcpClient client;
    client.setRestartBackoff(20, 2);
    connectFresh(client, false);
    QSignalSpy gaveUp(&client, &AcpClient::agentRestartFailed);
    QSignalSpy restarting(&client, &AcpClient::agentRestarting);
    // A broken install mid-update: the launcher exits straight away.
    QVERIFY(writeWrapper("", "", "exit 1\n"));
    client.restartAgent();
    QTRY_COMPARE_WITH_TIMEOUT(gaveUp.count(), 1, 10000);
    QCOMPARE(restarting.count(), 2);
    // And it recovers once the install is fixed.
    QVERIFY(writeWrapper("1.0.1", "full"));
    QSignalSpy connected(&client, &AcpClient::connected);
    QVERIFY(client.launch());
    QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 10000);
}

void AcpClientTest::protocolDriftIsTolerated() {
    REQUIRE_PYTHON();
    QVERIFY(writeWrapper("9.9.9", "drift"));
    AcpClient client;
    QSignalSpy errors(&client, &AcpClient::errorOccurred);
    connectFresh(client);
    QCOMPARE(client.agentProtocolVersion(), 2);
    QVERIFY(client.agentCapabilities().contains("futureCapability"));
    // list isn't advertised: answered locally, never sent.
    QVERIFY(!client.canListSessions());
    QSignalSpy listed(&client, &AcpClient::sessionsListed);
    QVERIFY(client.listSessions(m_dir->path()));
    QTRY_COMPARE_WITH_TIMEOUT(listed.count(), 1, 2000);
    QVERIFY(!logHasMethod("session/list"));

    QSignalSpy chunks(&client, &AcpClient::messageChunk);
    QSignalSpy usage(&client, &AcpClient::contextUsage);
    QSignalSpy done(&client, &AcpClient::turnCompleted);
    QVERIFY(client.sendPrompt("drift"));
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 5000);
    QCOMPARE(chunks.count(), 1);
    QCOMPARE(chunks.first().at(0).toString(), QString("ok"));
    QCOMPARE(usage.count(), 1);
    QVERIFY(usage.first().at(0).toInt() > 0);
    QCOMPARE(errors.count(), 0);
}

void AcpClientTest::resumeFallsBackToLoadSession() {
    REQUIRE_PYTHON();
    QVERIFY(writeWrapper("1.0.0", "loadonly"));
    AcpClient client;
    connectFresh(client, false);
    QSignalSpy ready(&client, &AcpClient::ready);
    QSignalSpy chunks(&client, &AcpClient::messageChunk);
    QVERIFY(client.resumeSession("S-old", m_dir->path()));
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 5000);
    QCOMPARE(client.sessionId(), QString("S-old"));
    QVERIFY(logHasMethod("session/load"));
    QVERIFY(!logHasMethod("session/resume"));
    QCOMPARE(chunks.count(), 1);  // history replayed before the response
}

void AcpClientTest::legacyAgentWithoutCapabilities() {
    REQUIRE_PYTHON();
    QVERIFY(writeWrapper("", "legacy"));
    AcpClient client;
    connectFresh(client);
    QCOMPARE(client.agentVersion(), QString());
    QSignalSpy listed(&client, &AcpClient::sessionsListed);
    QVERIFY(client.listSessions(m_dir->path()));
    QTRY_COMPARE_WITH_TIMEOUT(listed.count(), 1, 5000);
    QVERIFY(logHasMethod("session/list"));
}

void AcpClientTest::staleUpdatesAfterStopAreDropped() {
    REQUIRE_PYTHON();
    QVERIFY(writeWrapper("1.0.0", "full"));
    AcpClient client;
    connectFresh(client);
    QSignalSpy chunks(&client, &AcpClient::messageChunk);
    QSignalSpy done(&client, &AcpClient::turnCompleted);
    QSignalSpy failed(&client, &AcpClient::turnFailed);
    QVERIFY(client.sendPrompt("slow"));
    QTest::qWait(200);
    client.stopSession();
    QTRY_VERIFY_WITH_TIMEOUT(logHasMethod("session/cancel"), 5000);
    QTest::qWait(500);
    QCOMPARE(chunks.count(), 0);  // the cancelled turn's late chunk is dropped
    QCOMPARE(done.count(), 0);    // and so is its response
    QCOMPARE(failed.count(), 0);
}

void AcpClientTest::slashCommandsClearedOnReset() {
    REQUIRE_PYTHON();
    QVERIFY(writeWrapper("1.0.0", "full"));
    AcpClient client;
    QSignalSpy cmds(&client, &AcpClient::commandsUpdated);
    connectFresh(client);
    QTRY_VERIFY_WITH_TIMEOUT(!client.slashCommands().isEmpty(), 5000);
    const int before = cmds.count();
    client.stopSession();
    QVERIFY(client.slashCommands().isEmpty());
    QCOMPARE(cmds.count(), before + 1);
}

void AcpClientTest::sendPromptWithoutSessionFails() {
    REQUIRE_PYTHON();
    QVERIFY(writeWrapper("1.0.0", "full"));
    AcpClient client;
    connectFresh(client, false);
    QVERIFY(!client.sendPrompt("/model"));
    QVERIFY(!client.isBusy());
}

QTEST_MAIN(AcpClientTest)
#include "test_acpclient.moc"
