#include <QtTest>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "sessioncatalog.h"

class SessionCatalogTest : public QObject {
    Q_OBJECT

    QJsonObject listResult(const QByteArray &json) {
        return QJsonDocument::fromJson(json).object();
    }

    void writeDb(const QString &dir, const QList<QVariantMap> &rows, const QStringList &extraCols = {}) {
        const QString path = QDir(dir).filePath("state.db");
        const QString name = QStringLiteral("test-write-%1").arg(quintptr(this), 0, 16);
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", name);
        db.setDatabaseName(path);
        QVERIFY(db.open());
        QSqlQuery q(db);
        QString cols = QStringLiteral("id TEXT, title TEXT, cwd TEXT, last_activity_at REAL, "
                                      "started_at REAL, message_count INTEGER, hidden INTEGER, archived INTEGER");
        for (const QString &c : extraCols) cols += QStringLiteral(", %1 TEXT").arg(c);
        QVERIFY(q.exec(QStringLiteral("CREATE TABLE sessions (%1)").arg(cols)));
        for (const auto &r : rows) {
            QVERIFY(q.prepare(QStringLiteral(
                "INSERT INTO sessions (id, title, cwd, last_activity_at, started_at, message_count, hidden, archived) "
                "VALUES (:id,:title,:cwd,:la,:st,:mc,:h,:a)")));
            q.bindValue(":id", r.value("id"));
            q.bindValue(":title", r.value("title"));
            q.bindValue(":cwd", r.value("cwd"));
            q.bindValue(":la", r.value("last_activity_at"));
            q.bindValue(":st", r.value("started_at"));
            q.bindValue(":mc", r.value("message_count", 2));
            q.bindValue(":h", r.value("hidden", 0));
            q.bindValue(":a", r.value("archived", 0));
            QVERIFY(q.exec());
        }
        db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(name);
    }

private slots:
    void parsesCamelCaseAndSnakeCase() {
        QString cursor;
        auto sessions = SessionCatalog::parseListResult(listResult(R"({
            "sessions": [
                {"sessionId": "A", "title": "Camel", "cwd": "/c", "updatedAt": "2026-11-20T12:00:00Z"},
                {"session_id": "B", "title": "Snake", "cwd": "/s", "updated_at": "2026-08-19T12:00:00Z"}
            ],
            "next_cursor": "B"
        })"), &cursor);
        QCOMPARE(sessions.size(), 2);
        QCOMPARE(sessions[0].id, QString("A"));
        QCOMPARE(sessions[1].id, QString("B"));
        QCOMPARE(cursor, QString("B"));
        QVERIFY(sessions[0].updated.isValid());
        QVERIFY(sessions[1].updated.isValid());
        QVERIFY(sessions[0].updated > sessions[1].updated);
    }

    void parsesUnixTimestamp() {
        auto sessions = SessionCatalog::parseListResult(listResult(
            R"({"sessions":[{"id":"U","title":"Unix","updated_at":1795147779.97}]})"));
        QCOMPARE(sessions.size(), 1);
        QVERIFY(sessions[0].updated.isValid());
        QCOMPARE(sessions[0].updated.date(), QDate(2026, 11, 20));
    }

    void skipsEntriesWithoutId() {
        auto sessions = SessionCatalog::parseListResult(listResult(
            R"({"sessions":[{"title":"no id"},{"sessionId":"ok"}]})"));
        QCOMPARE(sessions.size(), 1);
        QCOMPARE(sessions[0].id, QString("ok"));
    }

    void sortsNewestFirst() {
        QList<SessionCatalog::Entry> s;
        auto add = [&](const char *id, const char *when) {
            SessionCatalog::Entry e;
            e.id = QLatin1String(id);
            e.updatedAt = QLatin1String(when);
            e.updated = SessionCatalog::parseTime(QLatin1String(when));
            s.append(e);
        };
        add("aug", "2026-08-19T10:00:00Z");
        add("nov", "2026-11-20T10:00:00Z");
        add("unknown", "");
        SessionCatalog::sortNewestFirst(s);
        QCOMPARE(s[0].id, QString("nov"));
        QCOMPARE(s[1].id, QString("aug"));
        QCOMPARE(s[2].id, QString("unknown"));
    }

