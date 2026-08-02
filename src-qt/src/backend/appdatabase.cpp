#include "appdatabase.h"

#include <QDataStream>
#include <QDateTime>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>

AppDatabase::AppDatabase(const QString &databasePath)
    : m_connectionName(QString("consiglio-%1").arg(
          QUuid::createUuid().toString(QUuid::WithoutBraces))) {
    if (databasePath.isEmpty()) {
        const QString dataDirectory = QStandardPaths::writableLocation(
            QStandardPaths::AppDataLocation);
        QDir().mkpath(dataDirectory);
        m_databasePath = dataDirectory + "/consiglio.sqlite";
    } else {
        m_databasePath = databasePath;
        if (databasePath != ":memory:") {
            QDir().mkpath(QFileInfo(databasePath).absolutePath());
        }
    }

    m_db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
    m_db.setDatabaseName(m_databasePath);
    if (!m_db.open()) {
        m_lastError = m_db.lastError().text();
        return;
    }
    QSqlQuery pragma(m_db);
    pragma.exec("PRAGMA foreign_keys = ON");
    pragma.exec("PRAGMA busy_timeout = 5000");
    if (m_databasePath != ":memory:") pragma.exec("PRAGMA journal_mode = WAL");
    if (ensureSchema() && databasePath.isEmpty()) migrateLegacySettings();
}

AppDatabase::~AppDatabase() {
    if (m_db.isValid()) m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool AppDatabase::isOpen() const { return m_db.isOpen(); }
QString AppDatabase::databasePath() const { return m_databasePath; }
QString AppDatabase::lastError() const { return m_lastError; }

bool AppDatabase::ensureSchema() {
    QSqlQuery query(m_db);
    const QStringList statements = {
        "CREATE TABLE IF NOT EXISTS preferences ("
        "key TEXT PRIMARY KEY, value BLOB NOT NULL)",
        "CREATE TABLE IF NOT EXISTS projects ("
        "id TEXT PRIMARY KEY, name TEXT NOT NULL, workspace TEXT NOT NULL UNIQUE, "
        "created_at INTEGER NOT NULL, last_activity INTEGER NOT NULL)",
        "CREATE TABLE IF NOT EXISTS sessions ("
        "id TEXT PRIMARY KEY, provider TEXT NOT NULL, status TEXT NOT NULL, "
        "repository TEXT, branch TEXT, project_id TEXT, permission_mode TEXT, preferred_name TEXT, "
        "started_at INTEGER NOT NULL, last_activity INTEGER NOT NULL)",
        "CREATE INDEX IF NOT EXISTS sessions_activity_idx "
        "ON sessions(last_activity DESC)",
        "CREATE TABLE IF NOT EXISTS events ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, session_id TEXT, type INTEGER NOT NULL, "
        "content TEXT NOT NULL, timestamp INTEGER NOT NULL, command TEXT, working_dir TEXT)",
        "CREATE INDEX IF NOT EXISTS events_session_time_idx "
        "ON events(session_id, timestamp, id)"
    };
    for (const auto &statement : statements) {
        if (!query.exec(statement)) {
            m_lastError = query.lastError().text();
            return false;
        }
    }
    query.exec("PRAGMA table_info(sessions)");
    bool hasProjectId = false;
    while (query.next()) {
        if (query.value(1).toString() == "project_id") hasProjectId = true;
    }
    if (!hasProjectId && !query.exec("ALTER TABLE sessions ADD COLUMN project_id TEXT")) {
        m_lastError = query.lastError().text();
        return false;
    }

    // FTS5 is an optional retrieval index. The app remains usable on SQLite
    // builds without FTS5; the MCP server falls back to LIKE search.
    QSqlQuery ftsCheck(m_db);
    ftsCheck.prepare("SELECT 1 FROM sqlite_master WHERE type='table' AND name='events_fts'");
    const bool ftsExisted = ftsCheck.exec() && ftsCheck.next();
    QSqlQuery fts(m_db);
    if (fts.exec("CREATE VIRTUAL TABLE IF NOT EXISTS events_fts USING fts5("
                 "content, command, content='events', content_rowid='id')")) {
        fts.exec("CREATE TRIGGER IF NOT EXISTS events_fts_insert AFTER INSERT ON events BEGIN "
                 "INSERT INTO events_fts(rowid, content, command) "
                 "VALUES (new.id, new.content, new.command); END");
        fts.exec("CREATE TRIGGER IF NOT EXISTS events_fts_delete AFTER DELETE ON events BEGIN "
                 "INSERT INTO events_fts(events_fts, rowid, content, command) "
                 "VALUES ('delete', old.id, old.content, old.command); END");
        fts.exec("CREATE TRIGGER IF NOT EXISTS events_fts_update AFTER UPDATE ON events BEGIN "
                 "INSERT INTO events_fts(events_fts, rowid, content, command) "
                 "VALUES ('delete', old.id, old.content, old.command); "
                 "INSERT INTO events_fts(rowid, content, command) "
                 "VALUES (new.id, new.content, new.command); END");
        if (!ftsExisted) fts.exec("INSERT INTO events_fts(events_fts) VALUES('rebuild')");
    }
    return true;
}

QByteArray AppDatabase::encodeVariant(const QVariant &value) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_15);
    stream << value;
    return data;
}

