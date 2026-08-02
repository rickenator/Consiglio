#include <QtTest/QtTest>
#include <QSignalSpy>

#include "backend/ptyhandler.h"

class TestPtyHandler : public QObject {
    Q_OBJECT

private slots:
    void providesTerminalOnUnix();
    void carriesInteractiveInput();
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

QTEST_MAIN(TestPtyHandler)
#include "test_ptyhandler.moc"
