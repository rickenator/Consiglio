#include "sessionmanager.h"

#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUuid>

namespace {
QString tomlString(QString value) {
    value.replace('\\', "\\\\");
    value.replace('"', "\\\"");
    value.replace('\n', "\\n");
    value.replace('\r', "\\r");
    value.replace('\t', "\\t");
    return '"' + value + '"';
}

QString tomlStringArray(const QStringList &values) {
    QStringList escaped;
    for (const auto &value : values) escaped << tomlString(value);
    return '[' + escaped.join(',') + ']';
}

QString normalizedOpenAiBaseUrl(QString baseUrl) {
    while (baseUrl.endsWith('/')) baseUrl.chop(1);
    if (!baseUrl.endsWith("/v1")) baseUrl += "/v1";
    return baseUrl;
}

QString validatedSandboxMode(const QVariantMap &options) {
    const QString requested = options.value("sandboxMode", "workspace-write").toString();
    if (requested == "read-only" || requested == "workspace-write" ||
        requested == "danger-full-access") return requested;
    return "workspace-write";
}

QString normalizedUserName(const QVariantMap &options) {
    QString name = options.value("userName", "Dude").toString().trimmed();
    name.replace(QRegularExpression("[\\r\\n\\t]+"), " ");
    if (name.isEmpty()) name = "Dude";
    return name.left(80);
}

// Pulls the `session_id: <id>` value out of Hermes stderr output. Returns an
// empty string when no id is present.
QString extractHermesSessionId(const QString &stderrText) {
    const int idx = stderrText.lastIndexOf("session_id:");
    if (idx < 0) return {};
    const QString rest = stderrText.mid(idx + QStringLiteral("session_id:").length());
    int start = -1;
    for (int i = 0; i < rest.length(); ++i) {
        if (!rest.at(i).isSpace()) { start = i; break; }
    }
    if (start < 0) return {};
    int end = start;
    while (end < rest.length() && !rest.at(end).isSpace()) ++end;
    return rest.mid(start, end - start);
}
}

SessionManager::SessionManager(QObject *parent) : QObject(parent) {}
SessionManager::~SessionManager() { stopAllSessions(); }

QList<SessionRecord> SessionManager::listSessions() const {
    QList<SessionRecord> result;
    for (const auto &state : m_sessions) result.append(state.record);
    return result;
}

