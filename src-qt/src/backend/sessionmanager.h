#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>
#include <QVariant>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSet>
#include "backend/approvalrouter.h"
#include "backend/ptyhandler.h"

struct SessionRecord {
    QString id;
    QString provider; // ollama, llama-cpp, codex, etc.
    QString status;   // running, stopped, error
    QString repository;
    QString branch;
    QString projectId;
    QString permissionMode;
    QString preferredName;
    qint64 startedAt = 0;
    qint64 lastActivity = 0;
};

class SessionManager : public QObject {
    Q_OBJECT

public:
    explicit SessionManager(QObject *parent = nullptr);
    ~SessionManager() override;

    QList<SessionRecord> listSessions() const;
    QString startSession(const QString &provider, const QString &repository = {},
                         const QString &branch = {}, const QVariantMap &options = {});
    bool stopSession(const QString &sessionId);
    bool reconnectSession(const QString &sessionId);
    bool hasSession(const QString &sessionId) const;
    bool sendCommand(const QString &sessionId, const QString &command);

signals:
    void sessionStarted(const QString &sessionId, const SessionRecord &record);
    void sessionStopped(const QString &sessionId);
    void sessionError(const QString &sessionId, const QString &error);
    void outputReceived(const QString &sessionId, const QString &data);
    void assistantMessageReceived(const QString &sessionId, const QString &message);
    void structuredErrorReceived(const QString &sessionId, const QString &error);
    void approvalRequested(const ApprovalRequest &request);

public slots:
    void stopAllSessions();

private:
    struct SessionState {
        SessionRecord record;
        PtyHandler *pty = nullptr;
        QProcess *process = nullptr;
        QString workingDir;
        QString program;
        QStringList providerArgs;
        QProcessEnvironment environment;
        bool structuredCodex = false;
        QString jsonBuffer;
        QString stderrBuffer;
        QString codexThreadId;
        QSet<QString> processedItemIds;
        bool identitySent = false;
    };
    QMap<QString, SessionState> m_sessions;
    ApprovalRouter m_approvalRouter;

    void setupSession(const QString &sessionId, const QString &provider,
                      const QString &repository, const QString &branch);
    void onProcessOutput();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);
    bool startCodexTurn(const QString &sessionId, const QString &prompt);
    void consumeCodexOutput(const QString &sessionId, const QByteArray &data, bool flush = false);
};
