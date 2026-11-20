#include "mainwindow.h"
#include "panelmanager.h"
#include "sessionlist.h"
#include "eventtimeline.h"
#include "startupwizard.h"
#include "settingsdialog.h"
#include "providerselectiondialog.h"
#include "newsessiondialog.h"
#include "projectpanel.h"
#include "approvaldialog.h"
#include "hermesoptionsdialog.h"
#include "commandpalette.h"
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolBar>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QIcon>
#include <QApplication>
#include <QStyle>
#include <QFileDialog>
#include <QFileInfo>
#include <QDateTime>
#include <QGuiApplication>
#include <QScreen>
#include <QWindow>
#include <QTimer>
#include "uimetrics.h"

static QRect clampRectToScreen(const QRect &rect, const QRect &available);

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), m_database(), m_settings(nullptr, &m_database)
{
    setWindowTitle("Consiglio");
    const QScreen *screen = QGuiApplication::screenAt(QPoint(0, 0));
    const QSize available = screen ? screen->availableGeometry().size() : QSize(1440, 900);
    setMinimumSize(qMin(UiMetrics::px(640), available.width()),
                   qMin(UiMetrics::px(400), available.height()));
    resize(qRound(available.width() * 0.84), qRound(available.height() * 0.84));

    setupUI();
    loadSettings();

    // Center on primary screen after UI is set up and shown
    QTimer::singleShot(100, this, [this, screen]() {
        if (screen) {
            const QRect geo = screen->availableGeometry();
            move(geo.x() + (geo.width() - width()) / 2,
                 geo.y() + (geo.height() - height()) / 2);
        }
    });
    m_database.markRunningSessionsInterrupted(QDateTime::currentMSecsSinceEpoch());
    refreshProjectList();
    refreshSessionList();
    loadTimelineEvents();
    QTimer::singleShot(0, this, [this]() { showStartupWizardIfNeeded(); });
}

MainWindow::~MainWindow() {
    m_database.setPreference("windowGeometry", saveGeometry());
    m_database.setPreference("windowState", saveState());
    m_sessionManager.stopAllSessions();
}

Settings &MainWindow::settings() { return m_settings; }
AgentDetector &MainWindow::agentDetector() { return m_agentDetector; }
SessionManager &MainWindow::sessionManager() { return m_sessionManager; }
ApprovalRouter &MainWindow::approvalRouter() { return m_approvalRouter; }

