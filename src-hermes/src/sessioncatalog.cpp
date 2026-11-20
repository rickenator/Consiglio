#include "sessioncatalog.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QVariant>
#include <QUuid>
#include <QSet>
#include <algorithm>

#ifdef Q_OS_WIN
#include <QProcessEnvironment>
#endif

namespace SessionCatalog {
namespace {

const QString kConnectionPrefix = QStringLiteral("consiglio-hermes-sessions-ro-");

QSqlDatabase openReadonly(const QString &path, QString *error) {
    const QString name = kConnectionPrefix + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
    db.setDatabaseName(path);
    // Read-only: no journal writes, no schema changes, no WAL checkpoint.
    db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    if (!db.open()) {
        if (error) *error = db.lastError().text();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(name);
    }
    return db;
}

void closeAndRemove(QSqlDatabase &db) {
    if (!db.isValid()) return;
    const QString name = db.connectionName();
    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(name);
}

QSet<QString> columns(QSqlDatabase &db, const QString &table) {
    QSet<QString> out;
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table))) return out;
    while (q.next()) out.insert(q.value(1).toString());
    return out;
}

Entry fromStoreRow(const QSqlQuery &q, const QSet<QString> &cols) {
    Entry e;
    auto val = [&](const char *name) -> QVariant {
        return cols.contains(QLatin1String(name)) ? q.value(QLatin1String(name)) : QVariant();
    };
    e.id = val("id").toString();
    e.title = val("title").toString();
    e.cwd = val("cwd").toString();
    const QVariant recency = val("last_activity_at").isNull() ? val("started_at") : val("last_activity_at");
    e.updated = parseTime(recency);
    if (e.updated.isValid()) e.updatedAt = e.updated.toUTC().toString(Qt::ISODate);
    else e.updatedAt = recency.toString();
    return e;
}

} // namespace

QString firstString(const QJsonObject &obj, const QStringList &keys) {
    for (const QString &k : keys) {
        const QString v = obj.value(k).toString().trimmed();
        if (!v.isEmpty()) return v;
    }
    // Some agents send numeric ids.
    for (const QString &k : keys) {
        const QJsonValue v = obj.value(k);
        if (v.isDouble()) return QString::number(v.toDouble(), 'g', 17);
    }
    return {};
}

QDateTime parseTime(const QVariant &value) {
    if (!value.isValid() || value.isNull()) return {};
    if (value.type() == QVariant::DateTime) {
        const QDateTime dt = value.toDateTime();
        return dt.timeSpec() == Qt::LocalTime ? dt.toUTC() : dt.toUTC();
    }
    if (value.type() == QVariant::Double || value.type() == QVariant::Int
        || value.type() == QVariant::LongLong || value.type() == QVariant::ULongLong) {
        bool ok = false;
        const double seconds = value.toDouble(&ok);
        if (ok && seconds > 1e8 && seconds < 1e12)
            return QDateTime::fromSecsSinceEpoch(qint64(seconds), Qt::UTC);
        if (ok && seconds >= 1e12)
            return QDateTime::fromMSecsSinceEpoch(qint64(seconds), Qt::UTC);
    }
    const QString raw = value.toString().trimmed();
    if (raw.isEmpty()) return {};
    bool ok = false;
    const double seconds = raw.toDouble(&ok);
    if (ok && seconds > 1e8 && seconds < 1e12)
        return QDateTime::fromSecsSinceEpoch(qint64(seconds), Qt::UTC);
    if (ok && seconds >= 1e12)
        return QDateTime::fromMSecsSinceEpoch(qint64(seconds), Qt::UTC);
    // ISO-8601, with or without a trailing Z, with or without milliseconds.
    QString iso = raw;
    if (iso.endsWith(QLatin1Char('Z'), Qt::CaseInsensitive))
        iso = iso.left(iso.size() - 1) + QStringLiteral("+00:00");
    QDateTime dt = QDateTime::fromString(iso, Qt::ISODate);
    if (!dt.isValid()) dt = QDateTime::fromString(iso, Qt::ISODateWithMs);
    if (!dt.isValid()) dt = QDateTime::fromString(raw, Qt::ISODate);
    if (dt.isValid() && dt.timeSpec() == Qt::LocalTime) dt.setTimeSpec(Qt::UTC);
    return dt.toUTC();
}

