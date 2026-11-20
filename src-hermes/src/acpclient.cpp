#include "acpclient.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonValue>
#include <QStandardPaths>
#include <QTimer>
#include <QtGlobal>

namespace {
// JSON-RPC 2.0 standard error codes.
constexpr int kMethodNotFound = -32601;

QString textOfContentBlocks(const QJsonArray &blocks) {
    // ACP tool-call content: [{type:"content", content:{type:"text", text}}, ...]
    // plus diff/terminal blocks we don't render here. Unknown types are skipped.
    QString text;
    for (const auto &c : blocks) {
        const auto co = c.toObject();
        if (co.value("type").toString() == QLatin1String("content")) {
            text += co.value("content").toObject().value("text").toString();
        }
    }
    return text;
}

qint64 toInt64(const QJsonValue &v) {
    // Token counts can exceed int range; JSON numbers arrive as doubles.
    return v.isDouble() ? static_cast<qint64>(v.toDouble()) : v.toVariant().toLongLong();
}
} // namespace

AcpClient::AcpClient(QObject *parent) : QObject(parent) {
    qRegisterMetaType<AcpClient::PermissionRequest>("AcpClient::PermissionRequest");
    qRegisterMetaType<QList<AcpClient::SessionInfo>>("QList<SessionInfo>");
}

AcpClient::~AcpClient() {
    // Don't notify listeners that may already be half-destroyed.
    blockSignals(true);
    shutdown();
}

// ─── Executable resolution ────────────────────────────────────────────────

QString AcpClient::resolveExecutable(const QString &requested) {
    if (!requested.trimmed().isEmpty()) return requested.trimmed();

    const QString fromEnv = qEnvironmentVariable("HERMES_BIN").trimmed();
    if (!fromEnv.isEmpty()) return fromEnv;

    // PATH first, so whatever `hermes` the user's shell runs (and whatever
    // `hermes update` keeps current) is what we run.
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("hermes"));
    if (!onPath.isEmpty()) return onPath;

    // A GUI-launched app often inherits a much smaller PATH than a login
    // shell, so also look in the standard install locations (the same layout
    // the Hermes installer and the Electron shell use). These are install
    // locations, not versions: the launcher in them is kept current by
    // `hermes update`.
    const QString home = QDir::homePath();
    const QStringList dirs = {
        home + QStringLiteral("/.local/bin"),
        home + QStringLiteral("/.hermes/hermes-agent/venv/bin"),
        QStringLiteral("/usr/local/bin"),
        QStringLiteral("/opt/homebrew/bin"),
        home + QStringLiteral("/bin"),
    };
    const QString found = QStandardPaths::findExecutable(QStringLiteral("hermes"), dirs);
    if (!found.isEmpty()) return found;

    // Last resort: let the OS resolve it (and fail with a clear error).
    return QStringLiteral("hermes");
}

QString AcpClient::resolvedExecutable() const { return m_resolvedExecutable; }

// ─── Process lifecycle ────────────────────────────────────────────────────

bool AcpClient::launch(const QString &executable) {
    m_requestedExecutable = executable;
    m_restartAttempt = 0;
    if (m_restartTimer) m_restartTimer->stop();
    return launchInternal();
}

