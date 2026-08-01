#include "ptyhandler.h"
#include <QDir>

PtyHandler::PtyHandler(QObject *parent) : QObject(parent) {}

PtyHandler::~PtyHandler() {
    if (m_process) {
        kill();
        delete m_process;
        m_process = nullptr;
    }
}

bool PtyHandler::start(const QString &program, const QStringList &arguments, const QString &workingDir) {
    if (m_process) {
        kill();
        delete m_process;
    }

    m_process = new QProcess(this);

    if (!workingDir.isEmpty()) {
        m_process->setWorkingDirectory(workingDir);
    }

    // Connect signals
    connect(m_process, &QProcess::readyReadStandardOutput, this, &PtyHandler::onReadyReadStandardOutput);
    connect(m_process, &QProcess::readyReadStandardError, this, &PtyHandler::onReadyReadStandardError);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &PtyHandler::onFinished);
    connect(m_process, &QProcess::errorOccurred, this, &PtyHandler::onErrorOccurred);

    m_process->start(program, arguments);
    return m_process->waitForStarted(5000);
}

void PtyHandler::write(const QByteArray &data) {
    if (m_process && m_process->state() == QProcess::Running) {
        m_process->write(data);

    }
}

void PtyHandler::kill() {
    if (m_process) {
        m_process->kill();
        m_process->waitForFinished(3000);
    }
}

bool PtyHandler::isRunning() const {
    return m_process && m_process->state() == QProcess::Running;
}

QString PtyHandler::errorString() const {
    return m_error;
}

void PtyHandler::onReadyReadStandardOutput() {
    if (m_process) {
        emit outputReceived(m_process->readAllStandardOutput());
    }
}

void PtyHandler::onReadyReadStandardError() {
    if (m_process) {
        emit outputReceived(m_process->readAllStandardError());
    }
}

void PtyHandler::onFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    emit finished(exitCode);
    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }
}

void PtyHandler::onErrorOccurred(QProcess::ProcessError error) {
    m_error = m_process ? m_process->errorString() : "Unknown error";
    emit errorOccurred(error);
}
