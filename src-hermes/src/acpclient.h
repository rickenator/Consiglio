#pragma once

#include <QObject>
#include <QProcess>
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>

// AcpClient drives a single persistent `hermes acp` process speaking the
// Agent Client Protocol (newline-delimited JSON-RPC over stdio). The process
// lives for the app's lifetime and can host several Hermes sessions in
// sequence — open one with newSession(), re-open an old one with
// resumeSession(), browse previous ones with listSessions(). Everything the
// CLI shows you is surfaced as Qt signals:
//
//   - streamed answer text        (agent_message_chunk)
//   - streamed reasoning          (agent_thought_chunk)
//   - live tool calls + results    (tool_call / tool_call_update)
//   - context window usage         (usage_update  -> used/size)
//   - available models             (session/new|resume result)
//   - slash commands               (availableCommands update)
//   - turn completion + tokens     (prompt result -> stopReason/usage)
//
// This is what makes the UI "resemble Hermes": instead of a one-shot
// `hermes chat -q` black box, we stream the same events the editor adapter
// consumes.
class AcpClient : public QObject {
    Q_OBJECT

public:
    struct SlashCommand {
        QString name;         // e.g. "model"
        QString description;  // e.g. "Show current model and provider"
    };

    // A previously-saved Hermes session (from session/list).
    struct SessionInfo {
        QString id;          // sessionId (UUID)
        QString title;       // auto-generated title
        QString cwd;         // workspace directory
        QString updatedAt;   // ISO-8601 timestamp
    };

    explicit AcpClient(QObject *parent = nullptr);
    ~AcpClient() override;

    // Launch `hermes acp` and run initialize. The process stays up afterwards
    // so sessions can be opened/resumed/listed without relaunching. Emits
    // connected() once initialized, or errorOccurred() on failure.
    bool launch(const QString &executable = QStringLiteral("hermes"));

    // Open a brand-new session rooted at cwd. Emits ready() when live.
    bool newSession(const QString &cwd);

    // Re-open an existing session (from listSessions). Emits ready() when live.
    bool resumeSession(const QString &sessionId, const QString &cwd);

    // List previously-saved sessions for cwd. Emits sessionsListed().
    bool listSessions(const QString &cwd);

    // Send a user prompt to the active session. Returns false if no session
    // is ready or a turn is already in flight. Streams until turnCompleted().
    bool sendPrompt(const QString &text);

    // End the active session (client-side) but keep the process warm so the
    // session list can be refreshed immediately. Emits sessionStopped().
    void stopSession();

    // Kill the process entirely (app exit).
    void shutdown();

    QString sessionId() const;
    QStringList availableModels() const;
    QList<SlashCommand> slashCommands() const;
    int contextUsed() const;
    int contextSize() const;
    bool isBusy() const;
    bool isReady() const;      // an active session is live
    bool isConnected() const;  // process up + initialized

signals:
    void connected();                          // process up + initialized
    void ready();                              // a session is live (new or resumed)
    void errorOccurred(const QString &message);
    void sessionsListed(QList<SessionInfo> sessions);
    void sessionStopped();                     // active session ended
    void messageChunk(const QString &text);   // streamed final answer
    void thoughtChunk(const QString &text);   // streamed reasoning
    void toolCallStarted(const QString &id, const QString &title, const QString &kind);
    void toolCallUpdated(const QString &id, const QString &status, const QString &output);
    void contextUsage(int used, int size);
    void sessionTitle(const QString &title);
    void commandsUpdated();
    void turnCompleted(const QString &stopReason, qint64 inputTokens, qint64 outputTokens);
    void processExited(int code);

private slots:
    void onStdout();
    void onProcessFinished(int code, QProcess::ExitStatus status);

private:
    enum class ReqKind { Initialize, NewSession, ResumeSession, ListSessions, Prompt };

    bool sendRequest(int id, const QString &method, const QJsonObject &params, ReqKind kind);
    void handleLine(const QString &line);
    void handleUpdate(const QJsonObject &update);
    void finishTurn(const QJsonObject &result);
    void resetSessionState();

    QProcess *m_proc = nullptr;
    QByteArray m_stdoutBuf;
    int m_nextReqId = 1;        // initialize takes 1; subsequent requests increment
    QMap<int, ReqKind> m_pending;  // outstanding request id -> kind
    bool m_connected = false;
    bool m_sessionReady = false;
    bool m_busy = false;
    QString m_sessionId;
    QStringList m_models;
    QList<SlashCommand> m_commands;
    int m_contextUsed = 0;
    int m_contextSize = 0;
};
