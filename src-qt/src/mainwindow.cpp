#include "mainwindow.h"
#include "sidebar.h"
#include "panelmanager.h"
#include "sessionlist.h"
#include "eventtimeline.h"
#include "startupwizard.h"
#include "settingsdialog.h"
#include "providerselectiondialog.h"
#include "newsessiondialog.h"
#include "projectpanel.h"
#include "approvaldialog.h"
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QVBoxLayout>
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
    const QScreen *screen = QGuiApplication::primaryScreen();
    const QSize available = screen ? screen->availableGeometry().size() : QSize(1440, 900);
    setMinimumSize(qMin(UiMetrics::px(640), available.width()),
                   qMin(UiMetrics::px(400), available.height()));
    resize(qRound(available.width() * 0.84), qRound(available.height() * 0.84));

    setupUI();
    loadSettings();
    m_database.markRunningSessionsInterrupted(QDateTime::currentMSecsSinceEpoch());
    refreshProjectList();
    refreshSessionList();
    loadTimelineEvents();
    QTimer::singleShot(0, this, [this]() { showStartupWizardIfNeeded(); });
}

MainWindow::~MainWindow() {
    // Save window state
    m_database.setPreference("windowGeometry", saveGeometry());
    m_database.setPreference("windowState", saveState());
    m_database.setPreference("lastPanelIndex", m_lastPanelIndex);

    m_sessionManager.stopAllSessions();
}

Settings &MainWindow::settings() { return m_settings; }
AgentDetector &MainWindow::agentDetector() { return m_agentDetector; }
SessionManager &MainWindow::sessionManager() { return m_sessionManager; }
ApprovalRouter &MainWindow::approvalRouter() { return m_approvalRouter; }

void MainWindow::setupUI() {
    // Central widget with sidebar + content
    auto *central = new QWidget(this);
    auto *layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_sidebar = new Sidebar(this);
    layout->addWidget(m_sidebar, 0);

    m_content = new QStackedWidget(this);
    layout->addWidget(m_content, 1);

    // Panel manager creates all panels
    m_panelManager = new PanelManager(m_content, this);
    m_panelManager->createPanels(this);

    // Sidebar navigation
    connect(m_sidebar, &Sidebar::panelChanged, m_content, [this](Sidebar::PanelId id) {
        m_content->setCurrentIndex(static_cast<int>(id));
        m_lastPanelIndex = static_cast<int>(id);
    });
    connect(m_sidebar, &Sidebar::newSessionRequested, this, &MainWindow::onNewSession);
    connect(m_sidebar, &Sidebar::settingsRequested, this, &MainWindow::onSettings);

    // ─── Backend → Frontend wiring ──────────────────────────────────────
    // SessionManager signals → panel updates
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

    // Panel signals → SessionManager actions
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

    // Restore window state if available
    auto geom = m_database.preference("windowGeometry");
    if (!geom.toByteArray().isEmpty()) {
        restoreGeometry(geom.toByteArray());
        const QScreen *screen = QGuiApplication::screenAt(frameGeometry().center());
        const QRect available = screen ? screen->availableGeometry()
                                       : (QGuiApplication::primaryScreen()
                                              ? QGuiApplication::primaryScreen()->availableGeometry()
                                              : QRect());
        if (available.isValid()) {
            const QRect clamped = clampRectToScreen(frameGeometry(), available);
            if (clamped != frameGeometry()) {
                setGeometry(clamped);
            }
        }
    }
    auto state = m_database.preference("windowState");
    if (!state.toByteArray().isEmpty()) {
        restoreState(state.toByteArray());
    }
    int savedPanel = m_database.preference(
        "lastPanelIndex", static_cast<int>(Sidebar::PanelId::Sessions)).toInt();
    if (savedPanel >= 0 && savedPanel < m_content->count()) {
        m_content->setCurrentIndex(savedPanel);
    }

    QTimer::singleShot(0, this, [this]() {
        if (isMinimized()) showNormal();
        show();
        raise();
        activateWindow();
        if (windowHandle()) {
            windowHandle()->raise();
            windowHandle()->requestActivate();
        }
    });

    // Open directly into the working session view.
    m_sidebar->selectPanel(Sidebar::PanelId::Sessions);
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

    // View menu
    auto *viewMenu = menuBar->addMenu("&View");
    auto *sessionsAction = new QAction(tr("&Sessions"), viewMenu);
    sessionsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
    connect(sessionsAction, &QAction::triggered, this, [this]() {
        m_sidebar->selectPanel(Sidebar::PanelId::Sessions);
    });
    viewMenu->addAction(sessionsAction);

    auto *timelineAction = new QAction(tr("&Timeline"), viewMenu);
    timelineAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_2));
    connect(timelineAction, &QAction::triggered, this, [this]() {
        m_sidebar->selectPanel(Sidebar::PanelId::Timeline);
    });
    viewMenu->addAction(timelineAction);

    auto *filesAction = new QAction(tr("&Files"), viewMenu);
    filesAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_3));
    connect(filesAction, &QAction::triggered, this, [this]() {
        m_sidebar->selectPanel(Sidebar::PanelId::Files);
    });
    viewMenu->addAction(filesAction);

    auto *projectsAction = new QAction(tr("&Projects"), viewMenu);
    projectsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_4));
    connect(projectsAction, &QAction::triggered, this, [this]() {
        m_sidebar->selectPanel(Sidebar::PanelId::Projects);
    });
    viewMenu->addAction(projectsAction);

    // Tools menu
    auto *toolsMenu = menuBar->addMenu("&Tools");
    auto *secretsAction = new QAction(tr("&Secrets Manager"), toolsMenu);
    connect(secretsAction, &QAction::triggered, this, [this]() {
        m_sidebar->selectPanel(Sidebar::PanelId::Secrets);
    });
    toolsMenu->addAction(secretsAction);

    auto *mobileAction = new QAction(tr("&Mobile Pairing"), toolsMenu);
    connect(mobileAction, &QAction::triggered, this, [this]() {
        m_sidebar->selectPanel(Sidebar::PanelId::Mobile);
    });
    toolsMenu->addAction(mobileAction);

    toolsMenu->addSeparator();
    auto *scanProvidersAction = new QAction(tr("&Scan Providers…"), toolsMenu);
    connect(scanProvidersAction, &QAction::triggered, this, [this]() {
        scanAndSelectProvider();
    });
    toolsMenu->addAction(scanProvidersAction);

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

    m_approvalBadge = new QLabel(statusBar);
    m_approvalBadge->setVisible(false);
    statusBar->addWidget(m_approvalBadge);

    m_sessionStatusLabel = new QLabel("Ready", statusBar);
    statusBar->addPermanentWidget(m_sessionStatusLabel);
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

    const QString initialWorkspace = m_database.preference(
        "lastWorkspace", QDir::homePath()).toString();
    NewSessionDialog dialog(provider, initialWorkspace, this);
    if (dialog.exec() != QDialog::Accepted) return false;
    return startSession(provider, dialog.workspace(), dialog.sandboxMode());
}

