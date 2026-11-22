// Host profiles: local by default, saved SSH hosts, a single active host, and
// the migration from the one-connection settings the previous version wrote.
#include "hermesremote.h"
#include "hostmanager.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QtTest>

class HostManagerTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        m_dir.reset(new QTemporaryDir);
        QVERIFY(m_dir->isValid());
        qputenv("XDG_CONFIG_HOME", m_dir->filePath("config").toUtf8());
        QCoreApplication::setOrganizationName("AnivizaTest");
        QCoreApplication::setApplicationName("ConsiglioHostManagerTest");
    }
    void init() {
        HostManager::clearAll();
        HermesRemote::clear();
    }

    void localIsAlwaysThereAndDefault() {
        const auto profiles = HostManager::profiles();
        QCOMPARE(profiles.size(), 1);
        QVERIFY(profiles.first().isLocal());
        QCOMPARE(profiles.first().displayName(), QString("This machine"));
        QCOMPARE(HostManager::active().id, QString(HostManager::localId()));
        QCOMPARE(HostManager::activeId(), QString(HostManager::localId()));
    }

    void addSelectRemove() {
        const QString a = HostManager::addProfile(QStringLiteral("  spark  "));
        const QString b = HostManager::addProfile(QStringLiteral("rick@lefty"));
        QVERIFY(!a.isEmpty());
        QVERIFY(a != b);
        QCOMPARE(HostManager::profiles().size(), 3);          // local + two

        HostProfile pa = HostManager::profile(a);
        QCOMPARE(pa.destination, QString("spark"));
        QCOMPARE(pa.displayName(), QString("spark"));         // label defaults to the destination
        QCOMPARE(pa.launcher, QString());                     // empty means "hermes"

        HostManager::setActiveId(a);
        QCOMPARE(HostManager::activeId(), a);
        QCOMPARE(HostManager::active().destination, QString("spark"));

        // Editing persists; the label is preserved when it differs.
        pa.label = QStringLiteral("spark (dev)");
        pa.options = QStringLiteral("-p 2222");
        pa.launcher = QStringLiteral("/opt/hermes/bin/hermes");
        HostManager::updateProfile(pa);
        const HostProfile again = HostManager::profile(a);
        QCOMPARE(again.label, QString("spark (dev)"));
        QCOMPARE(again.options, QString("-p 2222"));
        QCOMPARE(again.launcher, QString("/opt/hermes/bin/hermes"));

        // Removing the active host falls back to local, not to a dangling id.
        HostManager::removeProfile(a);
        QCOMPARE(HostManager::profiles().size(), 2);
        QCOMPARE(HostManager::activeId(), QString(HostManager::localId()));
        HostManager::removeProfile(b);
        QCOMPARE(HostManager::profiles().size(), 1);
    }

    void localCannotBeRemovedOrEdited() {
        HostProfile local = HostManager::localProfile();
        HostManager::updateProfile(local);      // must not create a stored profile
        HostManager::removeProfile(local.id);
        QCOMPARE(HostManager::profiles().size(), 1);
    }

    void aStaleActiveIdFallsBackToLocal() {
        const QString id = HostManager::addProfile(QStringLiteral("gone"));
        HostManager::setActiveId(id);
        QCOMPARE(HostManager::activeId(), id);
        // Remove it behind the manager's back (a hand-edited config, say).
        HostManager::removeProfile(id);
        QCOMPARE(HostManager::activeId(), QString(HostManager::localId()));
        QCOMPARE(HostManager::active().isLocal(), true);
    }

    // The one-connection settings of #80 turn into one profile, once.
    void migratesTheSingleConnectionSettings() {
        HermesRemote::setEnabled(true);
        HermesRemote::setDestination(QStringLiteral("spark"));
        HermesRemote::setOptions(QStringLiteral("-p 2222"));
        HermesRemote::setLauncher(QStringLiteral("/opt/hermes/bin/hermes"));

        HostManager::migrateLegacy();
        const auto profiles = HostManager::profiles();
        QCOMPARE(profiles.size(), 2);
        const HostProfile migrated = profiles.at(1);
        QCOMPARE(migrated.destination, QString("spark"));
        QCOMPARE(migrated.options, QString("-p 2222"));
        QCOMPARE(migrated.launcher, QString("/opt/hermes/bin/hermes"));
        QCOMPARE(HostManager::activeId(), migrated.id);

        // A second call must not duplicate it.
        HostManager::migrateLegacy();
        QCOMPARE(HostManager::profiles().size(), 2);
    }

    void migratesNothingWhenRemoteWasOff() {
        HermesRemote::setEnabled(false);
        HermesRemote::setDestination(QStringLiteral("spark"));
        HostManager::migrateLegacy();
        QCOMPARE(HostManager::profiles().size(), 1);
        QCOMPARE(HostManager::active().isLocal(), true);
    }

private:
    QScopedPointer<QTemporaryDir> m_dir;
};

QTEST_MAIN(HostManagerTest)
#include "test_hostmanager.moc"