bool AcpClient::launchInternal() {
    if (m_proc && m_proc->state() != QProcess::NotRunning) return false;
    if (m_proc) {
        m_proc->disconnect(this);
        m_proc->deleteLater();
        m_proc = nullptr;
    }
    m_stopping = false;
    m_stdoutBuf.clear();
    m_pending.clear();

    // Re-resolve on every launch: an update may have replaced the launcher.
    m_resolvedExecutable = resolveExecutable(m_requestedExecutable);

    auto *proc = new QProcess(this);
    m_proc = proc;
    proc->setProcessChannelMode(QProcess::SeparateChannels);

    connect(proc, &QProcess::readyReadStandardOutput, this, &AcpClient::onStdout);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &AcpClient::onProcessFinished);
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError err) {
        // FailedToStart is reported by the waitForStarted branch below.
        if (err == QProcess::FailedToStart || proc != m_proc) return;
        if (!m_connected) emit errorOccurred(proc->errorString());
    });

    proc->start(m_resolvedExecutable, {QStringLiteral("acp")});
    if (!proc->waitForStarted(8000)) {
        const QString why = proc->errorString();
        proc->disconnect(this);
        proc->deleteLater();
        m_proc = nullptr;
        emit errorOccurred(QStringLiteral("Failed to start %1 acp: %2")
                               .arg(m_resolvedExecutable, why));
        return false;
    }

    // Handshake. We offer ACP v1 and no client-side fs/terminal methods; the
    // agent answers with the version and capabilities it will actually use,
    // and everything after this is negotiated from that answer.
    QJsonObject fs;
    fs["readTextFile"] = false;
    fs["writeTextFile"] = false;
    QJsonObject clientCaps;
    clientCaps["fs"] = fs;
    clientCaps["terminal"] = false;
    QJsonObject clientInfo;
    clientInfo["name"] = QStringLiteral("consiglio-hermes");
    clientInfo["title"] = QStringLiteral("Consiglio");
    clientInfo["version"] = QCoreApplication::applicationVersion().isEmpty()
                                ? QStringLiteral("0.1.0")
                                : QCoreApplication::applicationVersion();
    QJsonObject initParams;
    initParams["protocolVersion"] = 1;
    initParams["clientCapabilities"] = clientCaps;
    initParams["clientInfo"] = clientInfo;
    return sendRequest(m_nextReqId++, "initialize", initParams, ReqKind::Initialize);
}

void AcpClient::stopProcess() {
    if (m_restartTimer) m_restartTimer->stop();
    if (!m_proc) return;
    QProcess *proc = m_proc;
    m_proc = nullptr;
    proc->disconnect(this);
    if (proc->state() != QProcess::NotRunning) {
        // ACP over stdio: closing stdin is the polite "we're done", which
        // lets Hermes flush session state. Escalate if it doesn't go.
        proc->closeWriteChannel();
        if (!proc->waitForFinished(1500)) {
            proc->terminate();
            if (!proc->waitForFinished(1500)) {
                proc->kill();
                proc->waitForFinished(1000);
            }
        }
    }
    proc->deleteLater();
    m_connected = false;
    m_pending.clear();
    m_stdoutBuf.clear();
}

void AcpClient::shutdown() {
    m_stopping = true;
    m_restoreSessionId.clear();
    m_restoreCwd.clear();
    cancelPendingPermissions(false);
    stopProcess();
    resetSessionState();
}

void AcpClient::restartAgent() {
    if (!m_sessionId.isEmpty()) {
        m_restoreSessionId = m_sessionId;
        m_restoreCwd = m_sessionCwd;
    }
    if (m_busy) failTurn(tr("Hermes was restarted; the turn was interrupted and is not replayed."));
    cancelPendingPermissions(false);
    m_stopping = true;
    stopProcess();
    resetSessionState();
    m_restartAttempt = 0;
    if (!launchInternal()) scheduleRestart();
}

void AcpClient::setAutoRestart(bool enabled) {
    m_autoRestart = enabled;
    if (!enabled && m_restartTimer) m_restartTimer->stop();
}

void AcpClient::setRestartBackoff(int initialMs, int maxAttempts) {
    m_restartInitialMs = qMax(1, initialMs);
    m_restartMaxAttempts = qMax(0, maxAttempts);
}

void AcpClient::scheduleRestart() {
    if (!m_autoRestart || m_stopping) return;
    if (m_restartAttempt >= m_restartMaxAttempts) {
        emit agentRestartFailed(tr("Hermes did not come back after %1 attempts. "
                                   "Use Restart Hermes to try again.").arg(m_restartAttempt));
        return;
    }
    const int delay = qMin(m_restartInitialMs << qMin(m_restartAttempt, 4), 8000);
    ++m_restartAttempt;
    if (!m_restartTimer) {
        m_restartTimer = new QTimer(this);
        m_restartTimer->setSingleShot(true);
        connect(m_restartTimer, &QTimer::timeout, this, [this]() {
            if (m_stopping || (m_proc && m_proc->state() != QProcess::NotRunning)) return;
            if (!launchInternal()) scheduleRestart();
        });
    }
    emit agentRestarting(m_restartAttempt, delay);
    m_restartTimer->start(delay);
}

