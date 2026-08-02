#pragma once

#include <QMainWindow>
#include <QStackedWidget>
#include <QLabel>
#include <QToolBar>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include "backend/settings.h"
#include "backend/agentdetector.h"
#include "backend/sessionmanager.h"
#include "backend/approvalrouter.h"
#include "backend/appdatabase.h"
#include "models/eventmodel.h"

class PanelManager;
class StartupWizard;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    Settings &settings();
    AgentDetector &agentDetector();
    SessionManager &sessionManager();
    ApprovalRouter &approvalRouter();

signals:
    void sessionStarted(const QString &sessionId);
    void sessionStopped(const QString &sessionId);
    void approvalRequested(const ApprovalRequest &request);
    void approvalResolved(const QString &id, bool approved);

private slots:
    void onNewSession();
    void onSettings();
    void onStartupWizard();
    void onQuit();
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void onApprovalRequested(const ApprovalRequest &request);
    void onPendingApprovalsChanged(int count);

    // Wiring slots
    void onSessionStarted(const QString &sessionId, const SessionRecord &record);
    void onSessionStopped(const QString &sessionId);
    void onSessionError(const QString &sessionId, const QString &error);
    void onOutputReceived(const QString &sessionId, const QString &data);
    void onAssistantMessageReceived(const QString &sessionId, const QString &message);
    void onStructuredErrorReceived(const QString &sessionId, const QString &error);
    void onCodexThreadIdReceived(const QString &sessionId, const QString &threadId);
    void onSessionSelected(const QString &sessionId);
    void onSessionStoppedFromList(const QString &sessionId);
    void onClearProjectFilterRequested();
    void onCommandExecuted(const QString &command, const QString &workingDir);
    void onProjectSelected(const QString &projectId, const QString &projectName);

private:
    void setupUI();
    void setupMenuBar();
    void setupStatusBar();
    void setupTray();
    void loadSettings();
    void showStartupWizardIfNeeded();
    bool scanAndSelectProvider();
    bool startConfiguredSession();
    bool startSession(const QString &provider, const QString &workspace,
                      const QString &sandboxMode);
    void refreshSessionList();
    void refreshProjectList();
    void loadTimelineEvents(const QString &sessionId = {});
    void addTimelineEvent(const EventModel::EventItem &event);
    void sendCommandToActiveSession(const QString &command, const QString &workingDir);
    void updateStatusBarSessionState();

    QStackedWidget *m_content = nullptr;
    PanelManager *m_panelManager = nullptr;
    QLabel *m_approvalBadge = nullptr;
    QLabel *m_sessionStatusLabel = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_trayMenu = nullptr;

    AppDatabase m_database;
    Settings m_settings;
    AgentDetector m_agentDetector;
    SessionManager m_sessionManager;
    ApprovalRouter m_approvalRouter;
    StartupWizard *m_wizard = nullptr;

    QString m_activeSessionId;
    QString m_viewedSessionId;
    QString m_selectedProjectId;
    QString m_selectedProjectName;
    QString m_selectedProjectWorkspace;
};