QString SessionManager::startSession(const QString &provider, const QString &repository,
                                     const QString &branch, const QVariantMap &options) {
    const QString sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    SessionState state;
    state.record.id = sessionId;
    state.record.provider = provider;
    state.record.status = "running";
    state.record.repository = repository;
    state.record.branch = branch;
    state.record.projectId = options.value("projectId").toString();
    state.record.permissionMode = validatedSandboxMode(options);
    state.record.preferredName = normalizedUserName(options);
    state.record.startedAt = QDateTime::currentMSecsSinceEpoch();
    state.record.lastActivity = state.record.startedAt;
    state.workingDir = !repository.isEmpty() && QDir(repository).exists()
        ? repository : QDir::currentPath();
    state.environment = QProcessEnvironment::systemEnvironment();
    // These describe the process that launched Consiglio; they must not
    // silently override the permission selected for a new child session.
    state.environment.remove("CODEX_PERMISSION_PROFILE");
    state.environment.remove("CODEX_SANDBOX_NETWORK_DISABLED");
    state.environment.remove("CODEX_THREAD_ID");
    state.environment.remove("CODEX_CI");

    if (provider == "ollama") {
        state.program = "ollama";
        const QString baseUrl = options.value("baseUrl").toString();
        if (!baseUrl.isEmpty()) state.environment.insert("OLLAMA_HOST", baseUrl);
        QString model = options.value("model").toString();
        if (model.isEmpty()) model = "qwen2.5:32b-instruct-q4_K_M";
        state.providerArgs << "run" << model;
    } else if (provider == "llama-cpp") {
        state.program = "server";
        QString host = options.value("host").toString();
        if (host.isEmpty()) host = "127.0.0.1";
        bool ok = false;
        int port = options.value("port").toInt(&ok);
        if (!ok) port = 8081;
        state.providerArgs << "--host" << host << "--port" << QString::number(port)
                           << "-m" << options.value("model").toString();
    } else if (provider == "codex") {
        state.program = "codex";
        state.structuredCodex = true;
        state.providerArgs << "-c"
                           << QString("sandbox_mode=%1").arg(
                                  tomlString(state.record.permissionMode));
    } else if (provider == "hermes") {
        // Hermes Agent is driven one structured turn at a time, mirroring the
        // Codex pattern: `hermes chat -q <prompt>` for a new session, or
        // `hermes chat --resume <sessionId> -q <prompt>` to continue. The
        // final response arrives on stdout; the `session_id:` line and any
        // banners arrive on stderr, which we parse separately.
        state.program = "hermes";
        state.structuredHermes = true;
        // Only the per-turn options live here; the subcommand, `-q`, and the
        // prompt are assembled in startHermesTurn so the prompt always sits
        // directly after `-q` (argparse requires the value to follow the flag).
        const QString model = options.value("model").toString();
        if (!model.isEmpty()) state.providerArgs << "-m" << model;
        const QString providerName = options.value("providerName").toString();
        if (!providerName.isEmpty()) state.providerArgs << "--provider" << providerName;
        const QString reasoning = options.value("reasoning").toString();
        if (!reasoning.isEmpty()) state.providerArgs << "--reasoning" << reasoning;
        const QString toolsets = options.value("toolsets").toString();
        if (!toolsets.isEmpty()) state.providerArgs << "-t" << toolsets;
        const QString skills = options.value("skills").toString();
        if (!skills.isEmpty()) state.providerArgs << "-s" << skills;
        const QString profile = options.value("profile").toString();
        if (!profile.isEmpty()) state.providerArgs << "--profile" << profile;
        const int maxTurns = options.value("maxTurns").toInt();
        if (maxTurns > 0) state.providerArgs << "--max-turns" << QString::number(maxTurns);
        if (options.value("yolo", false).toBool()) state.providerArgs << "--yolo";
        if (options.value("worktree", false).toBool()) state.providerArgs << "--worktree";
        // Keep the conversation quiet and machine-readable.
        state.providerArgs << "-Q" << "--source" << "consiglio";
    } else if (provider == "remote_llamacpp") {
        state.program = "codex";
        state.structuredCodex = true;
        const QString baseUrl = normalizedOpenAiBaseUrl(options.value("baseUrl").toString());
        const QString model = options.value("model").toString();
        const QString apiKey = options.value("apiKey", "llama.cpp").toString();
        const QString codexHome = QStandardPaths::writableLocation(
            QStandardPaths::AppDataLocation) + "/codex-remote";
        QDir().mkpath(codexHome);

        state.environment.insert("OPENAI_BASE_URL", baseUrl);
        state.environment.insert("OPENAI_API_BASE", baseUrl);
        state.environment.insert("OPENAI_API_KEY", apiKey.isEmpty() ? "llama.cpp" : apiKey);
        state.environment.insert("OPENAI_MODEL", model);
        state.environment.insert("CODEX_OSS_BASE_URL", baseUrl);
        state.environment.insert("CODEX_HOME", codexHome);
        state.providerArgs
            << "-c" << QString("sandbox_mode=%1").arg(
                           tomlString(state.record.permissionMode))
            << "-c" << "features.multi_agent=false"
            << "-c" << "model_supports_reasoning_summaries=false"
            << "-c" << "model_reasoning_summary=\"none\""
            << "-c" << "web_search=\"disabled\""
            << "-c" << QString("model=%1").arg(tomlString(model))
            << "-c" << "model_provider=\"remote_llamacpp\""
            << "-c" << "model_providers.remote_llamacpp.name=\"Remote llama.cpp\""
            << "-c" << QString("model_providers.remote_llamacpp.base_url=%1").arg(tomlString(baseUrl))
            << "-c" << "model_providers.remote_llamacpp.wire_api=\"responses\""
            << "-c" << "model_providers.remote_llamacpp.env_key=\"OPENAI_API_KEY\"";
    } else if (provider == "open-interpreter") {
        state.program = "interpreter";
    } else {
        state.program = "echo";
        state.providerArgs << "Session started";
    }

    if (state.structuredCodex) {
        const QString identityInstruction = QString(
            "The user's preferred name is %1. Address the user as %1 when using a name, "
            "and do not infer or substitute another name.").arg(state.record.preferredName);
        state.providerArgs << "-c"
                           << QString("developer_instructions=%1").arg(
                                  tomlString(identityInstruction));
        const QString historyMcpScript = options.value("historyMcpScript").toString();
        const QString historyDatabasePath = options.value("historyDatabasePath").toString();
        if (!historyMcpScript.isEmpty() && !historyDatabasePath.isEmpty()) {
            state.providerArgs
                << "-c" << "mcp_servers.consiglio_history.command=\"python3\""
                << "-c" << QString("mcp_servers.consiglio_history.args=%1").arg(
                    tomlStringArray({historyMcpScript, "--database", historyDatabasePath}));
        }
    }

    // Codex and Hermes are driven one structured turn at a time. There is
    // deliberately no full-screen TUI process to leak terminal control
    // sequences into the GUI.
    if (state.structuredCodex || state.structuredHermes) {
        m_sessions.insert(sessionId, state);
        emit sessionStarted(sessionId, state.record);
        return sessionId;
    }

    state.pty = new PtyHandler(this);
    connect(state.pty, &PtyHandler::outputReceived, this,
            [this, sessionId](const QByteArray &data) {
        emit outputReceived(sessionId, QString::fromUtf8(data));
    });
    connect(state.pty, &PtyHandler::finished, this, [this, sessionId](int) {
        auto it = m_sessions.find(sessionId);
        if (it == m_sessions.end()) return;
        auto *pty = it.value().pty;
        m_sessions.erase(it);
        if (pty) pty->deleteLater();
        emit sessionStopped(sessionId);
    });
    connect(state.pty, &PtyHandler::errorOccurred, this,
            [this, sessionId](QProcess::ProcessError) {
        auto it = m_sessions.find(sessionId);
        if (it == m_sessions.end() || !it.value().pty) return;
        emit sessionError(sessionId, it.value().pty->errorString());
    });

    if (state.pty->start(state.program, state.providerArgs,
                         state.workingDir, state.environment)) {
        m_sessions.insert(sessionId, state);
        emit sessionStarted(sessionId, state.record);
    } else {
        state.record.status = "error";
        m_sessions.insert(sessionId, state);
        emit sessionError(sessionId, state.pty->errorString().isEmpty()
            ? QString("Failed to start process") : state.pty->errorString());
    }
    return sessionId;
}