QList<Entry> parseListResult(const QJsonObject &result, QString *nextCursor) {
    if (nextCursor) {
        *nextCursor = firstString(result, {QStringLiteral("nextCursor"), QStringLiteral("next_cursor")});
    }
    const QJsonArray arr = result.value(QStringLiteral("sessions")).toArray();
    QList<Entry> out;
    out.reserve(arr.size());
    for (const auto &v : arr) {
        const QJsonObject so = v.toObject();
        Entry e;
        e.id = firstString(so, {QStringLiteral("sessionId"), QStringLiteral("session_id"),
                                QStringLiteral("id")});
        if (e.id.isEmpty()) continue;
        e.title = firstString(so, {QStringLiteral("title"), QStringLiteral("display_name"),
                                   QStringLiteral("displayName")});
        e.cwd = firstString(so, {QStringLiteral("cwd"), QStringLiteral("workspace"),
                                 QStringLiteral("directory")});
        const QJsonValue ts = so.contains(QStringLiteral("updatedAt")) ? so.value(QStringLiteral("updatedAt"))
                            : so.contains(QStringLiteral("updated_at")) ? so.value(QStringLiteral("updated_at"))
                            : so.contains(QStringLiteral("last_activity_at")) ? so.value(QStringLiteral("last_activity_at"))
                            : so.value(QStringLiteral("started_at"));
        e.updatedAt = ts.isString() ? ts.toString() : (ts.isDouble() ? QString::number(ts.toDouble(), 'g', 17) : QString());
        e.updated = parseTime(ts.toVariant());
        if (e.updated.isValid() && e.updatedAt.isEmpty())
            e.updatedAt = e.updated.toUTC().toString(Qt::ISODate);
        out.append(e);
    }
    return out;
}

QString defaultHermesHome() {
    const QString env = qEnvironmentVariable("HERMES_HOME").trimmed();
    if (!env.isEmpty()) return QDir::cleanPath(env);
#ifdef Q_OS_WIN
    const QString local = QProcessEnvironment::systemEnvironment().value(QStringLiteral("LOCALAPPDATA"));
    if (!local.isEmpty()) return QDir(local).filePath(QStringLiteral("hermes"));
#endif
    return QDir::home().filePath(QStringLiteral(".hermes"));
}

QList<Entry> readStore(const QString &hermesHome, QString *error) {
    const QString path = QDir(hermesHome).filePath(QStringLiteral("state.db"));
    if (!QFileInfo::exists(path)) {
        if (error) *error = QStringLiteral("no state.db");
        return {};
    }
    QSqlDatabase db = openReadonly(path, error);
    if (!db.isValid()) return {};

    const QSet<QString> cols = columns(db, QStringLiteral("sessions"));
    if (!cols.contains(QStringLiteral("id"))) {
        if (error) *error = QStringLiteral("sessions table missing");
        closeAndRemove(db);
        return {};
    }

    QStringList wanted{QStringLiteral("id")};
    for (const char *c : {"title", "cwd", "last_activity_at", "started_at",
                          "message_count", "hidden", "archived"}) {
        if (cols.contains(QLatin1String(c))) wanted << QLatin1String(c);
    }

    QString sql = QStringLiteral("SELECT %1 FROM sessions").arg(wanted.join(QStringLiteral(", ")));
    QStringList where;
    if (cols.contains(QStringLiteral("message_count")))
        where << QStringLiteral("IFNULL(message_count, 0) > 0");
    if (cols.contains(QStringLiteral("hidden")))
        where << QStringLiteral("IFNULL(hidden, 0) = 0");
    if (cols.contains(QStringLiteral("archived")))
        where << QStringLiteral("IFNULL(archived, 0) = 0");
    if (!where.isEmpty()) sql += QStringLiteral(" WHERE ") + where.join(QStringLiteral(" AND "));

    QSqlQuery q(db);
    if (!q.exec(sql)) {
        if (error) *error = q.lastError().text();
        closeAndRemove(db);
        return {};
    }
    QList<Entry> out;
    while (q.next()) {
        Entry e = fromStoreRow(q, cols);
        if (!e.id.isEmpty()) out.append(e);
    }
    closeAndRemove(db);
    return out;
}

void sortNewestFirst(QList<Entry> &sessions) {
    std::sort(sessions.begin(), sessions.end(), [](const Entry &a, const Entry &b) {
        if (a.updated.isValid() != b.updated.isValid()) return a.updated.isValid();
        if (a.updated.isValid() && b.updated.isValid() && a.updated != b.updated)
            return a.updated > b.updated;
        return a.id > b.id;
    });
}

QList<Entry> merge(const QList<Entry> &acp, const QList<Entry> &store) {
    QList<Entry> out = acp;
    QSet<QString> seen;
    for (const auto &e : acp) seen.insert(e.id);
    for (const auto &e : store) {
        if (seen.contains(e.id)) continue;
        seen.insert(e.id);
        out.append(e);
    }
    sortNewestFirst(out);
    return out;
}

} // namespace SessionCatalog
