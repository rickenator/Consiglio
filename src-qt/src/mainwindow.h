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

private:
    void setupUI();
    void setupMenuBar();
    void setupStatusBar();
    void setupTray();
    void loadSettings();
    void showStartupWizardIfNeeded();

    Sidebar *m_sidebar = nullptr;
    QStackedWidget *m_content = nullptr;
    PanelManager *m_panelManager = nullptr;
    QLabel *m_approvalBadge = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_trayMenu = nullptr;

    Settings m_settings;
    AgentDetector m_agentDetector;
    SessionManager m_sessionManager;
    ApprovalRouter m_approvalRouter;
    StartupWizard *m_wizard = nullptr;
};