bool SessionManager::startCodexTurn(const QString &sessionId, const QString &prompt) {
    auto it = m_sessions.find(sessionId);
    if (it == m_sessions.end() || !it.value().structuredCodex ||
        (it.value().process && it.value().process->state() != QProcess::NotRunning)) return false;

    auto *process = new QProcess(this);
    it.value().process = process;
    it.value().jsonBuffer.clear();
    it.value().stderrBuffer.clear();
    it.value().processedItemIds.clear();
    process->setWorkingDirectory(it.value().workingDir);
    process->setProcessEnvironment(it.value().environment);
    process->setProcessChannelMode(QProcess::SeparateChannels);

    connect(process, &QProcess::readyReadStandardOutput, this, [this, sessionId, process]() {
        consumeCodexOutput(sessionId, process->readAllStandardOutput());
    });
    connect(process, &QProcess::readyReadStandardError, this, [this, sessionId, process]() {
        auto current = m_sessions.find(sessionId);
        if (current != m_sessions.end())
            current.value().stderrBuffer += QString::fromUtf8(process->readAllStandardError());
    });
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, sessionId, process](int exitCode, QProcess::ExitStatus) {
        consumeCodexOutput(sessionId, process->readAllStandardOutput(), true);
        auto current = m_sessions.find(sessionId);
        if (current == m_sessions.end()) {
            process->deleteLater();
            return;
        }
        current.value().stderrBuffer += QString::fromUtf8(process->readAllStandardError());
        const QString stderrText = current.value().stderrBuffer.trimmed();
        current.value().process = nullptr;
        process->deleteLater();
        if (exitCode != 0) {
            emit structuredErrorReceived(sessionId,
                stderrText.isEmpty() ? QString("Codex turn stopped with code %1").arg(exitCode)
                                     : stderrText);
        }
    });
    connect(process, &QProcess::errorOccurred, this,
            [this, sessionId, process](QProcess::ProcessError) {
        emit structuredErrorReceived(sessionId, process->errorString());
    });

    QStringList args;
    if (it.value().codexThreadId.isEmpty()) {
        args << "exec" << "--json" << "--color" << "never"
             << "--skip-git-repo-check" << it.value().providerArgs << prompt;
    } else {
        args << "exec" << "resume" << "--json" << "--skip-git-repo-check"
             << it.value().providerArgs << it.value().codexThreadId << prompt;
    }
    process->start(it.value().program, args);
    if (!process->waitForStarted(5000)) {
        const QString error = process->errorString();
        it.value().process = nullptr;
        process->deleteLater();
        emit structuredErrorReceived(sessionId, error);
        return false;
    }
    // The prompt is already an argv value. EOF prevents `codex exec` from
    // waiting indefinitely for an additional piped-stdin block.
    process->closeWriteChannel();
    return true;
}