void MainWindow::setupUI() {
    // Central widget: collapsible session rail (left) + conversation pane (right).
    auto *central = new QWidget(this);
    auto *layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // ─── Left: session rail (stacked: Sessions | Projects) ───────────────
    m_sidebar = new QWidget(this);
    auto *sidebarLayout = new QVBoxLayout(m_sidebar);
    sidebarLayout->setContentsMargins(0, 0, 0, 0);
    sidebarLayout->setSpacing(0);

    m_panelManager = new PanelManager(this);
    m_panelManager->createPanels(this);

    // The rail is a stacked widget so both the session list and the projects
    // view have a real home (Projects was previously orphaned). Default to
    // Sessions; the View menu switches pages.
    m_railStack = new QStackedWidget(this);
    m_railStack->addWidget(m_panelManager->sessionsPanel());   // page 0
    m_railStack->addWidget(m_panelManager->projectsPanel());   // page 1
    m_railStack->setCurrentIndex(0);
    sidebarLayout->addWidget(m_railStack);
    layout->addWidget(m_sidebar, 0);

    // ─── Right: conversation pane (timeline + composer) ──────────────────
    m_mainContent = new QWidget(this);
    auto *mainLayout = new QVBoxLayout(m_mainContent);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->addWidget(m_panelManager->timelinePanel());
    layout->addWidget(m_mainContent, 1);

    // ─── Backend → Frontend wiring ───────────────────────────────────────
    connect(&m_sessionManager, &SessionManager::sessionStarted,
            this, &MainWindow::onSessionStarted);
    connect(&m_sessionManager, &SessionManager::sessionStopped,
            this, &MainWindow::onSessionStopped);
    connect(&m_sessionManager, &SessionManager::sessionError,
            this, &MainWindow::onSessionError);
    connect(&m_sessionManager, &SessionManager::outputReceived,
            this, &MainWindow::onOutputReceived);
    connect(&m_sessionManager, &SessionManager::assistantMessageReceived,
            this, &MainWindow::onAssistantMessageReceived);
    connect(&m_sessionManager, &SessionManager::structuredErrorReceived,
            this, &MainWindow::onStructuredErrorReceived);
    connect(&m_sessionManager, &SessionManager::codexThreadIdReceived,
            this, &MainWindow::onCodexThreadIdReceived);

    // Panel signals → MainWindow actions
    connect(m_panelManager->sessionsPanel(), &SessionList::sessionSelected,
            this, &MainWindow::onSessionSelected);
    connect(m_panelManager->sessionsPanel(), &SessionList::sessionStopped,
            this, &MainWindow::onSessionStoppedFromList);
    connect(m_panelManager->sessionsPanel(), &SessionList::clearProjectFilterRequested,
            this, &MainWindow::onClearProjectFilterRequested);
    connect(m_panelManager->timelinePanel(), &EventTimeline::commandExecuted,
            this, &MainWindow::onCommandExecuted);
    connect(m_panelManager->timelinePanel(), &EventTimeline::eventsCleared,
            this, [this]() { m_database.clearEvents(m_viewedSessionId); });
    connect(m_panelManager->projectsPanel(), &ProjectPanel::projectSelected,
            this, &MainWindow::onProjectSelected);

    // Approval signals
    connect(&m_approvalRouter, &ApprovalRouter::approvalRequested,
            this, &MainWindow::onApprovalRequested);
    connect(&m_approvalRouter, &ApprovalRouter::pendingCountChanged,
            this, &MainWindow::onPendingApprovalsChanged);

    setCentralWidget(central);
    setupMenuBar();
    setupStatusBar();
    setupTray();
    setupPalette();

    QTimer::singleShot(0, this, [this]() {
        if (isMinimized()) showNormal();
        show();
    });
}

static QRect clampRectToScreen(const QRect &rect, const QRect &available) {
    if (rect.intersects(available)) {
        return rect;
    }

    const QSize size = rect.size().boundedTo(available.size());
    const QPoint topLeft(
        available.left() + qMax(0, (available.width() - size.width()) / 2),
        available.top() + qMax(0, (available.height() - size.height()) / 2));
    return QRect(topLeft, size);
}