QVariant AppDatabase::decodeVariant(const QByteArray &data) {
    QVariant value;
    QByteArray copy = data;
    QDataStream stream(&copy, QIODevice::ReadOnly);
    stream.setVersion(QDataStream::Qt_5_15);
    stream >> value;
    return value;
}

QVariant AppDatabase::preference(const QString &key, const QVariant &fallback) const {
    QSqlQuery query(m_db);
    query.prepare("SELECT value FROM preferences WHERE key = ?");
    query.addBindValue(key);
    if (!query.exec() || !query.next()) return fallback;
    return decodeVariant(query.value(0).toByteArray());
}

bool AppDatabase::containsPreference(const QString &key) const {
    QSqlQuery query(m_db);
    query.prepare("SELECT 1 FROM preferences WHERE key = ?");
    query.addBindValue(key);
    return query.exec() && query.next();
}

bool AppDatabase::setPreference(const QString &key, const QVariant &value) {
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO preferences(key, value) VALUES(?, ?) "
                  "ON CONFLICT(key) DO UPDATE SET value = excluded.value");
    query.addBindValue(key);
    query.addBindValue(encodeVariant(value));
    if (query.exec()) return true;
    m_lastError = query.lastError().text();
    return false;
}

void AppDatabase::migrateLegacySettings() {
    if (preference("database/qsettingsMigrated", false).toBool()) return;
    QSettings legacy("Aniviza", "Consiglio");
    for (const auto &key : legacy.allKeys()) {
        if (!containsPreference(key)) setPreference(key, legacy.value(key));
    }
    setPreference("database/qsettingsMigrated", true);
}

bool AppDatabase::upsertSession(const SessionRecord &record) {
    QSqlQuery query(m_db);
    query.prepare(
        "INSERT INTO sessions(id, provider, status, repository, branch, project_id, permission_mode, "
        "preferred_name, started_at, last_activity) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(id) DO UPDATE SET provider=excluded.provider, status=excluded.status, "
        "repository=excluded.repository, branch=excluded.branch, "
        "project_id=excluded.project_id, permission_mode=excluded.permission_mode, "
        "preferred_name=excluded.preferred_name, "
        "started_at=excluded.started_at, last_activity=excluded.last_activity");
    query.addBindValue(record.id);
    query.addBindValue(record.provider);
    query.addBindValue(record.status);
    query.addBindValue(record.repository);
    query.addBindValue(record.branch);
    query.addBindValue(record.projectId);
    query.addBindValue(record.permissionMode);
    query.addBindValue(record.preferredName);
    query.addBindValue(record.startedAt);
    query.addBindValue(record.lastActivity);
    if (query.exec()) return true;
    m_lastError = query.lastError().text();
    return false;
}

bool AppDatabase::setSessionStatus(const QString &sessionId, const QString &status,
                                   qint64 lastActivity) {
    QSqlQuery query(m_db);
    query.prepare("UPDATE sessions SET status = ?, last_activity = ? WHERE id = ?");
    query.addBindValue(status);
    query.addBindValue(lastActivity);
    query.addBindValue(sessionId);
    if (!query.exec()) return false;
    QSqlQuery projectUpdate(m_db);
    projectUpdate.prepare("UPDATE projects SET last_activity = ? WHERE id = "
                          "(SELECT project_id FROM sessions WHERE id = ?)");
    projectUpdate.addBindValue(lastActivity);
    projectUpdate.addBindValue(sessionId);
    return projectUpdate.exec();
}

QList<SessionRecord> AppDatabase::sessions(const QString &projectId) const {
    QList<SessionRecord> records;
    QSqlQuery query(m_db);
    QString sql = "SELECT id, provider, status, repository, branch, project_id, permission_mode, "
                  "preferred_name, started_at, last_activity FROM sessions";
    if (!projectId.isEmpty()) sql += " WHERE project_id = ?";
    sql += " ORDER BY last_activity DESC";
    query.prepare(sql);
    if (!projectId.isEmpty()) query.addBindValue(projectId);
    if (!query.exec()) return records;
    while (query.next()) {
        SessionRecord record;
        record.id = query.value(0).toString();
        record.provider = query.value(1).toString();
        record.status = query.value(2).toString();
        record.repository = query.value(3).toString();
        record.branch = query.value(4).toString();
        record.projectId = query.value(5).toString();
        record.permissionMode = query.value(6).toString();
        record.preferredName = query.value(7).toString();
        record.startedAt = query.value(8).toLongLong();
        record.lastActivity = query.value(9).toLongLong();
        records.append(record);
    }
    return records;
}

