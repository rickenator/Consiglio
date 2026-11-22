#include "sessionmetrics.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <algorithm>

namespace {
constexpr int kSchemaVersion = 1;

QString defaultStorePath() {
    // Beside the settings file: ~/.config/Aniviza/Consiglio.conf normally, and
    // the sandbox's own folder under --sandbox. The folder may not exist yet;
    // save() creates it. Never fall back to the home directory itself — that
    // would leave a stray metrics.json next to the user's files.
    const QString settingsFile = QSettings().fileName();
    if (settingsFile.isEmpty()) {
        const QString data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        return QDir(data.isEmpty() ? QDir::homePath() : data).filePath(QStringLiteral("metrics.json"));
    }
    return QFileInfo(settingsFile).absolutePath() + QStringLiteral("/metrics.json");
}
}  // namespace

SessionMetrics::SessionMetrics() : m_storePath(defaultStorePath()) {}

SessionMetrics &SessionMetrics::instance() {
    static SessionMetrics metrics;
    return metrics;
}

QString SessionMetrics::storePath() const { return m_storePath; }

void SessionMetrics::setStorePath(const QString &path) {
    m_storePath = path;
    m_sessions.clear();
    m_sessionId.clear();
}

// ─── Load / save ──────────────────────────────────────────────────────────

void SessionMetrics::load() {
    m_sessions.clear();
    QFile f(m_storePath);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject()) return;
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("version")).toInt() != kSchemaVersion) return;  // unknown: ignore, not guess
    const QJsonObject sessions = root.value(QStringLiteral("sessions")).toObject();
    for (auto it = sessions.begin(); it != sessions.end(); ++it) {
        const QJsonObject s = it.value().toObject();
        SessionRecord rec;
        rec.title = s.value(QStringLiteral("title")).toString();
        rec.updatedAt = qint64(s.value(QStringLiteral("updatedAt")).toDouble());
        for (const auto &t : s.value(QStringLiteral("turns")).toArray()) {
            const QJsonObject o = t.toObject();
            metrics::TurnSample turn;
            turn.atMs = qint64(o.value(QStringLiteral("at")).toDouble());
            turn.inputTokens = qint64(o.value(QStringLiteral("in")).toDouble());
            turn.outputTokens = qint64(o.value(QStringLiteral("out")).toDouble());
            turn.seconds = o.value(QStringLiteral("secs")).toDouble();
            turn.stopReason = o.value(QStringLiteral("stop")).toString();
            turn.toolCalls = o.value(QStringLiteral("tools")).toInt();
            turn.contextUsed = o.value(QStringLiteral("ctxUsed")).toInt();
            turn.contextSize = o.value(QStringLiteral("ctxSize")).toInt();
            rec.turns.append(turn);
        }
        for (const auto &c : s.value(QStringLiteral("context")).toArray()) {
            const QJsonArray pair = c.toArray();
            if (pair.size() != 3) continue;
            rec.context.append({qint64(pair.at(0).toDouble()), pair.at(1).toInt(), pair.at(2).toInt()});
        }
        m_sessions.insert(it.key(), rec);
    }
    trim();
}

void SessionMetrics::save() {
    trim();
    QJsonObject sessions;
    for (auto it = m_sessions.constBegin(); it != m_sessions.constEnd(); ++it) {
        QJsonArray turns;
        for (const auto &t : it->turns) {
            QJsonObject o;
            o[QStringLiteral("at")] = double(t.atMs);
            o[QStringLiteral("in")] = double(t.inputTokens);
            o[QStringLiteral("out")] = double(t.outputTokens);
            o[QStringLiteral("secs")] = t.seconds;
            if (!t.stopReason.isEmpty()) o[QStringLiteral("stop")] = t.stopReason;
            if (t.toolCalls > 0) o[QStringLiteral("tools")] = t.toolCalls;
            if (t.contextUsed > 0) o[QStringLiteral("ctxUsed")] = t.contextUsed;
            if (t.contextSize > 0) o[QStringLiteral("ctxSize")] = t.contextSize;
            turns.append(o);
        }
        QJsonArray context;
        for (const auto &c : it->context) context.append(QJsonArray{double(c.atMs), double(c.used), double(c.size)});
        QJsonObject s;
        if (!it->title.isEmpty()) s[QStringLiteral("title")] = it->title;
        s[QStringLiteral("updatedAt")] = double(it->updatedAt);
        s[QStringLiteral("turns")] = turns;
        s[QStringLiteral("context")] = context;
        sessions[it.key()] = s;
    }
    QJsonObject root;
    root[QStringLiteral("version")] = kSchemaVersion;
    root[QStringLiteral("sessions")] = sessions;

    const QString dir = QFileInfo(m_storePath).absolutePath();
    QDir().mkpath(dir);
    QSaveFile f(m_storePath);
    if (!f.open(QIODevice::WriteOnly)) return;   // a read-only home must not break the app
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    f.commit();
}