void MainWindow::setupMenuBar() {
    auto *menuBar = this->menuBar();

    // File menu
    auto *fileMenu = menuBar->addMenu("&File");
    auto *newSessionAction = new QAction(tr("&New Session"), fileMenu);
    newSessionAction->setShortcut(QKeySequence::New);
    connect(newSessionAction, &QAction::triggered, this, &MainWindow::onNewSession);
    fileMenu->addAction(newSessionAction);

    fileMenu->addSeparator();
    auto *quitAction = new QAction(tr("E&xit"), fileMenu);
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &MainWindow::onQuit);
    fileMenu->addAction(quitAction);

    // View menu — rail page (Sessions | Projects) + command palette
    auto *viewMenu = menuBar->addMenu("&View");
    auto *railPageGroup = new QActionGroup(viewMenu);
    auto *sessionsPageAction = new QAction(tr("Sessions"), viewMenu);
    sessionsPageAction->setCheckable(true);
    sessionsPageAction->setChecked(true);
    railPageGroup->addAction(sessionsPageAction);
    connect(sessionsPageAction, &QAction::triggered, this, [this]() {
        m_railStack->setCurrentIndex(0);
    });
    viewMenu->addAction(sessionsPageAction);

    auto *projectsPageAction = new QAction(tr("Projects"), viewMenu);
    projectsPageAction->setCheckable(true);
    railPageGroup->addAction(projectsPageAction);
    connect(projectsPageAction, &QAction::triggered, this, [this]() {
        m_railStack->setCurrentIndex(1);
    });
    viewMenu->addAction(projectsPageAction);

    viewMenu->addSeparator();
    auto *sidebarAction = new QAction(tr("Toggle Session Rail"), viewMenu);
    sidebarAction->setCheckable(true);
    sidebarAction->setChecked(true);
    connect(sidebarAction, &QAction::triggered, this, [this](bool checked) {
        m_sidebar->setVisible(checked);
    });
    viewMenu->addAction(sidebarAction);

    auto *paletteAction = new QAction(tr("Command Palette"), viewMenu);
    paletteAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_K));
    connect(paletteAction, &QAction::triggered, this, [this]() {
        if (m_palette) m_palette->open();
    });
    viewMenu->addAction(paletteAction);

    // Tools menu
    auto *toolsMenu = menuBar->addMenu("&Tools");
    toolsMenu->addSeparator();
    auto *scanProvidersAction = new QAction(tr("&Scan Providers…"), toolsMenu);
    connect(scanProvidersAction, &QAction::triggered, this, [this]() {
        scanAndSelectProvider();
    });
    toolsMenu->addAction(scanProvidersAction);

    auto *hermesOptionsAction = new QAction(tr("Hermes Session Options…"), toolsMenu);
    connect(hermesOptionsAction, &QAction::triggered, this, &MainWindow::onHermesOptions);
    toolsMenu->addAction(hermesOptionsAction);

    // Settings menu
    auto *settingsMenu = menuBar->addMenu("&Settings");
    auto *settingsAction = new QAction(tr("&Open Settings"), settingsMenu);
    settingsAction->setShortcut(QKeySequence::Preferences);
    connect(settingsAction, &QAction::triggered, this, &MainWindow::onSettings);
    settingsMenu->addAction(settingsAction);

    // Help menu
    auto *helpMenu = menuBar->addMenu("&Help");
    auto *aboutAction = new QAction(tr("&About"), helpMenu);
    connect(aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, tr("About Consiglio"),
            tr("<h2>Consiglio %1</h2>"
               "<p>A native C++ desktop control plane for AI agents.</p>"
               "<p>Qt5 / C++17</p>")
                .arg(QApplication::applicationVersion()));
    });
    helpMenu->addAction(aboutAction);

    auto *wizardAction = new QAction(tr("&Provider Setup"), helpMenu);
    connect(wizardAction, &QAction::triggered, this, [this]() {
        scanAndSelectProvider();
    });
    helpMenu->addAction(wizardAction);
}

void MainWindow::setupStatusBar() {
    auto *statusBar = this->statusBar();

    // Left side: approval badge + session state.
    m_approvalBadge = new QLabel(statusBar);
    m_approvalBadge->setVisible(false);
    statusBar->addWidget(m_approvalBadge);

    m_sessionStatusLabel = new QLabel("Ready", statusBar);
    statusBar->addPermanentWidget(m_sessionStatusLabel);

    // Right side: rich context (provider · model · workdir · permissions).
    m_providerLabel = new QLabel(statusBar);
    m_providerLabel->setObjectName("statusProvider");
    m_modelLabel = new QLabel(statusBar);
    m_modelLabel->setObjectName("statusModel");
    m_workdirLabel = new QLabel(statusBar);
    m_workdirLabel->setObjectName("statusWorkdir");
    m_permLabel = new QLabel(statusBar);
    m_permLabel->setObjectName("statusPerm");
    statusBar->addPermanentWidget(m_providerLabel);
    statusBar->addPermanentWidget(m_modelLabel);
    statusBar->addPermanentWidget(m_workdirLabel);
    statusBar->addPermanentWidget(m_permLabel);
}

void MainWindow::setupTray() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        m_tray = nullptr;
        m_trayMenu = nullptr;
        return;
    }

    m_tray = new QSystemTrayIcon(this);
    m_trayMenu = new QMenu();

    auto *showAction = new QAction(tr("Show"), m_trayMenu);
    connect(showAction, &QAction::triggered, this, [this]() {
        show();
        raise();
        activateWindow();
    });
    m_trayMenu->addAction(showAction);

    m_trayMenu->addSeparator();
    auto *quitAction = new QAction(tr("Quit"), m_trayMenu);
    connect(quitAction, &QAction::triggered, this, &MainWindow::onQuit);
    m_trayMenu->addAction(quitAction);

    m_tray->setContextMenu(m_trayMenu);
    const QIcon trayIcon = QIcon::fromTheme("application-default-icon", QIcon());
    if (!trayIcon.isNull()) {
        m_tray->setIcon(trayIcon);
        m_tray->show();
    }

    connect(m_tray, &QSystemTrayIcon::activated, this, &MainWindow::onTrayActivated);
}