void AcpClient::onProcessFinished(int code, QProcess::ExitStatus status) {
    Q_UNUSED(status);
    QProcess *proc = qobject_cast<QProcess *>(sender());
    if (proc && proc != m_proc) return;  // a process we already let go of

    onStdout();  // flush trailing output
    const bool wasConnected = m_connected;
    const bool wasBusy = m_busy;

    if (m_proc) {
        m_proc->disconnect(this);
        m_proc->deleteLater();
        m_proc = nullptr;
    }
    m_connected = false;
    m_pending.clear();
    m_stdoutBuf.clear();
    cancelPendingPermissions(false);

    if (!m_sessionId.isEmpty()) {
        // Re-open this session when the agent comes back (e.g. after an update).
        m_restoreSessionId = m_sessionId;
        m_restoreCwd = m_sessionCwd;
    }
    if (wasBusy) {
        failTurn(tr("Hermes exited during the turn (code %1), possibly to apply an update. "
                    "The turn was interrupted and is not replayed; send it again once reconnected.")
                     .arg(code));
    }
    resetSessionState();

    if (code != 0 && !wasConnected) {
        emit errorOccurred(QStringLiteral("hermes acp exited with code %1").arg(code));
    }
    emit processExited(code);
    scheduleRestart();
}

// ─── Session operations ───────────────────────────────────────────────────

void AcpClient::abandonActiveSession() {
    // Tell the agent to stop the in-flight turn (ACP session/cancel is a
    // notification) and answer any permission prompt it is blocked on.
    if (m_busy && !m_sessionId.isEmpty()) {
        QJsonObject params;
        params["sessionId"] = m_sessionId;
        QJsonObject msg;
        msg["jsonrpc"] = "2.0";
        msg["method"] = "session/cancel";
        msg["params"] = params;
        sendMessage(msg);
    }
    cancelPendingPermissions(true);
    resetSessionState();
}

bool AcpClient::newSession(const QString &cwd) {
    if (!m_connected) return false;
    m_restoreSessionId.clear();
    abandonActiveSession();
    QJsonObject params;
    params["cwd"] = cwd;
    params["mcpServers"] = QJsonArray();
    m_openingSessionId.clear();
    m_openingNew = true;
    m_sessionCwd = cwd;
    return sendRequest(m_nextReqId++, "session/new", params, ReqKind::NewSession);
}

bool AcpClient::resumeSession(const QString &sessionId, const QString &cwd) {
    if (!m_connected || sessionId.isEmpty()) return false;
    m_restoreSessionId.clear();
    abandonActiveSession();
    return openSession(sessionId, cwd, false);
}

bool AcpClient::openSession(const QString &sessionId, const QString &cwd, bool restoring) {
    // Prefer session/resume when advertised; fall back to session/load
    // (agentCapabilities.loadSession). An agent that advertises neither but
    // also predates sessionCapabilities gets the legacy session/resume.
    const QJsonObject sessionCaps = m_agentCaps.value("sessionCapabilities").toObject();
    const bool capsKnown = m_agentCaps.contains("sessionCapabilities");
    QString method;
    ReqKind kind = ReqKind::ResumeSession;
    if (sessionCaps.contains("resume") || (!capsKnown && !m_agentCaps.value("loadSession").toBool())) {
        method = QStringLiteral("session/resume");
    } else if (m_agentCaps.value("loadSession").toBool()) {
        method = QStringLiteral("session/load");
        kind = ReqKind::LoadSession;
    } else {
        const QString msg = tr("This Hermes version can't re-open saved sessions.");
        if (restoring) {
            m_restoreSessionId.clear();
            emit sessionFailed(msg);
        } else {
            QTimer::singleShot(0, this, [this, msg]() { emit sessionFailed(msg); });
        }
        return false;
    }
    QJsonObject params;
    params["sessionId"] = sessionId;
    params["cwd"] = cwd;
    params["mcpServers"] = QJsonArray();
    m_openingSessionId = sessionId;
    m_openingNew = false;
    m_openingRestore = restoring;
    m_sessionCwd = cwd;
    return sendRequest(m_nextReqId++, method, params, kind);
}

bool AcpClient::listSessions(const QString &cwd) {
    if (!m_connected) return false;
    if (!canListSessions()) {
        // Not advertised: report an empty list instead of calling it.
        QTimer::singleShot(0, this, [this]() { emit sessionsListed({}); });
        return true;
    }
    QJsonObject params;
    params["cwd"] = cwd;
    return sendRequest(m_nextReqId++, "session/list", params, ReqKind::ListSessions);
}