QString AppDatabase::ensureProject(const QString &workspace, const QString &name) {
    const QString canonical = QFileInfo(workspace).canonicalFilePath().isEmpty()
        ? QDir::cleanPath(workspace) : QFileInfo(workspace).canonicalFilePath();
    const QString id = QString::fromLatin1(QCryptographicHash::hash(
        canonical.toUtf8(), QCryptographicHash::Sha256).toHex().left(24));
    const QString displayName = name.trimmed().isEmpty()
        ? QFileInfo(canonical).fileName() : name.trimmed();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO projects(id, name, workspace, created_at, last_activity) "
                  "VALUES(?, ?, ?, ?, ?) ON CONFLICT(workspace) DO UPDATE SET "
                  "name=excluded.name, last_activity=excluded.last_activity");
    query.addBindValue(id);
    query.addBindValue(displayName.isEmpty() ? canonical : displayName);
    query.addBindValue(canonical);
    query.addBindValue(now);
    query.addBindValue(now);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return {};
    }
    return id;
}

QList<ProjectRecord> AppDatabase::projects() const {
    QList<ProjectRecord> result;
    QSqlQuery query(m_db);
    if (!query.exec("SELECT p.id, p.name, p.workspace, p.created_at, p.last_activity, "
                    "COUNT(s.id) FROM projects p LEFT JOIN sessions s ON s.project_id=p.id "
                    "GROUP BY p.id ORDER BY p.last_activity DESC")) return result;
    while (query.next()) {
        ProjectRecord project;
        project.id = query.value(0).toString();
        project.name = query.value(1).toString();
        project.workspace = query.value(2).toString();
        project.createdAt = query.value(3).toLongLong();
        project.lastActivity = query.value(4).toLongLong();
        project.sessionCount = query.value(5).toInt();
        result.append(project);
    }
    return result;
}

bool AppDatabase::markRunningSessionsInterrupted(qint64 timestamp) {
    QSqlQuery query(m_db);
    query.prepare("UPDATE sessions SET status = 'interrupted', last_activity = ? "
                  "WHERE status = 'running'");
    query.addBindValue(timestamp);
    return query.exec();
}

bool AppDatabase::addEvent(const EventModel::EventItem &event) {
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO events(session_id, type, content, timestamp, command, working_dir) "
                  "VALUES(?, ?, ?, ?, ?, ?)");
    query.addBindValue(event.sessionId);
    query.addBindValue(static_cast<int>(event.type));
    query.addBindValue(event.content);
    query.addBindValue(event.timestamp);
    query.addBindValue(event.command);
    query.addBindValue(event.workingDir);
    if (query.exec()) {
        if (!event.sessionId.isEmpty()) {
            QSqlQuery touch(m_db);
            touch.prepare("UPDATE sessions SET last_activity = ? WHERE id = ?");
            touch.addBindValue(event.timestamp);
            touch.addBindValue(event.sessionId);
            touch.exec();
            QSqlQuery projectTouch(m_db);
            projectTouch.prepare("UPDATE projects SET last_activity = ? WHERE id = "
                                 "(SELECT project_id FROM sessions WHERE id = ?)");
            projectTouch.addBindValue(event.timestamp);
            projectTouch.addBindValue(event.sessionId);
            projectTouch.exec();
        }
        return true;
    }
    m_lastError = query.lastError().text();
    return false;
}

QList<EventModel::EventItem> AppDatabase::events(const QString &sessionId, int limit) const {
    QList<EventModel::EventItem> result;
    QSqlQuery query(m_db);
    if (sessionId.isEmpty()) {
        query.prepare("SELECT type, content, session_id, timestamp, command, working_dir "
                      "FROM events ORDER BY timestamp DESC, id DESC LIMIT ?");
        query.addBindValue(limit);
    } else {
        query.prepare("SELECT type, content, session_id, timestamp, command, working_dir "
                      "FROM events WHERE session_id = ? ORDER BY timestamp DESC, id DESC LIMIT ?");
        query.addBindValue(sessionId);
        query.addBindValue(limit);
    }
    if (!query.exec()) return result;
    while (query.next()) {
        EventModel::EventItem event;
        event.type = static_cast<EventModel::EventType>(query.value(0).toInt());
        event.content = query.value(1).toString();
        event.sessionId = query.value(2).toString();
        event.timestamp = query.value(3).toLongLong();
        event.command = query.value(4).toString();
        event.workingDir = query.value(5).toString();
        result.prepend(event);
    }
    return result;
}

bool AppDatabase::clearEvents(const QString &sessionId) {
    QSqlQuery query(m_db);
    if (sessionId.isEmpty()) return query.exec("DELETE FROM events");
    query.prepare("DELETE FROM events WHERE session_id = ?");
    query.addBindValue(sessionId);
    return query.exec();
}
