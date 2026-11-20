#include <QtTest>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>

#include "appsandbox.h"

class AppSandboxTest : public QObject {
    Q_OBJECT
    QTemporaryDir m_real;
    QString m_realHome;

    void plantRealHermes() {
        m_realHome = QDir(m_real.path()).filePath("real-home");
        QDir().mkpath(m_realHome + "/.hermes");
        QFile f(m_realHome + "/.hermes/config.yaml");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("model: keep-me\n");
        f.close();
        QFile s(m_realHome + "/.hermes/state.db");
        QVERIFY(s.open(QIODevice::WriteOnly));
        s.write("sessions-belonging-to-the-user");
        s.close();
    }

    QByteArray snap(const QString &path) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return {};
        return f.readAll();
    }

private slots:
    void init() {
        QVERIFY(m_real.isValid());
        AppSandbox::reset();
        plantRealHermes();
    }
    void cleanup() { AppSandbox::reset(); }

    void parseFlagAndEqualsAndEnv() {
        QString dir;
        QStringList rest;
        QVERIFY(!AppSandbox::parse({"consiglio"}, &dir, &rest));
        QVERIFY(AppSandbox::parse({"consiglio", "--sandbox"}, &dir, &rest));
        QVERIFY(dir.isEmpty());
        QCOMPARE(rest, QStringList{"consiglio"});
        QVERIFY(AppSandbox::parse({"consiglio", "--sandbox=/tmp/iso", "--hermes", "h"}, &dir, &rest));
        QCOMPARE(dir, QString("/tmp/iso"));
        QCOMPARE(rest, (QStringList{"consiglio", "--hermes", "h"}));
        QVERIFY(AppSandbox::parse({"consiglio", "--sandbox", "/tmp/other"}, &dir, &rest));
        QCOMPARE(dir, QString("/tmp/other"));

        const QByteArray previous = qgetenv("CONSIGLIO_SANDBOX");
        qputenv("CONSIGLIO_SANDBOX", "/tmp/from-env");
        QVERIFY(AppSandbox::parse({"consiglio"}, &dir, &rest));
        QCOMPARE(dir, QString("/tmp/from-env"));
        qputenv("CONSIGLIO_SANDBOX", "1");
        QVERIFY(AppSandbox::parse({"consiglio"}, &dir, &rest));
        QVERIFY(dir.isEmpty());
        // The flag wins over the environment.
        QVERIFY(AppSandbox::parse({"consiglio", "--sandbox=/tmp/flag"}, &dir, &rest));
        QCOMPARE(dir, QString("/tmp/flag"));
        if (previous.isEmpty()) qunsetenv("CONSIGLIO_SANDBOX");
        else qputenv("CONSIGLIO_SANDBOX", previous);
    }

    void activateIsolatesSettingsAndHermesHomeWithoutTouchingRealHome() {
        const QString config = m_realHome + "/.hermes/config.yaml";
        const QString state = m_realHome + "/.hermes/state.db";
        const QByteArray beforeCfg = snap(config);
        const QByteArray beforeState = snap(state);
        const QString sandboxRoot = QDir(m_real.path()).filePath("sandbox");
        QString err;
        QVERIFY2(AppSandbox::activate(sandboxRoot, &err), qPrintable(err));
        QVERIFY(AppSandbox::isActive());
        QCOMPARE(AppSandbox::hermesHome(), QDir(sandboxRoot).filePath(".hermes"));
        QCOMPARE(qEnvironmentVariable("HERMES_HOME"), AppSandbox::hermesHome());
        QVERIFY(QDir(AppSandbox::hermesHome()).exists());
        // An empty sandbox must not inherit the real store.
        QVERIFY(!QFile::exists(AppSandbox::hermesHome() + "/state.db"));
        QVERIFY(!QFile::exists(AppSandbox::hermesHome() + "/config.yaml"));

        QSettings s;
        s.setValue("session/workspace", "/tmp/sandbox-ws");
        s.sync();
        const QString ini = s.fileName();
        QVERIFY(ini.startsWith(AppSandbox::consiglioDir()));
        QVERIFY(!ini.contains(m_realHome));

        QCOMPARE(snap(config), beforeCfg);
        QCOMPARE(snap(state), beforeState);
        QVERIFY2(!QFile::exists(QDir(m_realHome).filePath("consiglio")), "sandbox must not write under the real home");
    }
};

QTEST_MAIN(AppSandboxTest)
#include "test_appsandbox.moc"