void SessionMetrics::clear() {
    m_sessions.clear();
    m_sessionId.clear();
    save();
}

void SessionMetrics::scheduleSave() {
    // Coalesce bursts (streaming context updates) into one write.
    QTimer::singleShot(1500, this, [this]() { save(); });
}

void SessionMetrics::trim() {
    for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
        while (it->turns.size() > kMaxTurnsPerSession) it->turns.removeFirst();
        while (it->context.size() > kMaxContextPerSession) it->context.removeFirst();
    }
    if (m_sessions.size() <= kMaxSessions) return;
    QList<QPair<qint64, QString>> order;
    order.reserve(m_sessions.size());
    for (auto it = m_sessions.constBegin(); it != m_sessions.constEnd(); ++it)
        order.append({it->updatedAt, it.key()});
    std::sort(order.begin(), order.end());
    const int drop = order.size() - kMaxSessions;
    for (int i = 0; i < drop; ++i) m_sessions.remove(order.at(i).second);
}

SessionMetrics::SessionRecord *SessionMetrics::record(const QString &sessionId, bool create) {
    auto it = m_sessions.find(sessionId);
    if (it != m_sessions.end()) return &it.value();
    if (!create || sessionId.isEmpty()) return nullptr;
    return &m_sessions[sessionId];
}

const SessionMetrics::SessionRecord *SessionMetrics::record(const QString &sessionId) const {
    auto it = m_sessions.constFind(sessionId);
    return it == m_sessions.constEnd() ? nullptr : &it.value();
}

// ─── Recording ────────────────────────────────────────────────────────────

void SessionMetrics::beginSession(const QString &sessionId) {
    m_sessionId = sessionId;
    m_turnStartMs = 0;
    m_turnToolCalls = 0;
    m_lastContextUsed = 0;
    m_lastContextSize = 0;
    const auto *rec = record(sessionId);
    if (rec && !rec->context.isEmpty()) {
        m_lastContextUsed = rec->context.last().used;
        m_lastContextSize = rec->context.last().size;
    }
}

void SessionMetrics::beginTurn(qint64 atMs) {
    m_turnStartMs = atMs;
    m_turnToolCalls = 0;
}

void SessionMetrics::noteToolCall() { ++m_turnToolCalls; }

void SessionMetrics::noteContext(int used, int size) {
    if (m_sessionId.isEmpty() || size <= 0) return;
    m_lastContextUsed = used;
    m_lastContextSize = size;
    auto *rec = record(m_sessionId, true);
    if (!rec) return;
    // Keep the trend meaningful without recording every streamed token: a new
    // sample when the window is resized, when the reading moves by >2%, or
    // when the last sample is older than 10 s.
    const auto &last = rec->context.isEmpty() ? metrics::ContextSample() : rec->context.last();
    const bool first = rec->context.isEmpty();
    const bool resized = last.size != size;
    const int delta = qAbs(used - last.used);
    const double pct = size > 0 ? (100.0 * delta) / size : 0.0;
    const bool moved = pct >= 2.0;
    const bool stale = !first && (QDateTime::currentMSecsSinceEpoch() - last.atMs) > 10000;
    if (!first && !resized && !moved && !stale) return;
    rec->context.append({QDateTime::currentMSecsSinceEpoch(), used, size});
    rec->updatedAt = QDateTime::currentMSecsSinceEpoch();
    while (rec->context.size() > kMaxContextPerSession) rec->context.removeFirst();
    emit changed(m_sessionId);
    scheduleSave();
}

