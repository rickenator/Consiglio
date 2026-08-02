#pragma once

#include <QMainWindow>
#include <QStackedWidget>
#include <QListWidget>
#include <QLabel>
#include <QToolBar>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include "backend/settings.h"
#include "backend/agentdetector.h"
#include "backend/sessionmanager.h"
#include "backend/approvalrouter.h"
#include "models/eventmodel.h"

class Sidebar;
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
    void onSessionSelected(const QString &sessionId);
    void onSessionStoppedFromList(const QString &sessionId);
    void onCommandExecuted(const QString &command, const QString &workingDir);

private:
    void setupUI();
    void setupMenuBar();
    void setupStatusBar();
    void setupTray();
    void loadSettings();
    void showStartupWizardIfNeeded();
    bool scanAndSelectProvider();
    void refreshSessionList();
    void addTimelineEvent(const EventModel::EventItem &event);
    void sendCommandToActiveSession(const QString &command, const QString &workingDir);
    void updateStatusBarSessionState();

    Sidebar *m_sidebar = nullptr;
    QStackedWidget *m_content = nullptr;
    PanelManager *m_panelManager = nullptr;
    QLabel *m_approvalBadge = nullptr;
    QLabel *m_sessionStatusLabel = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_trayMenu = nullptr;

    Settings m_settings;
    AgentDetector m_agentDetector;
    SessionManager m_sessionManager;
    ApprovalRouter m_approvalRouter;
    StartupWizard *m_wizard = nullptr;

    QString m_activeSessionId;
    int m_lastPanelIndex = 0;
};
