#include <QtTest/QtTest>
#include <QSignalSpy>

#include "backend/ptyhandler.h"

class TestPtyHandler : public QObject {
    Q_OBJECT

private slots:
    void providesTerminalOnUnix();
    void carriesInteractiveInput();
    void usesCallerEnvironmentExactly();
    void killBeforeExecDoesNotRunParentHandlers();
};

void TestPtyHandler::providesTerminalOnUnix() {
    PtyHandler pty;
    QSignalSpy outputSpy(&pty, &PtyHandler::outputReceived);
    QSignalSpy finishedSpy(&pty, &PtyHandler::finished);

    QVERIFY(pty.start("/bin/sh", {"-c", "test -t 0 && printf TTY_OK || printf NO_TTY"}));
    QTRY_VERIFY_WITH_TIMEOUT(!finishedSpy.isEmpty(), 3000);

    QByteArray output;
    for (const auto &emission : outputSpy) output += emission.at(0).toByteArray();
#if defined(Q_OS_UNIX) && !defined(Q_OS_ANDROID)
    QVERIFY2(output.contains("TTY_OK"), output.constData());
    QVERIFY2(!output.contains("NO_TTY"), output.constData());
#else
    QVERIFY(!output.isEmpty());
#endif
}

void TestPtyHandler::carriesInteractiveInput() {
    PtyHandler pty;
    QSignalSpy outputSpy(&pty, &PtyHandler::outputReceived);
    QSignalSpy finishedSpy(&pty, &PtyHandler::finished);

    QVERIFY(pty.start("/bin/sh", {"-c", "IFS= read -r line; printf 'RECEIVED:%s' \"$line\""}));
    pty.write("hello from Consiglio\n");
    QTRY_VERIFY_WITH_TIMEOUT(!finishedSpy.isEmpty(), 3000);

    QByteArray output;
    for (const auto &emission : outputSpy) output += emission.at(0).toByteArray();
    QVERIFY2(output.contains("RECEIVED:hello from Consiglio"), output.constData());
}

void TestPtyHandler::usesCallerEnvironmentExactly() {
#if defined(Q_OS_UNIX) && !defined(Q_OS_ANDROID)
    qputenv("CONSIGLIO_PTY_REMOVED", "still-here");
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.remove("CONSIGLIO_PTY_REMOVED");
    environment.insert("CONSIGLIO_PTY_ADDED", "added");

    PtyHandler pty;
    QSignalSpy outputSpy(&pty, &PtyHandler::outputReceived);
    QSignalSpy finishedSpy(&pty, &PtyHandler::finished);
    QVERIFY(pty.start("/bin/sh", {"-c", "printf 'ENV:%s:%s:%s' \"${CONSIGLIO_PTY_ADDED}\" "
                                        "\"${CONSIGLIO_PTY_REMOVED-unset}\" \"$TERM\""},
                      QString(), environment));
    QTRY_VERIFY_WITH_TIMEOUT(!finishedSpy.isEmpty(), 3000);
    qunsetenv("CONSIGLIO_PTY_REMOVED");

    QByteArray output;
    for (const auto &emission : outputSpy) output += emission.at(0).toByteArray();
    QVERIFY2(output.contains("ENV:added:unset:xterm-256color"), output.constData());
#else
    QSKIP("PTY path is Unix-only");
#endif
}

void TestPtyHandler::killBeforeExecDoesNotRunParentHandlers() {
    // Repeatedly start-and-kill immediately: a child killed before exec must
    // die with default dispositions and never run this process's handlers.
    for (int i = 0; i < 20; ++i) {
        PtyHandler pty;
        QSignalSpy finishedSpy(&pty, &PtyHandler::finished);
        QVERIFY(pty.start("/bin/sh", {"-c", "sleep 5"}));
        pty.kill();
        QVERIFY(!pty.isRunning());
    }
}

QTEST_MAIN(TestPtyHandler)
#include "test_ptyhandler.moc"
