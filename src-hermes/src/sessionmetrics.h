#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

namespace metrics {

// One completed turn. Every number here was reported by Hermes over ACP —
// input/output tokens from the prompt result, context from usage_update — plus
// the wall-clock seconds Consiglio measured itself. Nothing is inferred from
// text length: an estimate that looks like a measurement is a lie the UI would
// repeat in a graph.
struct TurnSample {
    qint64 atMs = 0;
    qint64 inputTokens = 0;
    qint64 outputTokens = 0;
    double seconds = 0.0;
    QString stopReason;
    int toolCalls = 0;
    int contextUsed = 0;
    int contextSize = 0;
};

// A context-window reading (used/size) at a point in time. Sampled from
// usage_update, so the trend is the agent's own accounting.
struct ContextSample {
    qint64 atMs = 0;
    int used = 0;
    int size = 0;
};

// Derived from the stored samples. Not stored itself.
struct Summary {
    int turns = 0;
    qint64 inputTokens = 0;
    qint64 outputTokens = 0;
    int toolCalls = 0;
    double totalSeconds = 0.0;
    double avgSeconds = 0.0;
    double fastestSeconds = 0.0;
    double avgOutputTokensPerSecond = 0.0;   // summed tokens / summed seconds
    int peakContextUsed = 0;
    int peakContextSize = 0;
    int cancelledTurns = 0;
    int failedTurns = 0;
    bool empty() const { return turns == 0 && inputTokens == 0 && outputTokens == 0; }
};

}  // namespace metrics

// Per-session measurements Consiglio owns.
//
// Hermes keeps the conversations; this keeps only what the GUI needs to show a
// perspective the CLI cannot: how a session spent its context, how each turn
// went, where the time went. It never stores conversation text, it is capped,
// it lives beside the settings file (so --sandbox isolates it), and it can be
// deleted at any time without affecting Hermes. That is the only local state
// Consiglio adds, and this comment is the contract.
class SessionMetrics : public QObject {
    Q_OBJECT
public:
    static SessionMetrics &instance();

    // Where the measurements live. Defaults to metrics.json beside the
    // QSettings file (so a sandbox keeps its own); tests point it elsewhere.
    QString storePath() const;
    void setStorePath(const QString &path);

    void load();
    void save();
    void clear();

    // Which session new samples belong to. Called when a session opens or is
    // restored; resets per-turn tracking.
    void beginSession(const QString &sessionId);
    QString sessionId() const { return m_sessionId; }

    void beginTurn(qint64 atMs);
    void noteToolCall();
    void noteContext(int used, int size);
    void endTurn(const QString &stopReason, qint64 inputTokens, qint64 outputTokens, qint64 atMs);

    QList<metrics::TurnSample> turns(const QString &sessionId) const;
    QList<metrics::ContextSample> contextSamples(const QString &sessionId) const;
    QList<double> contextPercentSeries(const QString &sessionId, int maxPoints = 60) const;
    metrics::Summary summary(const QString &sessionId) const;
    QStringList sessions() const;

    // Caps: a session keeps its most recent turns and context readings; the
    // store keeps the most recently touched sessions.
    static constexpr int kMaxTurnsPerSession = 500;
    static constexpr int kMaxContextPerSession = 240;
    static constexpr int kMaxSessions = 300;

signals:
    // A sample was recorded for `sessionId` (graphs may re-read).
    void changed(const QString &sessionId);

private:
    SessionMetrics();
    struct SessionRecord {
        QString title;
        qint64 updatedAt = 0;
        QList<metrics::TurnSample> turns;
        QList<metrics::ContextSample> context;
    };
    SessionRecord *record(const QString &sessionId, bool create);
    const SessionRecord *record(const QString &sessionId) const;
    void scheduleSave();
    void trim();

    QHash<QString, SessionRecord> m_sessions;
    QString m_storePath;
    QString m_sessionId;

    // Turn in flight.
    qint64 m_turnStartMs = 0;
    int m_turnToolCalls = 0;
    int m_lastContextUsed = 0;
    int m_lastContextSize = 0;
};
