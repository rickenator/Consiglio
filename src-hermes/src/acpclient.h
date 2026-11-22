#pragma once

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include <QPointer>
#include <QVariant>
#include "sessioncatalog.h"

class QTimer;
class AcpTransport;

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
        QString hint;         // input.hint, e.g. "model name to switch to" ("" = no input)
    };

    // A session configuration option (ACP configOptions), e.g. the model,
    // reasoning effort or edit-approval mode. Rendered generically: the UI
    // never assumes which ids exist, so options a newer Hermes adds appear on
    // their own and options it drops disappear.
    struct ConfigChoice {
        QString value;
        QString name;
        QString description;
        QString group;        // group label for grouped selects ("" = ungrouped)
    };
    struct ConfigOption {
        QString id;
        QString name;
        QString description;
        QString category;     // "mode", "model", "thought_level", "other", … (free-form)
        QString type;         // "select" is the only type rendered as a picker
        QString currentValue;
        QList<ConfigChoice> choices;
    };
    // Legacy ACP session modes (used only when the agent sends no "mode"
    // config option — the spec prefers configOptions).
    struct SessionMode {
        QString id;
        QString name;
        QString description;
    };
    // initialize.authMethods — how the agent can be set up (e.g. Hermes's
    // "hermes-setup", a terminal command that runs the provider wizard).
    struct AuthMethod {
        QString id;
        QString name;
        QString description;
        QString type;         // "terminal", "agent", … (free-form)
        QStringList args;     // for type == "terminal": args to pass to the agent binary
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

    // List previously-saved Hermes sessions, newest first. The cwd argument
    // is kept for callers but is not sent: filtering by workspace hid current
    // sessions whose cwd is stored on a different column. Emits sessionsListed().
    bool listSessions(const QString &cwd = QString());

    // Send a user prompt to the active session. Returns false if no session
    // is ready or a turn is already in flight. Streams until turnCompleted().
    bool sendPrompt(const QString &text);

    // Ask the agent to stop the turn in flight (session/cancel). The turn then
    // completes with stopReason "cancelled"; the session stays open.
    bool cancelTurn();
    void setCancelGrace(int ms);   // how long to wait for the agent to confirm (default 8 s)

    // End the active session (client-side) but keep the process warm so the
    // session list can be refreshed immediately. Emits sessionStopped().
    void stopSession();

    // Kill the process entirely (app exit).
    void shutdown();

    // Where the agent runs. Default: a local `hermes acp` process. setRemote()
    // points the same protocol at `ssh [-o …] <destination> -- <executable> acp`;
    // everything above the transport is identical, and failures surface ssh's
    // own message. Call before launch()/restartAgent().
    void setRemote(const QString &destination, const QStringList &sshOptions = QStringList(),
                   const QString &remoteExecutable = QStringLiteral("hermes"));
    void clearRemote();
    bool isRemote() const { return !m_remoteHost.isEmpty(); }
    QString remoteHost() const { return m_remoteHost; }
    // What the last launch actually ran, e.g. "hermes acp" or "spark: hermes acp".
    QString transportDescription() const;

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

    // Run a slash command without showing it in the conversation (used to
    // discover Hermes's full command list with "/commands N"). Queued behind
    // any turn in flight; a user prompt sent meanwhile waits for it. Emits
    // quietCommandFinished(). Returns false when no session is live.
    bool runQuietCommand(const QString &text);
    bool hasQuietWork() const;

    // Session configuration (ACP session/set_config_option, session/set_mode).
    QList<ConfigOption> configOptions() const;
    QList<SessionMode> sessionModes() const;
    QString currentModeId() const;
    bool setConfigOption(const QString &configId, const QString &value);
    bool setMode(const QString &modeId);
    QList<AuthMethod> authMethods() const;

    QString sessionId() const;
    QString sessionCwd() const;
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
    // A runQuietCommand() finished: `output` is the agent's reply text.
    void quietCommandFinished(const QString &command, const QString &output, bool ok);
    void configOptionsChanged();
    void modesChanged();
    // set_config_option / set_mode was rejected; the previous value stands.
    void configChangeSucceeded(const QString &configId);
    void configChangeFailed(const QString &configId, const QString &message);

private slots:
    void onTransportData(const QByteArray &data);
    void onTransportExited(int code, bool crashed);

private:
    enum class ReqKind { Initialize, NewSession, ResumeSession, LoadSession, ListSessions, Prompt,
                         QuietPrompt, SetConfigOption, SetMode };

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
    bool dispatchPrompt(const QString &text, bool quiet);
    void pumpQuietQueue();
    void finishQuiet(bool ok);
    void parseSessionConfig(const QJsonObject &result);
    void parseConfigOptions(const QJsonArray &options);
    static QString idKey(const QJsonValue &id);

    QPointer<AcpTransport> m_transport;
    QString m_requestedExecutable;   // what the caller asked for ("" = resolve)
    QString m_resolvedExecutable;    // what the last launch actually ran
    // Remote target ("" = local). argv-only: ssh options are already split.
    QString m_remoteHost;
    QString m_remoteExe = QStringLiteral("hermes");
    QStringList m_sshOptions;
    QString m_lastStderr;            // tail of the agent's/ssh's stderr, for diagnostics
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
    QList<ConfigOption> m_configOptions;
    QList<SessionMode> m_modes;
    QString m_currentMode;
    QList<AuthMethod> m_authMethods;
    // Pending optimistic config changes: request id -> (configId, previous, new).
    struct ConfigChange { QString id; QString previous; QString value; bool mode = false; };
    QMap<QString, ConfigChange> m_configChanges;

    // Quiet (hidden) slash commands.
    QStringList m_quietQueue;
    QString m_quietCommand;
    QString m_quietOutput;
    int m_quietReqId = -1;
    QString m_deferredPrompt;          // user prompt waiting for a quiet command
    bool m_hasDeferredPrompt = false;
    int m_cancelGraceMs = 8000;
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

    // session/list pagination + merge with the read-only Hermes store.
    QList<SessionCatalog::Entry> m_listedSessions;
    bool m_listing = false;
    void requestSessionPage(const QString &cursor);
    void finishSessionList();
};

Q_DECLARE_METATYPE(AcpClient::PermissionRequest)
Q_DECLARE_METATYPE(AcpClient::SessionInfo)