    void storeIsReadOnlyAndSkipsEmptyOrHidden() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        // Recent "local" session, an empty stale acp row, a hidden one, an old one.
        writeDb(tmp.path(), {
            {{"id","recent"},{"title","Today"},{"cwd","/now"},{"last_activity_at",1795202421.0},{"message_count",4}},
            {{"id","empty-acp"},{"title","Empty"},{"cwd","/old"},{"started_at",1692400000.0},{"message_count",0}},
            {{"id","hidden"},{"title","Hidden"},{"cwd","/h"},{"last_activity_at",1795200000.0},{"message_count",3},{"hidden",1}},
            {{"id","aug19"},{"title","August"},{"cwd","/legacy"},{"last_activity_at",1692441600.0},{"message_count",8}},
        });
        QFile beforeFile(tmp.filePath("state.db"));
        QVERIFY(beforeFile.open(QIODevice::ReadOnly));
        const QByteArray before = beforeFile.readAll();
        beforeFile.close();
        QString err;
        auto rows = SessionCatalog::readStore(tmp.path(), &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QStringList ids;
        for (const auto &e : rows) ids << e.id;
        QVERIFY(ids.contains("recent"));
        QVERIFY(ids.contains("aug19"));
        QVERIFY(!ids.contains("empty-acp"));
        QVERIFY(!ids.contains("hidden"));
        QFile f(tmp.filePath("state.db"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), before);
    }

    void mergePrefersAcpButAddsStoreOnlySessionsAndSorts() {
        SessionCatalog::Entry acpOld;
        acpOld.id = "aug19";
        acpOld.title = "From ACP";
        acpOld.updated = SessionCatalog::parseTime(QStringLiteral("2026-08-19T10:00:00Z"));
        acpOld.updatedAt = acpOld.updated.toUTC().toString(Qt::ISODate);
        SessionCatalog::Entry storeOld = acpOld;
        storeOld.title = "From store (should lose)";
        SessionCatalog::Entry storeNew;
        storeNew.id = "recent";
        storeNew.title = "Current Hermes";
        storeNew.updated = SessionCatalog::parseTime(QStringLiteral("2026-11-20T10:00:00Z"));
        storeNew.updatedAt = storeNew.updated.toUTC().toString(Qt::ISODate);
        auto merged = SessionCatalog::merge({acpOld}, {storeOld, storeNew});
        QCOMPARE(merged.size(), 2);
        QCOMPARE(merged[0].id, QString("recent"));
        QCOMPARE(merged[1].id, QString("aug19"));
        QCOMPARE(merged[1].title, QString("From ACP"));
    }

    void missingOrCorruptStoreIsTolerated() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QCOMPARE(SessionCatalog::readStore(tmp.path()).size(), 0);
        QFile f(tmp.filePath("state.db"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("not a database");
        f.close();
        QString err;
        QCOMPARE(SessionCatalog::readStore(tmp.path(), &err).size(), 0);
        QVERIFY(!err.isEmpty());
    }

    void defaultHermesHomeHonoursEnv() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QByteArray previous = qgetenv("HERMES_HOME");
        qputenv("HERMES_HOME", tmp.path().toUtf8());
        QCOMPARE(QDir::cleanPath(SessionCatalog::defaultHermesHome()), QDir::cleanPath(tmp.path()));
        if (previous.isEmpty()) qunsetenv("HERMES_HOME");
        else qputenv("HERMES_HOME", previous);
    }
};

QTEST_MAIN(SessionCatalogTest)
#include "test_sessioncatalog.moc"
