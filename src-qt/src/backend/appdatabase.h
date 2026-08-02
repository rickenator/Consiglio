#pragma once

#include "backend/sessionmanager.h"
#include "models/eventmodel.h"

#include <QSqlDatabase>
#include <QString>
#include <QVariant>

struct ProjectRecord {
    QString id;
    QString name;
    QString workspace;
    qint64 createdAt = 0;
    qint64 lastActivity = 0;
    int sessionCount = 0;
};

class AppDatabase {
public:
    explicit AppDatabase(const QString &databasePath = {});
    ~AppDatabase();

    AppDatabase(const AppDatabase &) = delete;
    AppDatabase &operator=(const AppDatabase &) = delete;

    bool isOpen() const;
    QString databasePath() const;
    QString lastError() const;

    QVariant preference(const QString &key, const QVariant &fallback = {}) const;
    bool containsPreference(const QString &key) const;
    bool setPreference(const QString &key, const QVariant &value);

    bool upsertSession(const SessionRecord &record);
    bool setSessionStatus(const QString &sessionId, const QString &status,
                          qint64 lastActivity);
    QList<SessionRecord> sessions(const QString &projectId = {}) const;
    bool markRunningSessionsInterrupted(qint64 timestamp);
    bool setSessionCodexThreadId(const QString &sessionId, const QString &threadId);

    QString ensureProject(const QString &workspace, const QString &name = {});
    QList<ProjectRecord> projects() const;

    bool addEvent(const EventModel::EventItem &event);
    QList<EventModel::EventItem> events(const QString &sessionId = {},
                                        int limit = 1000) const;
    bool clearEvents(const QString &sessionId = {});

private:
    bool ensureSchema();
    void migrateLegacySettings();
    static QByteArray encodeVariant(const QVariant &value);
    static QVariant decodeVariant(const QByteArray &data);

    QString m_connectionName;
    QString m_databasePath;
    QString m_lastError;
    QSqlDatabase m_db;
};
