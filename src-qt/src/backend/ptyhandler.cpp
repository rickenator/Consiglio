#include "ptyhandler.h"

#include <QDir>
#include <QFile>
#include <QProcessEnvironment>
#include <vector>

#if defined(Q_OS_UNIX) && !defined(Q_OS_ANDROID)
#include <QSocketNotifier>
#include <QTimer>
#include <cerrno>
#include <csignal>
#include <pthread.h>
#include <cstring>
#include <fcntl.h>
#include <pty.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

PtyHandler::PtyHandler(QObject *parent) : QObject(parent) {}

PtyHandler::~PtyHandler() {
    kill();
#if !defined(Q_OS_UNIX) || defined(Q_OS_ANDROID)
    delete m_process;
    m_process = nullptr;
#endif
}

bool PtyHandler::start(const QString &program, const QStringList &arguments,
                       const QString &workingDir, const QProcessEnvironment &environment) {
    kill();
    m_error.clear();

#if defined(Q_OS_UNIX) && !defined(Q_OS_ANDROID)
    struct winsize windowSize {};
    windowSize.ws_col = 160;
    windowSize.ws_row = 48;

    const QByteArray executable = QFile::encodeName(program);
    QList<QByteArray> encodedArguments;
    encodedArguments.reserve(arguments.size() + 1);
    encodedArguments.append(executable);
    for (const auto &argument : arguments) encodedArguments.append(argument.toLocal8Bit());

    // Encode everything the child needs before forking: between fork and exec
    // only async-signal-safe calls are allowed, so no Qt/heap work in the child.
    const QByteArray directory = workingDir.isEmpty() ? QByteArray() : QFile::encodeName(workingDir);
    // The caller's environment is the complete child environment (so removals
    // such as CODEX_PERMISSION_PROFILE take effect); an empty one inherits ours.
    QProcessEnvironment childEnvironment =
        environment.isEmpty() ? QProcessEnvironment::systemEnvironment() : environment;
    childEnvironment.insert(QStringLiteral("TERM"), QStringLiteral("xterm-256color"));
    QList<QByteArray> encodedEnvironment;
    for (const auto &entry : childEnvironment.toStringList()) encodedEnvironment.append(entry.toLocal8Bit());
    std::vector<char *> envp;
    envp.reserve(encodedEnvironment.size() + 1);
    for (auto &entry : encodedEnvironment) envp.push_back(entry.data());
    envp.push_back(nullptr);
    std::vector<char *> argv;
    argv.reserve(encodedArguments.size() + 1);
    for (auto &argument : encodedArguments) argv.push_back(argument.data());
    argv.push_back(nullptr);

    // Block signals across the fork so the child cannot run the parent's
    // handlers (e.g. a crash/termination handler) before exec resets them.
    sigset_t allSignals, previousMask;
    sigfillset(&allSignals);
    pthread_sigmask(SIG_SETMASK, &allSignals, &previousMask);
    pid_t child = forkpty(&m_masterFd, nullptr, nullptr, &windowSize);
    if (child == 0) {
        for (int signalNumber = 1; signalNumber < NSIG; ++signalNumber) ::signal(signalNumber, SIG_DFL);
        pthread_sigmask(SIG_SETMASK, &previousMask, nullptr);
        if (!directory.isEmpty() && ::chdir(directory.constData()) != 0) _exit(126);
        ::execvpe(executable.constData(), argv.data(), envp.data());
        _exit(127);
    }
    pthread_sigmask(SIG_SETMASK, &previousMask, nullptr);
    if (child < 0) {
        m_error = QString::fromLocal8Bit(std::strerror(errno));
        m_masterFd = -1;
        emit errorOccurred(QProcess::FailedToStart);
        return false;
    }

    m_childPid = child;
    const int flags = ::fcntl(m_masterFd, F_GETFL, 0);
    ::fcntl(m_masterFd, F_SETFL, flags | O_NONBLOCK);

    m_notifier = new QSocketNotifier(m_masterFd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, [this]() { readFromPty(); });
    m_childPoll = new QTimer(this);
    m_childPoll->setInterval(100);
    connect(m_childPoll, &QTimer::timeout, this, [this]() { pollChild(); });
    m_childPoll->start();
    return true;
#else
    m_process = new QProcess(this);
    if (!workingDir.isEmpty()) m_process->setWorkingDirectory(workingDir);
    m_process->setProcessEnvironment(environment);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &PtyHandler::onReadyReadStandardOutput);
    connect(m_process, &QProcess::readyReadStandardError, this, &PtyHandler::onReadyReadStandardError);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &PtyHandler::onFinished);
    connect(m_process, &QProcess::errorOccurred, this, &PtyHandler::onErrorOccurred);
    m_process->start(program, arguments);
    return m_process->waitForStarted(5000);
