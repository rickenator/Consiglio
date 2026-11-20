#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QVariant>

class QJsonObject;

// Parsing, sorting and a read-only look at Hermes's session store
// (~/.hermes/state.db). The ACP `session/list` result is the starting point;
// the store is consulted so sessions Hermes retags away from source=acp, or
// whose cwd lives on the sessions.cwd column rather than in model_config,
// still appear. Nothing here writes to the database.
namespace SessionCatalog {

struct Entry {
    QString id;
    QString title;
    QString cwd;
    QString updatedAt;   // original or ISO-8601 text, for display
    QDateTime updated;   // invalid when the timestamp couldn't be parsed
};

// Pull a string from the first key that is present and non-empty.
QString firstString(const QJsonObject &obj, const QStringList &keys);

// ISO-8601 (with or without a trailing Z), or a Unix timestamp (seconds).
QDateTime parseTime(const QVariant &value);

// `session/list` result: camelCase or snake_case fields, optional next cursor.
// Entries without an id are skipped. *nextCursor is set when the agent has
// more pages (nextCursor / next_cursor).
QList<Entry> parseListResult(const QJsonObject &result, QString *nextCursor = nullptr);

// Read-only SELECT from hermesHome/state.db. Skips hidden, archived, and
// empty (no messages) rows. Returns {} if the file, table or driver is
// missing — never creates or migrates anything.
QList<Entry> readStore(const QString &hermesHome, QString *error = nullptr);

// Default Hermes data dir: $HERMES_HOME, else %LOCALAPPDATA%/hermes (Windows),
// else ~/.hermes.
QString defaultHermesHome();

void sortNewestFirst(QList<Entry> &sessions);

// Union by id; ACP rows win when both sides have the same id.
QList<Entry> merge(const QList<Entry> &acp, const QList<Entry> &store);

} // namespace SessionCatalog
