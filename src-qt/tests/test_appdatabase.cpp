#include <QtTest/QtTest>

#include "backend/appdatabase.h"

#include <QTemporaryDir>

class TestAppDatabase : public QObject {
    Q_OBJECT

private slots:
    void persistsPreferences();
    void groupsSessionsAndEventsByProject();
    void marksAbandonedRunningSessions();
};

void TestAppDatabase::persistsPreferences() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.path() + "/consiglio.sqlite";
    {
        AppDatabase database(path);
        QVERIFY2(database.isOpen(), qPrintable(database.lastError()));
        QVERIFY(database.setPreference("userName", "Dude"));
        QVERIFY(database.setPreference("windowGeometry", QByteArray("geometry")));
    }
    AppDatabase reopened(path);
    QCOMPARE(reopened.preference("userName").toString(), QString("Dude"));
    QCOMPARE(reopened.preference("windowGeometry").toByteArray(), QByteArray("geometry"));
}

void TestAppDatabase::groupsSessionsAndEventsByProject() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    AppDatabase database(directory.path() + "/consiglio.sqlite");
    const QString projectId = database.ensureProject(directory.path(), "Test Project");
    QVERIFY(!projectId.isEmpty());

    SessionRecord session;
    session.id = "session-1";
    session.projectId = projectId;
    session.provider = "codex";
    session.status = "running";
    session.repository = directory.path();
    session.permissionMode = "workspace-write";
    session.preferredName = "Dude";
    session.startedAt = 100;
    session.lastActivity = 100;
    QVERIFY(database.upsertSession(session));

    EventModel::EventItem event;
    event.type = EventModel::AssistantMessage;
    event.sessionId = session.id;
    event.content = "Hello, Dude.";
    event.timestamp = 200;
    QVERIFY(database.addEvent(event));

    QCOMPARE(database.projects().size(), 1);
    QCOMPARE(database.projects().first().sessionCount, 1);
    QCOMPARE(database.sessions(projectId).size(), 1);
    QCOMPARE(database.sessions(projectId).first().preferredName, QString("Dude"));
    QCOMPARE(database.events(session.id).size(), 1);
    QCOMPARE(database.events(session.id).first().content, QString("Hello, Dude."));
    QVERIFY(database.clearEvents(session.id));
    QVERIFY(database.events(session.id).isEmpty());
}

void TestAppDatabase::marksAbandonedRunningSessions() {
    AppDatabase database(":memory:");
    SessionRecord session;
    session.id = "abandoned";
    session.provider = "codex";
    session.status = "running";
    session.startedAt = 10;
    session.lastActivity = 10;
    QVERIFY(database.upsertSession(session));
    QVERIFY(database.markRunningSessionsInterrupted(20));
    QCOMPARE(database.sessions().first().status, QString("interrupted"));
    QCOMPARE(database.sessions().first().lastActivity, 20);
}

QTEST_MAIN(TestAppDatabase)
#include "test_appdatabase.moc"
