#pragma once

#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QToolButton>
#include <QWidget>
#include "acpclient.h"

namespace ui { class ElidedLabel; class StatusDot; }

// The left rail: the workspace folder, a New Session button, the live
// session, and previous sessions (filterable) to resume.
class SessionRail : public QWidget {
    Q_OBJECT

public:
    explicit SessionRail(QWidget *parent = nullptr);

    QString workspace() const { return m_workspace; }
    void setWorkspace(const QString &dir);
    // Disable starting/resuming while Hermes isn't connected.
    void setConnected(bool connected);
    // Message shown when the list is empty ("No previous sessions", …).
    void setEmptyMessage(const QString &message);
    int sessionCount() const;

signals:
    void startRequested(const QString &cwd);
    void stopRequested();
    void resumeRequested(const QString &sessionId, const QString &cwd);
    // Emitted when the user picks a workspace directory — refresh the list.
    void dirChanged(const QString &cwd);

public slots:
    // Show/hide the live-session card.
    void setSessionActive(bool active, const QString &title = QString(),
                          const QString &cwd = QString(), const QString &sessionId = QString());
    // Populate the previous-sessions list (from AcpClient::sessionsListed).
    void setSessions(const QList<AcpClient::SessionInfo> &sessions);

private:
    void applyFilter();
    void restyle();

    QString m_workspace;
    QString m_activeId;
    bool m_connected = false;
    QPushButton *m_workspaceBtn = nullptr;
    QPushButton *m_newBtn = nullptr;
    QFrame *m_activeCard = nullptr;
    ui::StatusDot *m_activeDot = nullptr;
    ui::ElidedLabel *m_activeTitle = nullptr;
    ui::ElidedLabel *m_activeCwd = nullptr;
    QToolButton *m_stopBtn = nullptr;
    QLineEdit *m_filter = nullptr;
    QListWidget *m_sessionsList = nullptr;
    QLabel *m_emptyLabel = nullptr;
    QString m_emptyMessage;
};