bool AcpClient::sendPrompt(const QString &text) {
    if (!m_sessionReady || m_busy) return false;

    QJsonObject params;
    params["sessionId"] = m_sessionId;
    QJsonArray blocks;
    QJsonObject block;
    block["type"] = "text";
    block["text"] = text;
    blocks.append(block);
    params["prompt"] = blocks;

    const int id = m_nextReqId++;
    if (!sendRequest(id, "session/prompt", params, ReqKind::Prompt)) return false;
    m_busy = true;
    m_promptReqId = id;
    return true;
}

void AcpClient::stopSession() {
    // ACP has no "close session" RPC: cancel any turn in flight, then reset
    // our side and keep the process warm so another session can be opened.
    m_restoreSessionId.clear();
    abandonActiveSession();
    emit sessionStopped();
}

QString AcpClient::sessionId() const { return m_sessionId; }
QStringList AcpClient::availableModels() const { return m_models; }
QList<AcpClient::SlashCommand> AcpClient::slashCommands() const { return m_commands; }
int AcpClient::contextUsed() const { return m_contextUsed; }
int AcpClient::contextSize() const { return m_contextSize; }
bool AcpClient::isBusy() const { return m_busy; }
bool AcpClient::isReady() const { return m_sessionReady; }
bool AcpClient::isConnected() const { return m_connected; }
int AcpClient::agentProtocolVersion() const { return m_agentProtocol; }
QString AcpClient::agentName() const { return m_agentName; }
QString AcpClient::agentVersion() const { return m_agentVersion; }
QJsonObject AcpClient::agentCapabilities() const { return m_agentCaps; }

bool AcpClient::canListSessions() const {
    // Absent sessionCapabilities = an agent that predates the field: try it
    // and treat "method not found" as "no sessions".
    if (!m_agentCaps.contains("sessionCapabilities")) return true;
    return m_agentCaps.value("sessionCapabilities").toObject().contains("list");
}

bool AcpClient::canResumeSessions() const {
    if (!m_agentCaps.contains("sessionCapabilities")) return true;
    return m_agentCaps.value("sessionCapabilities").toObject().contains("resume")
        || m_agentCaps.value("loadSession").toBool();
}

void AcpClient::resetSessionState() {
    if (m_promptReqId >= 0) m_pending.remove(QString::number(m_promptReqId));
    m_promptReqId = -1;
    m_sessionReady = false;
    m_busy = false;
    m_sessionId.clear();
    m_openingSessionId.clear();
    m_openingNew = false;
    m_openingRestore = false;
    m_models.clear();
    const bool hadCommands = !m_commands.isEmpty();
    m_commands.clear();
    m_contextUsed = 0;
    m_contextSize = 0;
    // Let the palette drop the previous session's slash commands.
    if (hadCommands) emit commandsUpdated();
}

// ─── Permissions (agent -> client requests) ───────────────────────────────

bool AcpClient::respondPermission(const QString &key, const QString &optionId) {
    auto it = m_permissions.find(key);
    if (it == m_permissions.end()) return false;
    bool offered = false;
    for (const auto &o : it->request.options) offered = offered || o.optionId == optionId;
    if (!offered) return false;
    QJsonObject outcome;
    outcome["outcome"] = QStringLiteral("selected");
    outcome["optionId"] = optionId;
    QJsonObject result;
    result["outcome"] = outcome;
    QJsonObject msg;
    msg["jsonrpc"] = "2.0";
    msg["id"] = it->rpcId;
    msg["result"] = result;
    m_permissions.erase(it);
    return sendMessage(msg);
}

bool AcpClient::cancelPermission(const QString &key) {
    auto it = m_permissions.find(key);
    if (it == m_permissions.end()) return false;
    const QJsonValue rpcId = it->rpcId;
    m_permissions.erase(it);
    QJsonObject outcome;
    outcome["outcome"] = QStringLiteral("cancelled");
    QJsonObject result;
    result["outcome"] = outcome;
    QJsonObject msg;
    msg["jsonrpc"] = "2.0";
    msg["id"] = rpcId;
    msg["result"] = result;
    return sendMessage(msg);
}