void SessionMetrics::endTurn(const QString &stopReason, qint64 inputTokens, qint64 outputTokens,
                             qint64 atMs) {
    if (m_sessionId.isEmpty()) return;
    auto *rec = record(m_sessionId, true);
    if (!rec) return;
    metrics::TurnSample turn;
    turn.atMs = atMs;
    turn.inputTokens = inputTokens;
    turn.outputTokens = outputTokens;
    turn.seconds = m_turnStartMs > 0 ? qMax<qint64>(0, atMs - m_turnStartMs) / 1000.0 : 0.0;
    turn.stopReason = stopReason;
    turn.toolCalls = m_turnToolCalls;
    turn.contextUsed = m_lastContextUsed;
    turn.contextSize = m_lastContextSize;
    rec->turns.append(turn);
    rec->updatedAt = atMs;
    while (rec->turns.size() > kMaxTurnsPerSession) rec->turns.removeFirst();
    m_turnStartMs = 0;
    m_turnToolCalls = 0;
    emit changed(m_sessionId);
    save();   // a finished turn is worth persisting immediately
}

// ─── Queries ──────────────────────────────────────────────────────────────

QList<metrics::TurnSample> SessionMetrics::turns(const QString &sessionId) const {
    const auto *rec = record(sessionId);
    return rec ? rec->turns : QList<metrics::TurnSample>();
}

QList<metrics::ContextSample> SessionMetrics::contextSamples(const QString &sessionId) const {
    const auto *rec = record(sessionId);
    return rec ? rec->context : QList<metrics::ContextSample>();
}

QList<double> SessionMetrics::contextPercentSeries(const QString &sessionId, int maxPoints) const {
    QList<double> out;
    const auto samples = contextSamples(sessionId);
    if (samples.isEmpty()) return out;
    const int from = qMax(0, samples.size() - qMax(2, maxPoints));
    out.reserve(samples.size() - from);
    for (int i = from; i < samples.size(); ++i) {
        const auto &s = samples.at(i);
        if (s.size <= 0) continue;
        out.append(qBound(0.0, (100.0 * s.used) / s.size, 100.0));
    }
    return out;
}

metrics::Summary SessionMetrics::summary(const QString &sessionId) const {
    metrics::Summary sum;
    const auto *rec = record(sessionId);
    if (!rec) return sum;
    double fastest = 0.0;
    for (const auto &t : rec->turns) {
        ++sum.turns;
        sum.inputTokens += t.inputTokens;
        sum.outputTokens += t.outputTokens;
        sum.toolCalls += t.toolCalls;
        sum.totalSeconds += t.seconds;
        if (t.seconds > 0 && (fastest == 0.0 || t.seconds < fastest)) fastest = t.seconds;
        if (t.stopReason == QLatin1String("cancelled")) ++sum.cancelledTurns;
        if (t.stopReason == QLatin1String("error") || t.stopReason == QLatin1String("failed"))
            ++sum.failedTurns;
        if (t.contextUsed > sum.peakContextUsed) {
            sum.peakContextUsed = t.contextUsed;
            sum.peakContextSize = t.contextSize;
        }
    }
    for (const auto &c : rec->context) {
        if (c.used > sum.peakContextUsed) {
            sum.peakContextUsed = c.used;
            sum.peakContextSize = c.size;
        }
    }
    if (sum.turns > 0) sum.avgSeconds = sum.totalSeconds / sum.turns;
    sum.fastestSeconds = fastest;
    if (sum.totalSeconds > 0) sum.avgOutputTokensPerSecond = sum.outputTokens / sum.totalSeconds;
    return sum;
}

QStringList SessionMetrics::sessions() const { return m_sessions.keys(); }