void SessionManager::consumeCodexOutput(const QString &sessionId,
                                        const QByteArray &data, bool flush) {
    auto it = m_sessions.find(sessionId);
    if (it == m_sessions.end()) return;
    it.value().jsonBuffer += QString::fromUtf8(data);
    if (flush && !it.value().jsonBuffer.endsWith('\n')) it.value().jsonBuffer += '\n';

    int newline = -1;
    while ((newline = it.value().jsonBuffer.indexOf('\n')) >= 0) {
        const QString line = it.value().jsonBuffer.left(newline).trimmed();
        it.value().jsonBuffer.remove(0, newline + 1);
        if (line.isEmpty()) continue;

        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(line.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) continue;
        const auto event = document.object();
        const QString type = event.value("type").toString();
        if (type == "thread.started") {
            it.value().codexThreadId = event.value("thread_id").toString();
            emit codexThreadIdReceived(sessionId, it.value().codexThreadId);
            continue;
        }

        const auto item = event.value("item").toObject();
        const QString itemId = item.value("id").toString();
        if (type == "item.completed" && !itemId.isEmpty()) {
            if (it.value().processedItemIds.contains(itemId)) continue;
            it.value().processedItemIds.insert(itemId);
        }
        if (type == "item.completed" && item.value("type").toString() == "agent_message") {
            QString text = item.value("text").toString();
            if (text.isEmpty()) text = item.value("content").toString();
            if (!text.trimmed().isEmpty())
                emit assistantMessageReceived(sessionId, text.trimmed());
        } else if (type == "item.completed" &&
                   item.value("type").toString() == "command_execution") {
            QString text = item.value("command").toString();
            const QString output = item.value("aggregated_output").toString().trimmed();
            if (!output.isEmpty()) text += "\n" + output;
            if (!text.trimmed().isEmpty()) emit outputReceived(sessionId, text.trimmed());
        } else if (type == "error") {
            const QString message = event.value("message").toString();
            if (!message.isEmpty()) emit structuredErrorReceived(sessionId, message);
        } else if (type == "turn.failed") {
            const QString message = event.value("error").toObject().value("message").toString();
            if (!message.isEmpty()) emit structuredErrorReceived(sessionId, message);
        }
    }
}