QList<AcpClient::PermissionRequest> AcpClient::pendingPermissions() const {
    QList<PermissionRequest> out;
    for (const auto &p : m_permissions) out.append(p.request);
    return out;
}

void AcpClient::cancelPendingPermissions(bool notifyAgent) {
    const QStringList keys = m_permissions.keys();
    for (const QString &key : keys) {
        if (notifyAgent) cancelPermission(key);
        else m_permissions.remove(key);
        emit permissionCancelled(key);
    }
}

// ─── Wire ─────────────────────────────────────────────────────────────────

QString AcpClient::idKey(const QJsonValue &id) {
    if (id.isDouble()) return QString::number(static_cast<qint64>(id.toDouble()));
    if (id.isString()) return QStringLiteral("s:") + id.toString();
    return QString();
}

bool AcpClient::sendMessage(const QJsonObject &msg) {
    if (!m_proc || m_proc->state() != QProcess::Running) return false;
    const QByteArray line = QJsonDocument(msg).toJson(QJsonDocument::Compact) + '\n';
    return m_proc->write(line) == line.size();
}

bool AcpClient::sendRequest(int id, const QString &method, const QJsonObject &params, ReqKind kind) {
    QJsonObject msg;
    msg["jsonrpc"] = "2.0";
    msg["id"] = id;
    msg["method"] = method;
    msg["params"] = params;
    if (!sendMessage(msg)) return false;
    m_pending.insert(QString::number(id), kind);
    return true;
}

void AcpClient::sendErrorResponse(const QJsonValue &id, int code, const QString &message) {
    QJsonObject err;
    err["code"] = code;
    err["message"] = message;
    QJsonObject msg;
    msg["jsonrpc"] = "2.0";
    msg["id"] = id;
    msg["error"] = err;
    sendMessage(msg);
}

void AcpClient::onStdout() {
    if (!m_proc) return;
    m_stdoutBuf += m_proc->readAllStandardOutput();
    int nl;
    while ((nl = m_stdoutBuf.indexOf('\n')) >= 0) {
        const QString line = QString::fromUtf8(m_stdoutBuf.left(nl)).trimmed();
        m_stdoutBuf.remove(0, nl + 1);
        if (!line.isEmpty()) handleLine(line);
        if (!m_proc) break;  // a handler tore the process down
    }
}

void AcpClient::handleLine(const QString &line) {
    QJsonParseError err;
    const auto doc = QJsonDocument::fromJson(line.toUtf8(), &err);
    // Non-JSON lines (stray logging) are ignored rather than fatal.
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return;
    const auto msg = doc.object();

    const bool hasId = msg.contains("id") && !msg.value("id").isNull();
    if (msg.contains("method")) {
        // Requests and notifications FROM the agent. A request carries an id
        // in the agent's own id space; it is never a reply to one of ours,
        // even if the numbers happen to collide.
        if (hasId) {
            handleAgentRequest(msg);
        } else if (msg.value("method").toString() == QLatin1String("session/update")) {
            handleUpdate(msg.value("params").toObject());
        }
        // Unknown notifications are ignored (forward compatibility).
        return;
    }
    if (hasId) handleResponse(msg);
}

void AcpClient::handleAgentRequest(const QJsonObject &msg) {
    const QString method = msg.value("method").toString();
    const QJsonValue rpcId = msg.value("id");
    const QJsonObject params = msg.value("params").toObject();

    if (method == QLatin1String("session/request_permission")) {
        PermissionRequest req;
        req.key = idKey(rpcId);
        req.sessionId = params.value("sessionId").toString();
        const QJsonObject tool = params.value("toolCall").toObject();
        req.toolCallId = tool.value("toolCallId").toString();
        req.title = tool.value("title").toString();
        req.detail = textOfContentBlocks(tool.value("content").toArray());
        if (req.detail.isEmpty()) {
            req.detail = tool.value("rawInput").toObject().value("command").toString();
        }
        for (const auto &o : params.value("options").toArray()) {
            const auto oo = o.toObject();
            PermissionOption opt;
            opt.optionId = oo.value("optionId").toString();
            opt.name = oo.value("name").toString();
            opt.kind = oo.value("kind").toString();
            if (!opt.optionId.isEmpty()) req.options.append(opt);
        }
        const bool stale = !req.sessionId.isEmpty() && req.sessionId != m_sessionId;
        if (req.key.isEmpty() || stale || req.options.isEmpty()) {
            // Not ours to answer meaningfully: answer "cancelled" so the agent
            // isn't left waiting for its timeout.
            QJsonObject outcome;
            outcome["outcome"] = QStringLiteral("cancelled");
            QJsonObject result;
            result["outcome"] = outcome;
            QJsonObject reply;
            reply["jsonrpc"] = "2.0";
            reply["id"] = rpcId;
            reply["result"] = result;
            sendMessage(reply);
            return;
        }
        m_permissions.insert(req.key, PendingPermission{rpcId, req});
        emit permissionRequested(req);
        return;
    }

    // We advertised no fs/terminal capabilities; anything else is unknown.
    sendErrorResponse(rpcId, kMethodNotFound,
                      QStringLiteral("Method not found: %1").arg(method));
}

