#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>
#include <QVariant>
#include <QProcess>
#include "backend/approvalrouter.h"

struct SessionRecord {
    QString id;
    QString provider; // ollama, llama-cpp, codex, etc.
    QString status;   // running, stopped, error
    QString repository;
    QString branch;
    qint64 startedAt = 0;
    qint64 lastActivity = 0;
};

class SessionManager : public QObject {
    Q_OBJECT

public:
    explicit SessionManager(QObject *parent = nullptr);

    QList<SessionRecord> listSessions() const;
    QString startSession(const QString &provider, const QString &repository = {},
                         const QString &branch = {}, const QVariantMap &options = {});
    bool stopSession(const QString &sessionId);
    bool reconnectSession(const QString &sessionId);
    bool hasSession(const QString &sessionId) const;

signals:
    void sessionStarted(const QString &sessionId, const SessionRecord &record);
    void sessionStopped(const QString &sessionId);
    void sessionError(const QString &sessionId, const QString &error);
    void outputReceived(const QString &sessionId, const QString &data);
    void approvalRequested(const ApprovalRequest &request);

public slots:
    void stopAllSessions();

private:
    struct SessionState {
        SessionRecord record;
        QProcess *process = nullptr;
        QString workingDir;
    };
    QMap<QString, SessionState> m_sessions;
    ApprovalRouter m_approvalRouter;

    void setupSession(const QString &sessionId, const QString &provider,
                      const QString &repository, const QString &branch);
    void onProcessOutput();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);
};