void MainWindow::setupPalette() {
    m_palette = new CommandPalette(this);
    QList<PaletteAction> actions;
    actions.append({"new-session", tr("New Session"), "Ctrl+N"});
    actions.append({"stop-session", tr("Stop Active Session"), ""});
    actions.append({"hermes-options", tr("Hermes Session Options…"), ""});
    actions.append({"scan-providers", tr("Scan Providers…"), ""});
    actions.append({"settings", tr("Open Settings"), "Ctrl+,"});
    actions.append({"toggle-sidebar", tr("Toggle Session Rail"), ""});
    actions.append({"about", tr("About Consiglio"), ""});
    m_palette->setActions(actions);
    connect(m_palette, &CommandPalette::actionSelected,
            this, &MainWindow::onPaletteAction);
}

void MainWindow::loadSettings() {
    m_settings.load();
}

void MainWindow::showStartupWizardIfNeeded() {
    if (!m_settings.hasRunSetup() || qEnvironmentVariableIntValue("CONSIGLIO_RESCAN_PROVIDERS") == 1) {
        scanAndSelectProvider();
    }
}

bool MainWindow::scanAndSelectProvider() {
    m_sessionStatusLabel->setText(tr("Scanning AI providers…"));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_agentDetector.startDetection();
    const QList<AgentInfo> providers = m_agentDetector.detectAll();
    QApplication::restoreOverrideCursor();

    const QString initialWorkspace = m_database.preference(
        "lastWorkspace", QDir::homePath()).toString();
    ProviderSelectionDialog dialog(providers, m_settings.value().defaultProvider,
                                   initialWorkspace, this);
    if (dialog.exec() != QDialog::Accepted) {
        m_sessionStatusLabel->setText(tr("Ready"));
        return false;
    }

    const QString provider = dialog.selectedProvider();
    if (provider.isEmpty()) {
        m_sessionStatusLabel->setText(tr("No provider available"));
        return false;
    }

    m_settings.value().defaultProvider = provider;
    const QString endpoint = dialog.selectedEndpoint();
    const QString model = dialog.selectedModel();
    if (provider == "ollama" && !endpoint.isEmpty()) {
        m_settings.value().ollama.baseUrl = endpoint;
        if (!model.isEmpty()) m_settings.value().ollama.model = model;
    } else if (provider == "remote_llamacpp" && !endpoint.isEmpty()) {
        m_settings.value().remoteLlamaCpp.baseUrl = endpoint;
        if (!model.isEmpty()) m_settings.value().remoteLlamaCpp.model = model;
    }
    m_settings.value().providerConfigured = true;
    m_settings.save();
    m_sessionStatusLabel->setText(tr("Provider: %1").arg(provider));
    return startSession(provider, dialog.selectedWorkspace(),
                        dialog.selectedSandboxMode());
}

void MainWindow::refreshSessionList() {
    auto sessions = m_database.sessions(m_selectedProjectId);
    const auto liveSessions = m_sessionManager.listSessions();
    for (const auto &live : liveSessions) {
        bool replaced = false;
        for (auto &record : sessions) {
            if (record.id == live.id) {
                record = live;
                replaced = true;
                break;
            }
        }
        if (!replaced && (m_selectedProjectId.isEmpty() ||
                          live.projectId == m_selectedProjectId)) {
            sessions.prepend(live);
        }
    }
    m_panelManager->sessionsPanel()->setSessions(sessions);
    m_panelManager->sessionsPanel()->setProjectContext(m_selectedProjectName, m_selectedProjectWorkspace);
}

void MainWindow::refreshProjectList() {
    m_panelManager->projectsPanel()->setProjects(m_database.projects());
}

