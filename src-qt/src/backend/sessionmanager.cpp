#include "sessionmanager.h"
#include <QDateTime>
#include <QUuid>
#include <QDir>
#include <QStandardPaths>

SessionManager::SessionManager(QObject *parent) : QObject(parent) {}

QList<SessionRecord> SessionManager::listSessions() const {
    QList<SessionRecord> result;
    for (const auto &state : m_sessions) {
        result.append(state.record);
    }
    return result;
}

QString SessionManager::startSession(const QString &provider, const QString &repository,
                                      const QString &branch, const QVariantMap &options) {
    QString sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    SessionState state;
    state.record.id = sessionId;
    state.record.provider = provider;
    state.record.status = "running";
    state.record.repository = repository;
    state.record.branch = branch;
    state.record.startedAt = QDateTime::currentMSecsSinceEpoch();
    state.record.lastActivity = state.record.startedAt;

    // Set working directory
    if (!repository.isEmpty() && QDir(repository).exists()) {
        state.workingDir = repository;
    } else {
        state.workingDir = QDir::currentPath();
    }

    // Create process
    state.process = new QProcess(this);
    state.process->setWorkingDirectory(state.workingDir);

    // Connect signals
    connect(state.process, &QProcess::readyReadStandardOutput, this, [this, sessionId]() {
        auto data = m_sessions[sessionId].process->readAllStandardOutput();
        emit outputReceived(sessionId, QString::fromUtf8(data));
    });

    connect(state.process, &QProcess::readyReadStandardError, this, [this, sessionId]() {
        auto data = m_sessions[sessionId].process->readAllStandardError();
        emit outputReceived(sessionId, QString::fromUtf8(data));
    });

    connect(state.process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, sessionId](int exitCode, QProcess::ExitStatus exitStatus) {
        onProcessFinished(exitCode, exitStatus);
    });

    connect(state.process, &QProcess::errorOccurred, this, [this, sessionId](QProcess::ProcessError error) {
        onProcessError(error);
    });

    // Build command based on provider
    QStringList args;
    if (provider == "ollama") {
        state.process->setProgram("ollama");
        QString model = options.value("model").toString();
        if (model.isEmpty()) model = "qwen2.5:32b-instruct-q4_K_M";
        args << "run" << model;
    } else if (provider == "llama-cpp") {
        state.process->setProgram("server"); // Assumes server is in PATH
        QString host = options.value("host").toString();
        if (host.isEmpty()) host = "127.0.0.1";
        bool ok;
        int port = options.value("port").toInt(&ok);
        if (!ok) port = 8081;
        QString model = options.value("model").toString();
        args << "--host" << host
             << "--port" << QString::number(port)
             << "-m" << model;
    } else if (provider == "codex") {
        state.process->setProgram("codex");
        args << "--repository" << repository;
    } else {
        // Default: just echo for testing
        state.process->setProgram("echo");
        args << "Session started";
    }

    state.process->start(state.process->program(), args);

    if (state.process->waitForStarted(5000)) {
        m_sessions[sessionId] = state;
        emit sessionStarted(sessionId, state.record);
    } else {
        state.record.status = "error";
        m_sessions[sessionId] = state;
        emit sessionError(sessionId, "Failed to start process");
    }

    return sessionId;
}

bool SessionManager::stopSession(const QString &sessionId) {
    auto it = m_sessions.find(sessionId);
    if (it == m_sessions.end() || !it.value().process) return false;

    it.value().process->kill();
    it.value().process->waitForFinished(3000);
    it.value().record.status = "stopped";
    emit sessionStopped(sessionId);
    return true;
}

bool SessionManager::reconnectSession(const QString &sessionId) {
    auto it = m_sessions.find(sessionId);
    if (it == m_sessions.end()) return false;

    if (it.value().process && it.value().process->state() == QProcess::NotRunning) {
        // Restart the process
        stopSession(sessionId);
        startSession(it.value().record.provider, it.value().record.repository,
                     it.value().record.branch);
        return true;
    }
    return false;
}

bool SessionManager::hasSession(const QString &sessionId) const {
    return m_sessions.contains(sessionId);
}

void SessionManager::stopAllSessions() {
    for (const auto &id : m_sessions.keys()) {
        stopSession(id);
    }
}

void SessionManager::setupSession(const QString &sessionId, const QString &provider,
                                   const QString &repository, const QString &branch) {
    // Already handled in startSession
    Q_UNUSED(sessionId);
    Q_UNUSED(provider);
    Q_UNUSED(repository);
    Q_UNUSED(branch);
}

void SessionManager::onProcessOutput() {
    // Handled via lambda connections in startSession
}

void SessionManager::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    auto process = qobject_cast<QProcess *>(sender());
    if (!process) return;

    QString sessionId;
    for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
        if (it.value().process == process) {
            sessionId = it.key();
            it.value().record.status = exitStatus == QProcess::NormalExit ? "stopped" : "error";
            m_sessions.erase(it);
            break;
        }
    }

    if (!sessionId.isEmpty()) {
        emit sessionStopped(sessionId);
    }
}

void SessionManager::onProcessError(QProcess::ProcessError error) {
    auto process = qobject_cast<QProcess *>(sender());
    if (!process) return;

    QString sessionId;
    for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
        if (it.value().process == process) {
            sessionId = it.key();
            it.value().record.status = "error";
            break;
        }
    }

    if (!sessionId.isEmpty()) {
        emit sessionError(sessionId, process->errorString());
    }
}
