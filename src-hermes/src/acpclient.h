#pragma once

#include <QObject>
#include <QProcess>
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include <QPointer>
#include <QVariant>

class QTimer;

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
//
// Hermes updates itself (`hermes update` swaps the checkout under a stable
// launcher on PATH and restarts what it owns), so this client is deliberately
// version-agnostic:
//   - the executable is re-resolved on every launch (HERMES_BIN, then PATH),
//     never pinned to a path or version;
//   - what the agent can do is negotiated from the initialize response
//     (protocolVersion, agentInfo, agentCapabilities), and unknown methods,
//     fields, update kinds and capabilities are ignored rather than fatal;
//   - an unexpected agent exit (for example during an update) relaunches the
//     agent with backoff and re-opens the active session via session/resume
//     (or session/load) when the new agent advertises it. An interrupted turn
//     is reported as failed, never silently replayed.
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
    // `executable` empty means "resolve it" (see resolveExecutable) — resolved
    // again on every (re)launch so an update that moves the binary is picked up.
    bool launch(const QString &executable = QString());

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

    // A pending agent->client permission request (session/request_permission).
    struct PermissionOption {
        QString optionId;     // opaque id chosen by the agent; echoed back verbatim
        QString name;         // human label
        QString kind;         // allow_once / allow_always / reject_once / reject_always / (future kinds)
    };
    struct PermissionRequest {
        QString key;          // client-side key for respondPermission()
        QString sessionId;
        QString toolCallId;
        QString title;        // e.g. the command Hermes wants to run
        QString detail;       // text content of the tool call, if any
        QList<PermissionOption> options;
    };

    // Answer a permission request with one of the agent-supplied option ids.
    // Returns false if the request is unknown (already answered, stale) or the
    // option id was not offered.
    bool respondPermission(const QString &key, const QString &optionId);
    // Answer a permission request with the ACP "cancelled" outcome.
    bool cancelPermission(const QString &key);
    QList<PermissionRequest> pendingPermissions() const;

    // Restart the agent process (e.g. after `hermes update`) and re-open the
    // active session if the agent supports resuming it.
    void restartAgent();

    // Relaunch automatically when the agent exits unexpectedly (default on).
    void setAutoRestart(bool enabled);
    void setRestartBackoff(int initialMs, int maxAttempts);

    // What the agent reported in its initialize response.
    int agentProtocolVersion() const;
    QString agentName() const;
    QString agentVersion() const;
    QJsonObject agentCapabilities() const;
    bool canListSessions() const;
    bool canResumeSessions() const;
    QString resolvedExecutable() const;

    // Resolve the Hermes executable: an explicit request, else $HERMES_BIN,
    // else PATH lookup, else the bare name for the OS to resolve.
    static QString resolveExecutable(const QString &requested = QString());

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
    // Connection-level problems (launch, initialize, list). Turn and session
    // failures arrive as turnFailed() / sessionFailed() instead.
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
    // The in-flight prompt failed (ACP error, or the agent exited mid-turn).
    // The client is idle again and can take a new prompt.
    void turnFailed(const QString &message);
    // session/new or session/resume failed; no session is active.
    void sessionFailed(const QString &message);
    void processExited(int code);
    void permissionRequested(const AcpClient::PermissionRequest &request);
    // A pending permission request went away without an answer from the UI
    // (turn ended, session stopped, agent exited).
    void permissionCancelled(const QString &key);
    // The agent exited unexpectedly and a relaunch is scheduled.
    void agentRestarting(int attempt, int delayMs);
    // Gave up relaunching; call launch()/restartAgent() to try again.
    void agentRestartFailed(const QString &message);
    // The (re)connected agent reports a different version than last time —
    // typically because Hermes updated itself.
    void agentVersionChanged(const QString &oldVersion, const QString &newVersion);
    // After a relaunch, the previously active session was re-opened.
    void sessionRestored(const QString &sessionId);

private slots:
    void onStdout();
    void onProcessFinished(int code, QProcess::ExitStatus status);

private:
    enum class ReqKind { Initialize, NewSession, ResumeSession, LoadSession, ListSessions, Prompt };

    bool launchInternal();
    void stopProcess();
    void abandonActiveSession();
    bool sendRequest(int id, const QString &method, const QJsonObject &params, ReqKind kind);
    bool sendMessage(const QJsonObject &msg);
    void sendErrorResponse(const QJsonValue &id, int code, const QString &message);
    void handleLine(const QString &line);
    void handleResponse(const QJsonObject &msg);
    void handleAgentRequest(const QJsonObject &msg);
    void handleUpdate(const QJsonObject &params);
    void handleInitialized(const QJsonObject &result);
    void finishTurn(const QJsonObject &result);
    void failTurn(const QString &message);
    void resetSessionState();
    void cancelPendingPermissions(bool notifyAgent);
    void scheduleRestart();
    bool openSession(const QString &sessionId, const QString &cwd, bool restoring);
    static QString idKey(const QJsonValue &id);

    QPointer<QProcess> m_proc;
    QString m_requestedExecutable;   // what the caller asked for ("" = resolve)
    QString m_resolvedExecutable;    // what the last launch actually ran
    QByteArray m_stdoutBuf;
    int m_nextReqId = 1;
    QMap<QString, ReqKind> m_pending;  // outstanding request id -> kind
    int m_promptReqId = -1;            // id of the in-flight session/prompt
    bool m_connected = false;
    bool m_sessionReady = false;
    bool m_busy = false;
    bool m_stopping = false;           // shutdown()/restartAgent() in progress
    QString m_sessionId;
    QString m_sessionCwd;
    QStringList m_models;
    QList<SlashCommand> m_commands;
    int m_contextUsed = 0;
    int m_contextSize = 0;

    // Negotiated from initialize.
    int m_agentProtocol = 0;
    QString m_agentName;
    QString m_agentVersion;
    QJsonObject m_agentCaps;

    // Permission requests awaiting a UI answer, keyed by idKey(request id).
    struct PendingPermission { QJsonValue rpcId; PermissionRequest request; };
    QMap<QString, PendingPermission> m_permissions;

    // Restart-after-exit (Hermes self-update) handling.
    bool m_autoRestart = true;
    int m_restartInitialMs = 500;
    int m_restartMaxAttempts = 6;
    int m_restartAttempt = 0;
    QString m_restoreSessionId;        // session to re-open once reconnected
    QString m_restoreCwd;
    QTimer *m_restartTimer = nullptr;

    // Session being opened (new/resume/load) — its updates are accepted
    // before the response arrives (session/load replays history first).
    QString m_openingSessionId;
    bool m_openingNew = false;
    bool m_openingRestore = false;
};

Q_DECLARE_METATYPE(AcpClient::PermissionRequest)
Q_DECLARE_METATYPE(AcpClient::SessionInfo)