void MainWindow::loadTimelineEvents(const QString &sessionId) {
    m_viewedSessionId = sessionId;
    m_panelManager->timelinePanel()->setEvents(m_database.events(sessionId));
}

void MainWindow::addTimelineEvent(const EventModel::EventItem &event) {
    m_database.addEvent(event);
    if (m_viewedSessionId.isEmpty() || event.sessionId == m_viewedSessionId) {
        m_panelManager->timelinePanel()->addEvent(event);
    }
}

void MainWindow::updateStatusBarSessionState() {
    // Resolve the active session's provider/model/workdir/permission for the
    // rich status bar, then set the state label.
    QString provider, model, workdir, perm;
    if (!m_activeSessionId.isEmpty()) {
        auto sessions = m_sessionManager.listSessions();
        for (const auto &s : sessions) {
            if (s.id == m_activeSessionId) {
                provider = s.provider;
                workdir = s.repository;
                perm = s.permissionMode;
                // Model is tracked per-provider in settings.
                if (provider == "ollama") model = m_settings.value().ollama.model;
                else if (provider == "remote_llamacpp") model = m_settings.value().remoteLlamaCpp.model;
                break;
            }
        }
    }
    m_activeProvider = provider;
    m_activeModel = model;
    m_providerLabel->setText(provider.isEmpty() ? QString() : tr("provider: %1").arg(provider));
    m_modelLabel->setText(model.isEmpty() ? QString() : tr("model: %1").arg(model));
    m_workdirLabel->setText(workdir.isEmpty() ? QString() : tr("dir: %1").arg(QFileInfo(workdir).fileName()));
    m_permLabel->setText(perm.isEmpty() ? QString() : tr("perm: %1").arg(perm));

    if (m_activeSessionId.isEmpty()) {
        m_sessionStatusLabel->setText("Ready");
    } else {
        auto sessions = m_sessionManager.listSessions();
        for (const auto &s : sessions) {
            if (s.id == m_activeSessionId) {
                if (s.status == "running") {
                    m_sessionStatusLabel->setText(QString("Running: %1").arg(s.provider));
                } else if (s.status == "error") {
                    m_sessionStatusLabel->setText("Error");
                } else {
                    m_sessionStatusLabel->setText("Stopped");
                }
                return;
            }
        }
        m_sessionStatusLabel->setText("Ready");
    }
}

void MainWindow::onNewSession() {
    if (!m_settings.hasRunSetup()) {
        scanAndSelectProvider();
        return;
    }
    startConfiguredSession();
}

bool MainWindow::startConfiguredSession() {
    const QString provider = m_settings.value().defaultProvider;

    // Hermes sessions get the full options dialog (the CLI knobs). Other
    // providers use the focused New Session dialog.
    if (provider == "hermes") {
        const QString initialWorkspace = m_database.preference(
            "lastWorkspace", QDir::homePath()).toString();
        // Seed the dialog with previously saved options so re-opening preserves them.
        const QVariantMap saved = m_database.preference("hermes/sessionOptions").toMap();
        HermesOptionsDialog dialog(initialWorkspace, saved, this);
        if (dialog.exec() != QDialog::Accepted) return false;
        const QVariantMap opts = dialog.options();
        m_database.setPreference("hermes/sessionOptions", opts);
        return startSession(provider, dialog.workspace(), "provider-managed", opts);
    }

    const QString initialWorkspace = m_database.preference(
        "lastWorkspace", QDir::homePath()).toString();
    NewSessionDialog dialog(provider, initialWorkspace, this);
    if (dialog.exec() != QDialog::Accepted) return false;
    return startSession(provider, dialog.workspace(), dialog.sandboxMode());
}

