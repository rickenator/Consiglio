#include "acpclient.h"

#include <QJsonDocument>
#include <QJsonValue>

AcpClient::AcpClient(QObject *parent) : QObject(parent) {}

AcpClient::~AcpClient() {
    shutdown();
}

bool AcpClient::launch(const QString &executable) {
    if (m_proc) return false;

    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::SeparateChannels);

    connect(m_proc, &QProcess::readyReadStandardOutput, this, &AcpClient::onStdout);
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &AcpClient::onProcessFinished);
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (!m_connected)
            emit errorOccurred(m_proc->errorString());
    });

    m_proc->start(executable, {"acp"});
    if (!m_proc->waitForStarted(8000)) {
        emit errorOccurred(QStringLiteral("Failed to start %1 acp: %2")
                                .arg(executable)
                                .arg(m_proc->errorString()));
        return false;
    }

    // Handshake only — the process stays up so we can open/resume/list
    // sessions on demand without relaunching.
    QJsonObject initParams;
    initParams["protocolVersion"] = 1;
    initParams["clientCapabilities"] = QJsonObject();
    return sendRequest(1, "initialize", initParams, ReqKind::Initialize);
}

bool AcpClient::newSession(const QString &cwd) {
    if (!m_connected) return false;
    resetSessionState();
    QJsonObject params;
    params["cwd"] = cwd;
    params["mcpServers"] = QJsonArray();
    return sendRequest(m_nextReqId++, "session/new", params, ReqKind::NewSession);
}

bool AcpClient::resumeSession(const QString &sessionId, const QString &cwd) {
    if (!m_connected || sessionId.isEmpty()) return false;
    resetSessionState();
    QJsonObject params;
    params["sessionId"] = sessionId;
    params["cwd"] = cwd;
    return sendRequest(m_nextReqId++, "session/resume", params, ReqKind::ResumeSession);
}

bool AcpClient::listSessions(const QString &cwd) {
    if (!m_connected) return false;
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
    m_busy = true;
    return sendRequest(id, "session/prompt", params, ReqKind::Prompt);
}

void AcpClient::stopSession() {
    // Client-side end of the active session. The ACP adapter has no explicit
    // "close session" RPC in this version; we reset our side and keep the
    // process warm so the caller can immediately list/resume another session.
    resetSessionState();
    emit sessionStopped();
}

void AcpClient::shutdown() {
    if (!m_proc) return;
    m_proc->disconnect(this);
    if (m_proc->state() != QProcess::NotRunning) {
        m_proc->kill();
        m_proc->waitForFinished(3000);
    }
    m_proc->deleteLater();
    m_proc = nullptr;
    m_connected = false;
    resetSessionState();
}

QString AcpClient::sessionId() const { return m_sessionId; }
QStringList AcpClient::availableModels() const { return m_models; }
QList<AcpClient::SlashCommand> AcpClient::slashCommands() const { return m_commands; }
int AcpClient::contextUsed() const { return m_contextUsed; }
int AcpClient::contextSize() const { return m_contextSize; }
bool AcpClient::isBusy() const { return m_busy; }
bool AcpClient::isReady() const { return m_sessionReady; }
bool AcpClient::isConnected() const { return m_connected; }

void AcpClient::resetSessionState() {
    m_sessionReady = false;
    m_busy = false;
    m_sessionId.clear();
    m_models.clear();
    m_commands.clear();
    m_contextUsed = 0;
    m_contextSize = 0;
}

bool AcpClient::sendRequest(int id, const QString &method, const QJsonObject &params, ReqKind kind) {
    if (!m_proc || m_proc->state() == QProcess::NotRunning) return false;
    QJsonObject msg;
    msg["jsonrpc"] = "2.0";
    msg["id"] = id;
    msg["method"] = method;
    msg["params"] = params;
    const QByteArray line = QJsonDocument(msg).toJson(QJsonDocument::Compact) + '\n';
    m_proc->write(line);
    m_pending.insert(id, kind);
    return true;
}

void AcpClient::onStdout() {
    m_stdoutBuf += m_proc->readAllStandardOutput();
    int nl;
    while ((nl = m_stdoutBuf.indexOf('\n')) >= 0) {
        const QString line = QString::fromUtf8(m_stdoutBuf.left(nl)).trimmed();
        m_stdoutBuf.remove(0, nl + 1);
        if (!line.isEmpty()) handleLine(line);
    }
}