bool SessionManager::startHermesTurn(const QString &sessionId, const QString &prompt) {
    auto it = m_sessions.find(sessionId);
    if (it == m_sessions.end() || !it.value().structuredHermes ||
        (it.value().process && it.value().process->state() != QProcess::NotRunning)) return false;

    auto *process = new QProcess(this);
    it.value().process = process;
    it.value().jsonBuffer.clear();   // reused as the stdout buffer for Hermes
    it.value().stderrBuffer.clear();
    process->setWorkingDirectory(it.value().workingDir);
    process->setProcessEnvironment(it.value().environment);
    process->setProcessChannelMode(QProcess::SeparateChannels);

    connect(process, &QProcess::readyReadStandardOutput, this, [this, sessionId, process]() {
        consumeHermesOutput(sessionId, process->readAllStandardOutput());
    });
    connect(process, &QProcess::readyReadStandardError, this, [this, sessionId, process]() {
        auto current = m_sessions.find(sessionId);
        if (current != m_sessions.end())
            current.value().stderrBuffer += QString::fromUtf8(process->readAllStandardError());
    });
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, sessionId, process](int exitCode, QProcess::ExitStatus) {
        consumeHermesOutput(sessionId, process->readAllStandardOutput(), true);
        auto current = m_sessions.find(sessionId);
        if (current != m_sessions.end())
            current.value().stderrBuffer += QString::fromUtf8(process->readAllStandardError());
        // Capture the hermes session id from stderr so later turns resume.
        if (current != m_sessions.end()) {
            const QString sid = extractHermesSessionId(current.value().stderrBuffer);
            if (!sid.isEmpty() && sid != current.value().codexThreadId) {
                current.value().codexThreadId = sid;
                emit codexThreadIdReceived(sessionId, sid);
            }
        }
        const QString stderrText = current.value().stderrBuffer.trimmed();
        current.value().process = nullptr;
        process->deleteLater();
        if (exitCode != 0) {
            // Surface the tail of stderr (the real error), not the banner noise.
            emit structuredErrorReceived(sessionId,
                stderrText.isEmpty() ? QString("Hermes turn stopped with code %1").arg(exitCode)
                                     : stderrText);
        }
    });
    connect(process, &QProcess::errorOccurred, this,
            [this, sessionId, process](QProcess::ProcessError) {
        emit structuredErrorReceived(sessionId, process->errorString());
    });

    QStringList args;
    args << "chat";
    args += it.value().providerArgs;
    if (!it.value().codexThreadId.isEmpty())
        args << "--resume" << it.value().codexThreadId;
    args << "-q" << prompt;
    process->start(it.value().program, args);
    if (!process->waitForStarted(5000)) {
        const QString error = process->errorString();
        it.value().process = nullptr;
        process->deleteLater();
        emit structuredErrorReceived(sessionId, error);
        return false;
    }
    process->closeWriteChannel();
    return true;
}

void SessionManager::consumeHermesOutput(const QString &sessionId,
                                         const QByteArray &data, bool flush) {
    auto it = m_sessions.find(sessionId);
    if (it == m_sessions.end()) return;
    it.value().jsonBuffer += QString::fromUtf8(data);
    if (!flush) return;  // accumulate until the turn completes

    // The final response is everything on stdout minus any `session_id:` line
    // (Hermes normally prints that to stderr, but we guard against leakage).
    QString text = it.value().jsonBuffer;
    it.value().jsonBuffer.clear();
    if (!text.endsWith('\n')) text += '\n';
    const int idLine = text.indexOf("session_id:");
    if (idLine >= 0) text.remove(idLine, text.length() - idLine);
    text = text.trimmed();
    if (text.isEmpty()) return;
    emit assistantMessageReceived(sessionId, text);
}