#endif
}

void PtyHandler::write(const QByteArray &data) {
#if defined(Q_OS_UNIX) && !defined(Q_OS_ANDROID)
    if (m_masterFd >= 0) ::write(m_masterFd, data.constData(), static_cast<size_t>(data.size()));
#else
    if (m_process && m_process->state() == QProcess::Running) m_process->write(data);
#endif
}

void PtyHandler::kill() {
#if defined(Q_OS_UNIX) && !defined(Q_OS_ANDROID)
    if (m_childPid > 0) {
        ::kill(static_cast<pid_t>(m_childPid), SIGTERM);
        int status = 0;
        for (int attempt = 0; attempt < 20; ++attempt) {
            if (::waitpid(static_cast<pid_t>(m_childPid), &status, WNOHANG) == m_childPid) break;
            ::usleep(50'000);
        }
        if (::waitpid(static_cast<pid_t>(m_childPid), &status, WNOHANG) == 0) {
            ::kill(static_cast<pid_t>(m_childPid), SIGKILL);
            ::waitpid(static_cast<pid_t>(m_childPid), &status, 0);
        }
        m_childPid = -1;
    }
    closePty();
#else
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(3000);
    }
#endif
}

bool PtyHandler::isRunning() const {
#if defined(Q_OS_UNIX) && !defined(Q_OS_ANDROID)
    return m_childPid > 0 && m_masterFd >= 0;
#else
    return m_process && m_process->state() == QProcess::Running;
#endif
}

QString PtyHandler::errorString() const { return m_error; }

#if defined(Q_OS_UNIX) && !defined(Q_OS_ANDROID)
void PtyHandler::readFromPty() {
    if (m_masterFd < 0) return;
    QByteArray output;
    char buffer[8192];
    for (;;) {
        const ssize_t count = ::read(m_masterFd, buffer, sizeof(buffer));
        if (count > 0) output.append(buffer, static_cast<int>(count));
        else break;
    }
    if (!output.isEmpty()) emit outputReceived(output);
}

void PtyHandler::pollChild() {
    if (m_childPid <= 0) return;
    int status = 0;
    const pid_t result = ::waitpid(static_cast<pid_t>(m_childPid), &status, WNOHANG);
    if (result != m_childPid) return;

    readFromPty();
    const int exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    m_childPid = -1;
    closePty();
    emit finished(exitCode);
}

void PtyHandler::closePty() {
    if (m_childPoll) {
        m_childPoll->stop();
        m_childPoll->deleteLater();
        m_childPoll = nullptr;
    }
    if (m_notifier) {
        m_notifier->setEnabled(false);
        m_notifier->deleteLater();
        m_notifier = nullptr;
    }
    if (m_masterFd >= 0) {
        ::close(m_masterFd);
        m_masterFd = -1;
    }
}
#else
void PtyHandler::onReadyReadStandardOutput() {
    if (m_process) emit outputReceived(m_process->readAllStandardOutput());
}
void PtyHandler::onReadyReadStandardError() {
    if (m_process) emit outputReceived(m_process->readAllStandardError());
}
void PtyHandler::onFinished(int exitCode, QProcess::ExitStatus) { emit finished(exitCode); }
void PtyHandler::onErrorOccurred(QProcess::ProcessError error) {
    m_error = m_process ? m_process->errorString() : QString("Unknown error");
    emit errorOccurred(error);
}
#endif
