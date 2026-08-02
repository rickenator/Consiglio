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

QString normalizedOpenAiBaseUrl(QString baseUrl) {
    while (baseUrl.endsWith('/')) baseUrl.chop(1);
    if (!baseUrl.endsWith("/v1")) baseUrl += "/v1";
    return baseUrl;
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
    state.record.startedAt = QDateTime::currentMSecsSinceEpoch();
    state.record.lastActivity = state.record.startedAt;
    state.workingDir = !repository.isEmpty() && QDir(repository).exists()
        ? repository : QDir::currentPath();
    state.environment = QProcessEnvironment::systemEnvironment();

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

    // Codex is driven one structured turn at a time. There is deliberately no
    // full-screen TUI process to leak terminal control sequences into the GUI.
    if (state.structuredCodex) {
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

bool SessionManager::reconnectSession(const QString &sessionId) {
    return m_sessions.contains(sessionId);
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
    if (!it.value().pty || !it.value().pty->isRunning()) return false;
    it.value().pty->write((command + "\r").toUtf8());
    return true;
}

void SessionManager::setupSession(const QString &, const QString &, const QString &, const QString &) {}
void SessionManager::onProcessOutput() {}
void SessionManager::onProcessFinished(int, QProcess::ExitStatus) {}
void SessionManager::onProcessError(QProcess::ProcessError) {}