bool MainWindow::startSession(const QString &provider, const QString &repoDir,
                              const QString &sandboxMode) {
    if (provider.isEmpty() || !QDir(repoDir).exists()) return false;

    // Build options map
    QVariantMap options;
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

    // Start the session
    auto sessionId = m_sessionManager.startSession(provider, repoDir, "", options);
    if (sessionId.isEmpty()) return false;
    m_activeSessionId = sessionId;
    loadTimelineEvents(sessionId);

    m_database.setPreference("lastWorkspace", repoDir);
    m_selectedProjectId = projectId;

    // Switch to timeline panel
    m_sidebar->selectPanel(Sidebar::PanelId::Timeline);

    // Update UI
    refreshSessionList();
    refreshProjectList();
    updateStatusBarSessionState();

    // Add system event to timeline
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

    // If timeline is visible, add a system event
    if (qobject_cast<EventTimeline *>(m_content->currentWidget()) == m_panelManager->timelinePanel()) {
        EventModel::EventItem evt;
        evt.type = EventModel::SystemEvent;
        evt.content = QString("Session started (%1) — provider: %2")
                          .arg(sessionId.left(8), record.provider);
        evt.sessionId = sessionId;
        evt.timestamp = QDateTime::currentMSecsSinceEpoch();
        addTimelineEvent(evt);
    }
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

    // Add system event to timeline if visible
    if (qobject_cast<EventTimeline *>(m_content->currentWidget()) == m_panelManager->timelinePanel()) {
        EventModel::EventItem evt;
        evt.type = EventModel::SystemEvent;
        evt.content = QString("Session stopped (%1)").arg(sessionId.left(8));
        evt.sessionId = sessionId;
        evt.timestamp = QDateTime::currentMSecsSinceEpoch();
        addTimelineEvent(evt);
    }
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

    // Add error event to timeline
    EventModel::EventItem evt;
    evt.type = EventModel::Error;
    evt.content = QString("Session error (%1): %2").arg(sessionId.left(8), error);
    evt.sessionId = sessionId;
    evt.timestamp = QDateTime::currentMSecsSinceEpoch();
    addTimelineEvent(evt);
}

void MainWindow::onOutputReceived(const QString &sessionId, const QString &data) {
    if (data.isEmpty()) return;
    if (sessionId == m_activeSessionId) {
        m_panelManager->timelinePanel()->setThinking(false);
    }

    // Add as command output event to timeline
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
    m_activeSessionId = m_sessionManager.hasSession(sessionId) ? sessionId : QString();
    loadTimelineEvents(sessionId);
    updateStatusBarSessionState();
    if (m_activeSessionId.isEmpty()) m_sessionStatusLabel->setText(tr("Recorded session"));

    // Switch to timeline to show the session activity
    m_sidebar->selectPanel(Sidebar::PanelId::Timeline);

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
    // Show the command as a user message in the timeline
    EventModel::EventItem evt;
    evt.type = EventModel::UserMessage;
    evt.content = command;
    evt.sessionId = m_activeSessionId;
    evt.timestamp = QDateTime::currentMSecsSinceEpoch();
    addTimelineEvent(evt);

    // If we have an active session, send the command to it
    if (!m_activeSessionId.isEmpty() && m_sessionManager.hasSession(m_activeSessionId)) {
        sendCommandToActiveSession(command, workingDir);
    } else {
        // No active session — show a hint
        EventModel::EventItem hint;
        hint.type = EventModel::SystemEvent;
        hint.content = "No active session. Start a session first (File → New Session).";
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
        m_sidebar->selectPanel(Sidebar::PanelId::Sessions);
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
    m_sidebar->selectPanel(Sidebar::PanelId::Sessions);
}
