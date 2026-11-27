// Finding Hermes on another machine: the ssh argv is argv-only, the probe
// parses what the remote login shell reports, and the preferences round-trip.
#include "hermesremote.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

class HermesRemoteTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        m_dir.reset(new QTemporaryDir);
        QVERIFY(m_dir->isValid());
        qputenv("XDG_CONFIG_HOME", m_dir->filePath("config").toUtf8());
        QCoreApplication::setOrganizationName("AnivizaTest");
        QCoreApplication::setApplicationName("ConsiglioHermesRemoteTest");
        HermesRemote::clear();
    }

    void cleanup() { HermesRemote::clear(); }

    // The probe must be argv: flags, destination and ONE remote command token.
    void probeIsArgvOnly() {
        const QStringList argv = HermesRemote::probeArgv(
            QStringLiteral("myhost.example"), {QStringLiteral("-p"), QStringLiteral("2222")});
        QVERIFY(argv.contains(QStringLiteral("-T")));
        QVERIFY(argv.contains(QStringLiteral("BatchMode=yes")));
        QVERIFY(argv.contains(QStringLiteral("ConnectTimeout=10")));
        QVERIFY(argv.contains(QStringLiteral("-p")));
        QVERIFY(argv.contains(QStringLiteral("2222")));
        QVERIFY(argv.contains(QStringLiteral("--")));
        const int dest = argv.indexOf(QStringLiteral("myhost.example"));
        QVERIFY(dest > 0);
        QCOMPARE(dest + 1, argv.size() - 1);   // exactly one token follows the destination
        const QString command = argv.last();
        QVERIFY(command.startsWith(QStringLiteral("sh -lc '")));
        QVERIFY(command.contains(QStringLiteral("command -v hermes")));
        QVERIFY(command.contains(QStringLiteral("hermes --version")));
    }

    void parsesTheRemoteAnswer() {
        const QByteArray out = "PATH=/home/user/.local/bin/hermes\n"
                               "VER=Hermes Agent v0.23.2 (2026.9.24) · stable · local abc123\n";
        const RemoteProbe p = HermesRemote::parse(out, QByteArray(), 0);
        QVERIFY(p.ok);
        QCOMPARE(p.executable, QString("/home/user/.local/bin/hermes"));
        QVERIFY(p.version.startsWith("Hermes Agent v0.23.2"));
        QVERIFY(p.text().contains("/home/user/.local/bin/hermes"));
    }

    void reportsAMissingRemoteHermes() {
        const RemoteProbe p = HermesRemote::parse(QByteArray(), "sh: 1: hermes: not found\n", 127);
        QVERIFY(!p.ok);
        QVERIFY(p.error.contains("not found"));
    }

    void surfacesSshsOwnError() {
        const RemoteProbe p = HermesRemote::parse(
            QByteArray(), "Permission denied (publickey).\n", 255);
        QVERIFY(!p.ok);
        QCOMPARE(p.error, QString("Permission denied (publickey)."));
    }

    // End to end through a stand-in ssh, the same trick the transport test uses.
    void probeAsyncUsesSshAndReports() {
        const QString shim = m_dir->filePath("ssh");
        QFile f(shim);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("#!/bin/sh\n"
                "printf 'PATH=/opt/hermes/bin/hermes\\n'\n"
                "printf 'VER=Hermes Agent v9.9.9 (test)\\n'\n");
        f.close();
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

        const QByteArray savedPath = qgetenv("PATH");
        qputenv("PATH", QFileInfo(shim).absolutePath().toUtf8() + ":" + savedPath);
        RemoteProbe got;
        bool called = false;
        HermesRemote::probeAsync(this, QStringLiteral("fake-host"), {}, [&](const RemoteProbe &p) {
            got = p;
            called = true;
        }, 8000);
        QTRY_VERIFY_WITH_TIMEOUT(called, 9000);
        qputenv("PATH", savedPath);
        QVERIFY(got.ok);
        QCOMPARE(got.executable, QString("/opt/hermes/bin/hermes"));
        QVERIFY(got.version.contains("v9.9.9"));
    }

    void preferencesRoundTrip() {
        QVERIFY(!HermesRemote::enabled());
        HermesRemote::setEnabled(true);
        HermesRemote::setDestination(QStringLiteral("  myhost  "));
        HermesRemote::setOptions(QStringLiteral("-p 2222 -i \"/tmp/my key\""));
        HermesRemote::setLauncher(QStringLiteral("/opt/hermes/bin/hermes"));

        QVERIFY(HermesRemote::enabled());
        QCOMPARE(HermesRemote::destination(), QString("myhost"));
        const QStringList opts = HermesRemote::options();
        QCOMPARE(opts.size(), 4);
        QCOMPARE(opts.first(), QString("-p"));
        QCOMPARE(opts.last(), QString("/tmp/my key"));   // quoted token survives
        QCOMPARE(HermesRemote::launcher(), QString("/opt/hermes/bin/hermes"));

        HermesRemote::clear();
        QVERIFY(!HermesRemote::enabled());
        QVERIFY(HermesRemote::destination().isEmpty());
        QVERIFY(HermesRemote::options().isEmpty());
        QCOMPARE(HermesRemote::launcher(), QString("hermes"));
    }

private:
    QScopedPointer<QTemporaryDir> m_dir;
};

QTEST_MAIN(HermesRemoteTest)
#include "test_hermesremote.moc"
