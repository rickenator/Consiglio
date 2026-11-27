#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

// How AcpClient reaches `hermes acp`.
//
// The protocol is newline-delimited JSON-RPC over stdio, so the client only
// ever sees bytes — which is what makes a remote agent the same client with a
// different transport. Two implementations for now:
//
//   LocalProcessTransport  `hermes acp` as a child process (the default)
//   SshProcessTransport    `ssh [-o …] host -- hermes acp`
//
// Everything above this layer (sessions, approvals, discovery, restart) is
// unchanged by where the agent lives. Failures are surfaced verbatim: ssh's
// own message is the most useful diagnostic there is.
class AcpTransport : public QObject {
    Q_OBJECT
public:
    explicit AcpTransport(QObject *parent = nullptr) : QObject(parent) {}
    ~AcpTransport() override = default;

    // Spawn and wait for the process to exist. On failure returns false and
    // fills `error` with something a user can act on.
    virtual bool start(QString *error) = 0;
    // One newline-terminated JSON-RPC message.
    virtual void writeLine(const QByteArray &line) = 0;
    // Close stdin (the polite ACP goodbye), then escalate if it does not go.
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;
    // Human-readable, e.g. "hermes acp" or "myhost.local: hermes acp".
    virtual QString describe() const = 0;

signals:
    // A chunk of the agent's stdout (ACP messages, possibly partial lines).
    void dataReady(const QByteArray &data);
    // A chunk of stderr (logs, or ssh's own diagnostics).
    void stderrText(const QString &text);
    void exited(int code, bool crashed);
};

class LocalProcessTransport : public AcpTransport {
    Q_OBJECT
public:
    explicit LocalProcessTransport(const QString &executable, QObject *parent = nullptr);

    bool start(QString *error) override;
    void writeLine(const QByteArray &line) override;
    void stop() override;
    bool isRunning() const override;
    QString describe() const override;

    QString executable() const { return m_executable; }

private:
    QString m_executable;
    class QProcess *m_proc = nullptr;
};

class SshProcessTransport : public AcpTransport {
    Q_OBJECT
public:
    // `destination` is what ssh takes as the host: `host`, `user@host`, or an
    // entry from ~/.ssh/config. `sshOptions` are extra -o/-i/-J arguments,
    // already split. `remoteExecutable` is the launcher on the far side.
    SshProcessTransport(const QString &destination, const QStringList &sshOptions,
                        const QString &remoteExecutable, QObject *parent = nullptr);

    bool start(QString *error) override;
    void writeLine(const QByteArray &line) override;
    void stop() override;
    bool isRunning() const override;
    QString describe() const override;

    QString destination() const { return m_destination; }
    // The exact argv ssh is started with (exposed for tests and diagnostics).
    QStringList commandLine() const;

private:
    QString m_destination;
    QStringList m_options;
    QString m_remoteExecutable;
    class QProcess *m_proc = nullptr;
};