void AcpClient::handleResponse(const QJsonObject &msg) {
    const QString key = idKey(msg.value("id"));
    if (!m_pending.contains(key)) return;  // stale or unknown: ignore
    const ReqKind kind = m_pending.take(key);

    if (msg.contains("error")) {
        const auto e = msg.value("error").toObject();
        const int code = e.value("code").toInt();
        const QString text = e.value("message").toString();
        const QString message = QStringLiteral("ACP error %1: %2").arg(code).arg(text);
        switch (kind) {
        case ReqKind::Initialize:
            emit errorOccurred(QStringLiteral("Hermes ACP initialize failed: %1").arg(text));
            return;
        case ReqKind::ListSessions:
            if (code == kMethodNotFound) { emit sessionsListed({}); return; }
            emit errorOccurred(message);
            return;
        case ReqKind::NewSession:
        case ReqKind::ResumeSession:
        case ReqKind::LoadSession:
            if (m_openingRestore) m_restoreSessionId.clear();
            m_openingSessionId.clear();
            m_openingNew = false;
            m_openingRestore = false;
            m_sessionReady = false;
            emit sessionFailed(message);
            return;
        case ReqKind::Prompt:
            if (key == QString::number(m_promptReqId)) {
                failTurn(message);
            }
            return;
        }
        return;
    }

    const auto result = msg.value("result").toObject();
    switch (kind) {
    case ReqKind::Initialize:
        handleInitialized(result);
        break;

    case ReqKind::NewSession:
    case ReqKind::ResumeSession:
    case ReqKind::LoadSession: {
        QString id = result.value("sessionId").toString();
        if (id.isEmpty()) {
            // Resume echoes the id under _meta.hermes.sessionProvenance.acpSessionId.
            const auto prov = result.value("_meta").toObject().value("hermes").toObject()
                                  .value("sessionProvenance").toObject();
            id = prov.value("acpSessionId").toString();
        }
        if (id.isEmpty()) id = m_openingSessionId;  // session/load returns no id
        const bool restoring = m_openingRestore;
        m_sessionId = id;
        m_openingSessionId.clear();
        m_openingNew = false;
        m_openingRestore = false;
        m_models.clear();
        const auto models = result.value("models").toObject().value("availableModels").toArray();
        for (const auto &m : models) {
            const QString modelId = m.toObject().value("modelId").toString();
            if (!modelId.isEmpty()) m_models.append(modelId);
        }
        m_sessionReady = !m_sessionId.isEmpty();
        if (!m_sessionReady) {
            emit sessionFailed(tr("Hermes did not return a session id."));
            break;
        }
        m_restoreSessionId.clear();
        m_restoreCwd.clear();
        emit ready();
        if (restoring) emit sessionRestored(m_sessionId);
        break;
    }

    case ReqKind::ListSessions: {
        QList<SessionInfo> sessions;
        for (const auto &s : result.value("sessions").toArray()) {
            const auto so = s.toObject();
            SessionInfo info;
            info.id = so.value("sessionId").toString();
            info.title = so.value("title").toString();
            info.cwd = so.value("cwd").toString();
            info.updatedAt = so.value("updatedAt").toString();
            if (!info.id.isEmpty()) sessions.append(info);
        }
        emit sessionsListed(sessions);
        break;
    }

    case ReqKind::Prompt:
        if (key == QString::number(m_promptReqId)) finishTurn(result);
        break;
    }
}