bool SessionManager::stopSession(const QString &sessionId) {
    auto it = m_sessions.find(sessionId);
    if (it == m_sessions.end()) return false;
    if (it.value().process) {
        it.value().process->disconnect();
        it.value().process->kill();
        it.value().process->waitForFinished(3000);
        delete it.value().process;
    }
    if (it.value().pty) {
        it.value().pty->disconnect();
        it.value().pty->kill();
        delete it.value().pty;
    }
    m_sessions.erase(it);
    emit sessionStopped(sessionId);
    return true;
}

bool SessionManager::reconnectSession(const SessionRecord &record) {
    if (m_sessions.contains(record.id)) return true;

    // Structured Codex and Hermes sessions with a thread/session ID can be
    // reconnected; everything else (PTY providers) cannot.
    const bool isCodex = record.provider == "codex";
    const bool isHermes = record.provider == "hermes";
    if ((!isCodex && !isHermes) || record.codexThreadId.isEmpty()) return false;

    SessionState state;
    state.record = record;
    state.record.status = "running";
    state.workingDir = !record.repository.isEmpty() && QDir(record.repository).exists()
        ? record.repository : QDir::currentPath();
    state.environment = QProcessEnvironment::systemEnvironment();
    state.environment.remove("CODEX_PERMISSION_PROFILE");
    state.environment.remove("CODEX_SANDBOX_NETWORK_DISABLED");
    state.environment.remove("CODEX_THREAD_ID");
    state.environment.remove("CODEX_CI");

    if (isCodex) {
        state.structuredCodex = true;
        state.program = "codex";
        state.providerArgs << "-c"
                           << QString("sandbox_mode=%1").arg(tomlString(record.permissionMode));
        const QString identityInstruction = QString(
            "The user's preferred name is %1. Address the user as %1 when using a name, "
            "and do not infer or substitute another name.").arg(record.preferredName);
        state.providerArgs << "-c"
                           << QString("developer_instructions=%1").arg(
                                  tomlString(identityInstruction));
    } else {
        state.structuredHermes = true;
        state.program = "hermes";
        // Quiet, machine-readable turns; the prompt is added per turn.
        state.providerArgs << "-Q" << "--source" << "consiglio";
    }

    // Restore the thread/session ID so subsequent commands resume.
    state.codexThreadId = record.codexThreadId;

    m_sessions.insert(record.id, state);
    emit sessionStarted(record.id, state.record);
    return true;
}

bool SessionManager::hasSession(const QString &sessionId) const {
    return m_sessions.contains(sessionId);
}

void SessionManager::stopAllSessions() {
    const QStringList ids = m_sessions.keys();
    for (const auto &id : ids) stopSession(id);
}

bool SessionManager::sendCommand(const QString &sessionId, const QString &command) {
    auto it = m_sessions.find(sessionId);
    if (it == m_sessions.end()) return false;
    it.value().record.lastActivity = QDateTime::currentMSecsSinceEpoch();
    if (it.value().structuredCodex) return startCodexTurn(sessionId, command);
    if (it.value().structuredHermes) return startHermesTurn(sessionId, command);
    if (!it.value().pty || !it.value().pty->isRunning()) return false;
    QString effectiveCommand = command;
    if (!it.value().identitySent) {
        effectiveCommand = QString(
            "My preferred name is %1. Address me by that name when appropriate. %2")
            .arg(it.value().record.preferredName, command);
        it.value().identitySent = true;
    }
    it.value().pty->write((effectiveCommand + "\r").toUtf8());
    return true;
}

void SessionManager::setupSession(const QString &, const QString &, const QString &, const QString &) {}
void SessionManager::onProcessOutput() {}
void SessionManager::onProcessFinished(int, QProcess::ExitStatus) {}
void SessionManager::onProcessError(QProcess::ProcessError) {}