bool MainWindow::startSession(const QString &provider, const QString &repoDir,
                              const QString &sandboxMode, const QVariantMap &extraOptions) {
    if (provider.isEmpty() || !QDir(repoDir).exists()) return false;

    // Build options map.
    QVariantMap options = extraOptions;
    options["sandboxMode"] = sandboxMode;
    options["userName"] = m_settings.value().userName;
    const QString projectId = m_database.ensureProject(repoDir);
    options["projectId"] = projectId;
    const QString historyMcpScript = QCoreApplication::applicationDirPath()
        + "/consiglio_history_mcp.py";
    if (QFileInfo::exists(historyMcpScript)) {
        options["historyMcpScript"] = historyMcpScript;
        options["historyDatabasePath"] = m_database.databasePath();
    }
    if (provider == "ollama") {
        options["model"] = m_settings.value().ollama.model;
        options["baseUrl"] = m_settings.value().ollama.baseUrl;
    } else if (provider == "llama-cpp" || provider == "remote_llamacpp") {
        options["baseUrl"] = m_settings.value().remoteLlamaCpp.baseUrl;
        options["model"] = m_settings.value().remoteLlamaCpp.model;
        options["apiKey"] = m_settings.value().remoteLlamaCpp.apiKey;
    }

    auto sessionId = m_sessionManager.startSession(provider, repoDir, "", options);
    if (sessionId.isEmpty()) return false;
    m_activeSessionId = sessionId;
    loadTimelineEvents(sessionId);

    m_database.setPreference("lastWorkspace", repoDir);
    m_selectedProjectId = projectId;

    refreshSessionList();
    refreshProjectList();
    updateStatusBarSessionState();

    EventModel::EventItem evt;
    evt.type = EventModel::SystemEvent;
    evt.content = QString("Session started with provider '%1' in %2 — permissions: %3")
                      .arg(provider, repoDir, sandboxMode);
    evt.sessionId = sessionId;
    evt.timestamp = QDateTime::currentMSecsSinceEpoch();
    addTimelineEvent(evt);
    return true;
}

void MainWindow::onSettings() {
    SettingsDialog dlg(&m_settings.value(), &m_agentDetector, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_settings.save();
    }
}

void MainWindow::onStartupWizard() {
    if (!m_wizard) {
        m_wizard = new StartupWizard(&m_agentDetector, &m_settings.value(), this);
    }
    m_wizard->show();
    m_wizard->raise();
    m_wizard->activateWindow();
}

void MainWindow::onQuit() {
    m_sessionManager.stopAllSessions();
    QApplication::quit();
}

void MainWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::Trigger) {
        show();
        raise();
        activateWindow();
    }
}

