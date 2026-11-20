#pragma once

#include <QMainWindow>
#include <QLabel>
#include <QToolBar>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QStackedWidget>
#include "backend/settings.h"
#include "backend/agentdetector.h"
#include "backend/sessionmanager.h"
#include "backend/approvalrouter.h"
#include "backend/appdatabase.h"
#include "models/eventmodel.h"

class PanelManager;
class StartupWizard;
class CommandPalette;

// Chat-first main window. The center is a single conversation pane (the
// EventTimeline) with a composer; the left is a collapsible session rail.
// A command palette (Ctrl+K) and a rich status bar expose the same controls
// you'd reach in a Hermes CLI session.
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

    // New: palette + hermes options
    void onPaletteAction(const QString &id);
    void onHermesOptions();

private:
    void setupUI();
    void setupMenuBar();
    void setupStatusBar();
    void setupTray();
    void setupPalette();
    void loadSettings();
    void showStartupWizardIfNeeded();
    bool scanAndSelectProvider();
    bool startConfiguredSession();
    bool startSession(const QString &provider, const QString &workspace,
                      const QString &sandboxMode, const QVariantMap &extraOptions = {});
    void refreshSessionList();
    void refreshProjectList();
    void loadTimelineEvents(const QString &sessionId = {});
    void addTimelineEvent(const EventModel::EventItem &event);
    void sendCommandToActiveSession(const QString &command, const QString &workingDir);
    void updateStatusBarSessionState();
    void toggleSidebar();

    QWidget *m_sidebar = nullptr;
    QWidget *m_mainContent = nullptr;
    class QStackedWidget *m_railStack = nullptr;  // Sessions | Projects pages
    PanelManager *m_panelManager = nullptr;
    CommandPalette *m_palette = nullptr;
    QLabel *m_approvalBadge = nullptr;
    QLabel *m_sessionStatusLabel = nullptr;
    QLabel *m_providerLabel = nullptr;
    QLabel *m_modelLabel = nullptr;
    QLabel *m_workdirLabel = nullptr;
    QLabel *m_permLabel = nullptr;
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
    // Display-only: the model/provider of the active session for the status bar.
    QString m_activeModel;
    QString m_activeProvider;
};