void AcpClient::handleInitialized(const QJsonObject &result) {
    // Negotiate from what the agent says, not from what we expect: a newer
    // Hermes may answer with a newer protocol version, extra capabilities or
    // fields; an older one may omit some. Unknown keys are kept but unused.
    const QString previousVersion = m_agentVersion;
    m_agentProtocol = result.value("protocolVersion").toInt();
    const QJsonObject info = result.value("agentInfo").toObject();
    m_agentName = info.value("name").toString();
    m_agentVersion = info.value("version").toString();
    m_agentCaps = result.value("agentCapabilities").toObject();

    m_connected = true;
    m_restartAttempt = 0;
    if (!previousVersion.isEmpty() && !m_agentVersion.isEmpty() && previousVersion != m_agentVersion) {
        emit agentVersionChanged(previousVersion, m_agentVersion);
    }
    emit connected();

    if (!m_restoreSessionId.isEmpty() && m_connected) {
        openSession(m_restoreSessionId, m_restoreCwd, true);
    }
}

void AcpClient::handleUpdate(const QJsonObject &params) {
    // Drop updates for a session that isn't (or is no longer) ours, e.g.
    // late chunks from a turn that was cancelled by switching sessions.
    const QString sid = params.value("sessionId").toString();
    if (!sid.isEmpty() && sid != m_sessionId && sid != m_openingSessionId
        && !(m_openingNew && m_sessionId.isEmpty())) {
        return;
    }
    const QJsonObject update = params.value("update").toObject();
    const QString kind = update.value("sessionUpdate").toString();

    if (kind == "agent_message_chunk") {
        emit messageChunk(update.value("content").toObject().value("text").toString());
    } else if (kind == "agent_thought_chunk") {
        emit thoughtChunk(update.value("content").toObject().value("text").toString());
    } else if (kind == "usage_update") {
        m_contextUsed = static_cast<int>(qMin<qint64>(toInt64(update.value("used")), INT_MAX));
        m_contextSize = static_cast<int>(qMin<qint64>(toInt64(update.value("size")), INT_MAX));
        emit contextUsage(m_contextUsed, m_contextSize);
    } else if (kind == "session_info_update") {
        const QString title = update.value("title").toString();
        if (!title.isEmpty()) emit sessionTitle(title);
    } else if (kind == "tool_call" || kind == "tool_call_update") {
        const QString toolId = update.value("toolCallId").toString();
        const QString title = update.value("title").toString();
        const QString toolKind = update.value("kind").toString();
        const QString status = update.value("status").toString();
        const QString text = textOfContentBlocks(update.value("content").toArray());
        if (kind == "tool_call") {
            emit toolCallStarted(toolId, title, toolKind);
        } else {
            emit toolCallUpdated(toolId, status, text);
        }
    } else if (kind == "available_commands_update" || update.contains("availableCommands")) {
        // Hermes re-sends the full set.
        m_commands.clear();
        for (const auto &c : update.value("availableCommands").toArray()) {
            const auto co = c.toObject();
            SlashCommand sc;
            sc.name = co.value("name").toString();
            sc.description = co.value("description").toString();
            if (!sc.name.isEmpty()) m_commands.append(sc);
        }
        emit commandsUpdated();
    }
    // Any other sessionUpdate kind (plan, mode change, user echo, future
    // additions) is ignored on purpose.
}

void AcpClient::finishTurn(const QJsonObject &result) {
    m_busy = false;
    m_promptReqId = -1;
    cancelPendingPermissions(false);
    const QString stopReason = result.value("stopReason").toString();
    qint64 inputTokens = 0, outputTokens = 0;
    if (result.contains("usage")) {
        const QJsonObject usage = result.value("usage").toObject();
        inputTokens = toInt64(usage.value("inputTokens"));
        outputTokens = toInt64(usage.value("outputTokens"));
    }
    emit turnCompleted(stopReason, inputTokens, outputTokens);
}

void AcpClient::failTurn(const QString &message) {
    if (m_promptReqId >= 0) m_pending.remove(QString::number(m_promptReqId));
    m_busy = false;
    m_promptReqId = -1;
    cancelPendingPermissions(false);
    emit turnFailed(message);
}
