#include "acptransport.h"

#include "hermessetup.h"

#include <QProcess>
#include <QProcessEnvironment>

namespace {

// Both transports share the same process plumbing; only the argv differs.
class ProcessShell {
public:
    static QProcess *make(QObject *owner, AcpTransport *t) {
        auto *proc = new QProcess(owner);
        proc->setProcessChannelMode(QProcess::SeparateChannels);
        QObject::connect(proc, &QProcess::readyReadStandardOutput, t, [proc, t]() {
            emit t->dataReady(proc->readAllStandardOutput());
        });
        QObject::connect(proc, &QProcess::readyReadStandardError, t, [proc, t]() {
            const QString text = QString::fromUtf8(proc->readAllStandardError());
            if (!text.trimmed().isEmpty()) emit t->stderrText(text);
        });
        QObject::connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), t,
                         [t](int code, QProcess::ExitStatus status) {
                             emit t->exited(status == QProcess::NormalExit ? code : -1,
                                            status != QProcess::NormalExit);
                         });
        return proc;
    }

    // Closing stdin is the polite ACP goodbye (Hermes flushes session state);
    // escalate only if the agent does not take the hint.
    static void stopProcess(QProcess *proc) {
        if (!proc) return;
        proc->disconnect();
        if (proc->state() != QProcess::NotRunning) {
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
    }
};

}  // namespace

// ─── Local ────────────────────────────────────────────────────────────────

LocalProcessTransport::LocalProcessTransport(const QString &executable, QObject *parent)
    : AcpTransport(parent), m_executable(executable) {}

bool LocalProcessTransport::start(QString *error) {
    if (m_proc) return false;
    m_proc = ProcessShell::make(this, this);
    // HERMES_HOME is set only for a separate Consiglio-managed copy.
    m_proc->setProcessEnvironment(HermesSetup::processEnvironment());
    m_proc->start(m_executable, {QStringLiteral("acp")});
    if (!m_proc->waitForStarted(8000)) {
        const QString why = m_proc->errorString();
        m_proc->disconnect();
        m_proc->deleteLater();
        m_proc = nullptr;
        if (error) *error = QObject::tr("Failed to start %1 acp: %2").arg(m_executable, why);
        return false;
    }
    return true;
}

void LocalProcessTransport::writeLine(const QByteArray &line) {
    if (m_proc && m_proc->state() == QProcess::Running) m_proc->write(line);
}

void LocalProcessTransport::stop() {
    QProcess *proc = m_proc;
    m_proc = nullptr;
    ProcessShell::stopProcess(proc);
}

bool LocalProcessTransport::isRunning() const {
    return m_proc && m_proc->state() == QProcess::Running;
}

QString LocalProcessTransport::describe() const { return m_executable + QStringLiteral(" acp"); }

// ─── SSH ──────────────────────────────────────────────────────────────────

SshProcessTransport::SshProcessTransport(const QString &destination, const QStringList &sshOptions,
                                        const QString &remoteExecutable, QObject *parent)
    : AcpTransport(parent),
      m_destination(destination),
      m_options(sshOptions),
      m_remoteExecutable(remoteExecutable.isEmpty() ? QStringLiteral("hermes") : remoteExecutable) {}

QStringList SshProcessTransport::commandLine() const {
    // BatchMode: never prompt (there is no terminal to prompt on).
    // ServerAlive*: a dead network becomes an exit, not a hung turn.
    // accept-new: the first connection pins the host key without a prompt;
    // a *changed* key still fails, which is the one case worth stopping for.
    QStringList args{QStringLiteral("-T"),
                     QStringLiteral("-o"), QStringLiteral("BatchMode=yes"),
                     QStringLiteral("-o"), QStringLiteral("ServerAliveInterval=15"),
                     QStringLiteral("-o"), QStringLiteral("ServerAliveCountMax=3"),
                     QStringLiteral("-o"), QStringLiteral("StrictHostKeyChecking=accept-new")};
    args += m_options;
    args << QStringLiteral("--") << m_destination << m_remoteExecutable << QStringLiteral("acp");
    return args;
}

bool SshProcessTransport::start(QString *error) {
    if (m_proc) return false;
    if (m_destination.trimmed().isEmpty()) {
        if (error) *error = QObject::tr("No SSH host was chosen.");
        return false;
    }
    m_proc = ProcessShell::make(this, this);
    m_proc->setProcessEnvironment(HermesSetup::processEnvironment());
    // argv only, never a shell: the destination and the remote command are
    // separate arguments, so a host string cannot smuggle a command in.
    m_proc->start(QStringLiteral("ssh"), commandLine());
    if (!m_proc->waitForStarted(8000)) {
        const QString why = m_proc->errorString();
        m_proc->disconnect();
        m_proc->deleteLater();
        m_proc = nullptr;
        if (error) *error = QObject::tr("Couldn't start ssh: %1").arg(why);
        return false;
    }
    return true;
}

void SshProcessTransport::writeLine(const QByteArray &line) {
    if (m_proc && m_proc->state() == QProcess::Running) m_proc->write(line);
}

void SshProcessTransport::stop() {
    QProcess *proc = m_proc;
    m_proc = nullptr;
    ProcessShell::stopProcess(proc);
}

bool SshProcessTransport::isRunning() const {
    return m_proc && m_proc->state() == QProcess::Running;
}

QString SshProcessTransport::describe() const {
    return m_destination + QStringLiteral(": ") + m_remoteExecutable + QStringLiteral(" acp");
}
