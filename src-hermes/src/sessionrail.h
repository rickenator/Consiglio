#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QListWidget>
#include "acpclient.h"

// The left rail. Pick a workspace directory, start a fresh ACP session, or
// re-open a previous one from the list. The big button is a toggle: green
// "Start Session" when idle, red "Stop Session" while a session is active.
class SessionRail : public QWidget {
    Q_OBJECT

public:
    explicit SessionRail(QWidget *parent = nullptr);

signals:
    void startRequested(const QString &cwd);
    void stopRequested();
    void resumeRequested(const QString &sessionId, const QString &cwd);
    // Emitted when the user edits the workspace directory — refresh the list.
    void dirChanged(const QString &cwd);

public slots:
    // Toggle the big button between Start (green) and Stop (red), and update
    // the active-session info label.
    void setSessionActive(bool active, const QString &title = QString(),
                           const QString &cwd = QString());
    // Populate the previous-sessions list (from AcpClient::sessionsListed).
    void setSessions(const QList<AcpClient::SessionInfo> &sessions);

private:
    QLineEdit *m_dirEdit = nullptr;
    QPushButton *m_startBtn = nullptr;
    QLabel *m_infoLabel = nullptr;
    QListWidget *m_sessionsList = nullptr;
};