void MainWindow::onApprovalRequested(const ApprovalRequest &request) {
    emit approvalRequested(request);
    auto *dlg = new ConsiglioCmdApproval(request, this);
    connect(dlg, &ConsiglioCmdApproval::approved, this, [this, request]() {
        m_approvalRouter.resolve(request.id, true);
    });
    connect(dlg, &ConsiglioCmdApproval::rejected, this, [this, request]() {
        m_approvalRouter.resolve(request.id, false);
    });
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void MainWindow::onPendingApprovalsChanged(int count) {
    if (count > 0) {
        m_approvalBadge->setText(tr("⚠ %n approval(s)", "", count));
        m_approvalBadge->setVisible(true);
    } else {
        m_approvalBadge->setVisible(false);
    }
}

// ─── Wiring slots ──────────────────────────────────────────────────────

void MainWindow::onSessionStarted(const QString &sessionId, const SessionRecord &record) {
    Q_UNUSED(sessionId);
    m_database.upsertSession(record);
    refreshSessionList();
    refreshProjectList();
    updateStatusBarSessionState();

    EventModel::EventItem evt;
    evt.type = EventModel::SystemEvent;
    evt.content = QString("Session started (%1) — provider: %2")
                      .arg(sessionId.left(8), record.provider);
    evt.sessionId = sessionId;
    evt.timestamp = QDateTime::currentMSecsSinceEpoch();
    addTimelineEvent(evt);
}

void MainWindow::onSessionStopped(const QString &sessionId) {
    if (m_activeSessionId == sessionId) {
        m_panelManager->timelinePanel()->setThinking(false);
        m_activeSessionId.clear();
    }
    m_database.setSessionStatus(sessionId, "stopped", QDateTime::currentMSecsSinceEpoch());
    refreshSessionList();
    refreshProjectList();
    updateStatusBarSessionState();

    EventModel::EventItem evt;
    evt.type = EventModel::SystemEvent;
    evt.content = QString("Session stopped (%1)").arg(sessionId.left(8));
    evt.sessionId = sessionId;
    evt.timestamp = QDateTime::currentMSecsSinceEpoch();
    addTimelineEvent(evt);
}

void MainWindow::onSessionError(const QString &sessionId, const QString &error) {
    if (m_activeSessionId == sessionId) {
        m_panelManager->timelinePanel()->setThinking(false);
        m_activeSessionId.clear();
    }
    m_database.setSessionStatus(sessionId, "error", QDateTime::currentMSecsSinceEpoch());
    refreshSessionList();
    refreshProjectList();
    updateStatusBarSessionState();

    EventModel::EventItem evt;
    evt.type = EventModel::Error;
    evt.content = QString("Session error (%1): %2").arg(sessionId.left(8), error);
    evt.sessionId = sessionId;
    evt.timestamp = QDateTime::currentMSecsSinceEpoch();
    addTimelineEvent(evt);
}

void MainWindow::onCodexThreadIdReceived(const QString &sessionId, const QString &threadId) {
    m_database.setSessionCodexThreadId(sessionId, threadId);
}

void MainWindow::onOutputReceived(const QString &sessionId, const QString &data) {
    if (data.isEmpty()) return;
    if (sessionId == m_activeSessionId) {
        m_panelManager->timelinePanel()->setThinking(false);
    }

    EventModel::EventItem evt;
    evt.type = EventModel::CommandOutput;
    evt.content = data.trimmed();
    evt.sessionId = sessionId;
    evt.timestamp = QDateTime::currentMSecsSinceEpoch();
    addTimelineEvent(evt);
}

void MainWindow::onAssistantMessageReceived(const QString &sessionId, const QString &message) {
    if (message.isEmpty()) return;
    if (sessionId == m_activeSessionId) {
        m_panelManager->timelinePanel()->setThinking(false);
    }
    EventModel::EventItem evt;
    evt.type = EventModel::AssistantMessage;
    evt.content = message;
    evt.sessionId = sessionId;
    evt.timestamp = QDateTime::currentMSecsSinceEpoch();
    addTimelineEvent(evt);
}

void MainWindow::onStructuredErrorReceived(const QString &sessionId, const QString &error) {
    if (sessionId == m_activeSessionId) {
        m_panelManager->timelinePanel()->setThinking(false);
    }
    EventModel::EventItem evt;
    evt.type = EventModel::Error;
    evt.content = error;
    evt.sessionId = sessionId;
    evt.timestamp = QDateTime::currentMSecsSinceEpoch();
    addTimelineEvent(evt);
}

void MainWindow::onSessionSelected(const QString &sessionId) {
    m_panelManager->timelinePanel()->setThinking(false);

    // Try to reconnect if the session isn't currently active.
    if (!m_sessionManager.hasSession(sessionId)) {
        const auto allSessions = m_database.sessions();
        for (const auto &record : allSessions) {
            if (record.id == sessionId && m_sessionManager.reconnectSession(record)) {
                break;
            }
        }
    }

    m_activeSessionId = m_sessionManager.hasSession(sessionId) ? sessionId : QString();
    loadTimelineEvents(sessionId);
    updateStatusBarSessionState();
    if (m_activeSessionId.isEmpty()) m_sessionStatusLabel->setText(tr("Recorded session"));

    EventModel::EventItem evt;
    evt.type = EventModel::SystemEvent;
    evt.content = m_activeSessionId.isEmpty()
        ? QString("Viewing recorded session %1").arg(sessionId.left(8))
        : QString("Switched to session %1").arg(sessionId.left(8));
    evt.sessionId = sessionId;
    evt.timestamp = QDateTime::currentMSecsSinceEpoch();
    addTimelineEvent(evt);
}

void MainWindow::onSessionStoppedFromList(const QString &sessionId) {
    m_sessionManager.stopSession(sessionId);
    if (m_activeSessionId == sessionId) {
        m_activeSessionId.clear();
    }
    updateStatusBarSessionState();
}

void MainWindow::onCommandExecuted(const QString &command, const QString &workingDir) {
    // Show the command as a user message in the timeline.
    EventModel::EventItem evt;
    evt.type = EventModel::UserMessage;
    evt.content = command;
    evt.sessionId = m_activeSessionId;
    evt.timestamp = QDateTime::currentMSecsSinceEpoch();
    addTimelineEvent(evt);

    if (!m_activeSessionId.isEmpty() && m_sessionManager.hasSession(m_activeSessionId)) {
        sendCommandToActiveSession(command, workingDir);
    } else {
        EventModel::EventItem hint;
        hint.type = EventModel::SystemEvent;
        hint.content = "No active session. Start a session first (File → New Session or Ctrl+N).";
        hint.timestamp = QDateTime::currentMSecsSinceEpoch();
        addTimelineEvent(hint);
    }
}

void MainWindow::sendCommandToActiveSession(const QString &command, const QString &workingDir) {
    Q_UNUSED(workingDir);
    const bool sent = m_sessionManager.sendCommand(m_activeSessionId, command);
    m_panelManager->timelinePanel()->setThinking(sent);
    if (!sent) {
        EventModel::EventItem evt;
        evt.type = EventModel::Error;
        evt.content = tr("The active session could not accept that message.");
        evt.sessionId = m_activeSessionId;
        evt.timestamp = QDateTime::currentMSecsSinceEpoch();
        addTimelineEvent(evt);
    }
}

void MainWindow::onProjectSelected(const QString &projectId, const QString &) {
    m_selectedProjectId = projectId;
    const auto projects = m_database.projects();
    m_selectedProjectName.clear();
    m_selectedProjectWorkspace.clear();
    for (const auto &project : projects) {
        if (project.id == projectId) {
            m_selectedProjectName = project.name;
            m_selectedProjectWorkspace = project.workspace;
            break;
        }
    }
    refreshSessionList();
    const auto projectSessions = m_database.sessions(projectId);
    if (projectSessions.isEmpty()) {
        m_activeSessionId.clear();
        m_panelManager->timelinePanel()->setThinking(false);
            return;
    }

    onSessionSelected(projectSessions.first().id);
}

void MainWindow::onClearProjectFilterRequested() {
    m_selectedProjectId.clear();
    m_selectedProjectName.clear();
    m_selectedProjectWorkspace.clear();
    refreshSessionList();
    refreshProjectList();
    m_panelManager->timelinePanel()->setThinking(false);
}

// ─── Palette + Hermes options ──────────────────────────────────────────

void MainWindow::onPaletteAction(const QString &id) {
    if (id == "new-session") onNewSession();
    else if (id == "stop-session") {
        if (!m_activeSessionId.isEmpty()) {
            m_sessionManager.stopSession(m_activeSessionId);
            m_activeSessionId.clear();
            updateStatusBarSessionState();
        }
    } else if (id == "hermes-options") onHermesOptions();
    else if (id == "scan-providers") scanAndSelectProvider();
    else if (id == "settings") onSettings();
    else if (id == "toggle-sidebar") toggleSidebar();
    else if (id == "about") {
        QMessageBox::about(this, tr("About Consiglio"),
            tr("<h2>Consiglio %1</h2><p>A native C++ desktop control plane for AI agents.</p>")
                .arg(QApplication::applicationVersion()));
    }
}

void MainWindow::onHermesOptions() {
    const QString initialWorkspace = m_database.preference(
        "lastWorkspace", QDir::homePath()).toString();
    // If there's an active hermes session, re-apply options to it on next turn.
    // For now, open the dialog; starting a new hermes session uses these values.
    HermesOptionsDialog dialog(initialWorkspace, {}, this);
    if (dialog.exec() == QDialog::Accepted) {
        // Persist the chosen options so the next hermes session reuses them.
        QVariantMap opts = dialog.options();
        m_database.setPreference("hermes/sessionOptions", opts);
        m_database.setPreference("lastWorkspace", dialog.workspace());
    }
}

void MainWindow::toggleSidebar() {
    m_sidebar->setVisible(!m_sidebar->isVisible());
}