void AcpClient::handleLine(const QString &line) {
    QJsonParseError err;
    const auto doc = QJsonDocument::fromJson(line.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return;
    const auto msg = doc.object();

    // Notifications (no id): session/update stream.
    if (!msg.contains("id")) {
        if (msg["method"].toString() == "session/update") {
            handleUpdate(msg["params"].toObject()["update"].toObject());
        }
        return;
    }

    const int id = msg["id"].toInt();
    if (msg.contains("error")) {
        const auto e = msg["error"].toObject();
        emit errorOccurred(QStringLiteral("ACP error %1: %2")
                               .arg(id)
                               .arg(e["message"].toString()));
        m_pending.remove(id);
        return;
    }

    const auto result = msg["result"].toObject();
    const ReqKind kind = m_pending.value(id, ReqKind::Initialize);
    m_pending.remove(id);

    switch (kind) {
    case ReqKind::Initialize:
        // Handshake ack — the process is now usable for session ops.
        m_connected = true;
        emit connected();
        break;

    case ReqKind::NewSession:
    case ReqKind::ResumeSession: {
        // Both carry sessionId + availableModels (+ a commands update).
        m_sessionId = result["sessionId"].toString();
        // Resume echoes the id under _meta.hermes.sessionProvenance.acpSessionId.
        if (m_sessionId.isEmpty()) {
            const auto prov = result["_meta"].toObject()["hermes"].toObject()["sessionProvenance"].toObject();
            m_sessionId = prov["acpSessionId"].toString();
        }
        const auto models = result["models"].toObject()["availableModels"].toArray();
        for (const auto &m : models) {
            const auto mo = m.toObject();
            const QString modelId = mo["modelId"].toString();
            if (!modelId.isEmpty()) m_models.append(modelId);
        }
        m_sessionReady = true;
        emit ready();
        break;
    }

    case ReqKind::ListSessions: {
        QList<SessionInfo> sessions;
        const auto arr = result["sessions"].toArray();
        for (const auto &s : arr) {
            const auto so = s.toObject();
            SessionInfo info;
            info.id = so["sessionId"].toString();
            info.title = so["title"].toString();
            info.cwd = so["cwd"].toString();
            info.updatedAt = so["updatedAt"].toString();
            sessions.append(info);
        }
        emit sessionsListed(sessions);
        break;
    }

    case ReqKind::Prompt:
        finishTurn(result);
        break;
    }
}

void AcpClient::handleUpdate(const QJsonObject &update) {
    const QString kind = update["sessionUpdate"].toString();

    if (kind == "agent_message_chunk") {
        emit messageChunk(update["content"].toObject()["text"].toString());
    } else if (kind == "agent_thought_chunk") {
        emit thoughtChunk(update["content"].toObject()["text"].toString());
    } else if (kind == "usage_update") {
        m_contextUsed = update["used"].toInt();
        m_contextSize = update["size"].toInt();
        emit contextUsage(m_contextUsed, m_contextSize);
    } else if (kind == "session_info_update") {
        const QString title = update["title"].toString();
        if (!title.isEmpty()) emit sessionTitle(title);
    } else if (kind == "tool_call" || kind == "tool_call_update") {
        const QString toolId = update["toolCallId"].toString();
        const QString title = update["title"].toString();
        const QString toolKind = update["kind"].toString();
        const QString status = update["status"].toString();

        // Extract the textual content block (command or result output).
        QString text;
        const auto contentArr = update["content"].toArray();
        for (const auto &c : contentArr) {
            const auto co = c.toObject();
            if (co["type"].toString() == "content") {
                text += co["content"].toObject()["text"].toString();
            }
        }

        if (kind == "tool_call") {
            emit toolCallStarted(toolId, title, toolKind);
        } else {
            emit toolCallUpdated(toolId, status, text);
        }
    } else if (kind == "available_commands_update" ||
               update.contains("availableCommands")) {
        const auto cmds = update["availableCommands"].toArray();
        // Replace the command list (Hermes re-sends the full set).
        m_commands.clear();
        for (const auto &c : cmds) {
            const auto co = c.toObject();
            SlashCommand sc;
            sc.name = co["name"].toString();
            sc.description = co["description"].toString();
            m_commands.append(sc);
        }
        emit commandsUpdated();
    }
}

void AcpClient::finishTurn(const QJsonObject &result) {
    m_busy = false;
    const QString stopReason = result["stopReason"].toString();
    // The ACP usage object carries input/output separately (plus total, thought,
    // cached). We surface in/out so the UI can show them and compute a rate.
    qint64 inputTokens = 0, outputTokens = 0;
    if (result.contains("usage")) {
        const QJsonObject usage = result["usage"].toObject();
        inputTokens = usage["inputTokens"].toInt();
        outputTokens = usage["outputTokens"].toInt();
    }
    emit turnCompleted(stopReason, inputTokens, outputTokens);
}

void AcpClient::onProcessFinished(int code, QProcess::ExitStatus status) {
    Q_UNUSED(status);
    // Flush any trailing stdout before reporting exit.
    onStdout();
    if (code != 0 && !m_connected) {
        emit errorOccurred(QStringLiteral("hermes acp exited with code %1").arg(code));
    }
    m_connected = false;
    resetSessionState();
    emit processExited(code);
}
