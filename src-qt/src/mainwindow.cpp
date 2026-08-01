#include "mainwindow.h"
#include "sidebar.h"
#include "panelmanager.h"
#include "sessionlist.h"
#include "eventtimeline.h"
#include "startupwizard.h"
#include "settingsdialog.h"
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
#include <QDateTime>
#include <QSettings>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("Consiglio");
    setMinimumSize(1280, 800);
    resize(1440, 900);

    setupUI();
    loadSettings();
    showStartupWizardIfNeeded();
}

MainWindow::~MainWindow() {
    // Save window state
    ::QSettings s;
    s.setValue("windowGeometry", saveGeometry());
    s.setValue("windowState", saveState());
    s.setValue("lastPanelIndex", m_lastPanelIndex);

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

    // Panel signals → SessionManager actions
    connect(m_panelManager->sessionsPanel(), &SessionList::sessionSelected,
            this, &MainWindow::onSessionSelected);
    connect(m_panelManager->sessionsPanel(), &SessionList::sessionStopped,
            this, &MainWindow::onSessionStoppedFromList);
    connect(m_panelManager->timelinePanel(), &EventTimeline::commandExecuted,
            this, &MainWindow::onCommandExecuted);

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
    ::QSettings s;
    auto geom = s.value("windowGeometry");
    if (!geom.toByteArray().isEmpty()) {
        restoreGeometry(geom.toByteArray());
    }
    auto state = s.value("windowState");
    if (!state.toByteArray().isEmpty()) {
        restoreState(state.toByteArray());
    }
    int savedPanel = s.value("lastPanelIndex", 0).toInt();
    if (savedPanel >= 0 && savedPanel < m_content->count()) {
        m_content->setCurrentIndex(savedPanel);
    }

    // Show welcome panel by default
    m_sidebar->selectPanel(Sidebar::PanelId::Welcome);
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

    auto *discussionsAction = new QAction(tr("&Discussions"), viewMenu);
    discussionsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_4));
    connect(discussionsAction, &QAction::triggered, this, [this]() {
        m_sidebar->selectPanel(Sidebar::PanelId::Discussions);
    });
    viewMenu->addAction(discussionsAction);

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

    // Settings menu
    auto *settingsAction = new QAction(tr("&Settings"), menuBar->addMenu("&Settings"));
    settingsAction->setShortcut(QKeySequence::Preferences);
    connect(settingsAction, &QAction::triggered, this, &MainWindow::onSettings);
    menuBar->addAction(settingsAction);

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

    auto *wizardAction = new QAction(tr("&Startup Wizard"), helpMenu);
    connect(wizardAction, &QAction::triggered, this, &MainWindow::onStartupWizard);
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
    m_tray->setIcon(QIcon::fromTheme("application-default-icon", QIcon()));
    m_tray->show();

    connect(m_tray, &QSystemTrayIcon::activated, this, &MainWindow::onTrayActivated);
}

void MainWindow::loadSettings() {
    m_settings.load();
}

void MainWindow::showStartupWizardIfNeeded() {
    if (!m_settings.hasRunSetup()) {
        onStartupWizard();
    }
}

void MainWindow::refreshSessionList() {
    auto sessions = m_sessionManager.listSessions();
    m_panelManager->sessionsPanel()->setSessions(sessions);
}

void MainWindow::addTimelineEvent(const EventModel::EventItem &event) {
    m_panelManager->timelinePanel()->addEvent(event);
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
    // Show file dialog to select repository/workspace directory
    auto repoDir = QFileDialog::getExistingDirectory(this, tr("Select Workspace Directory"),
                                                      QDir::homePath(),
                                                      QFileDialog::ShowDirsOnly);
    if (repoDir.isEmpty()) {
        return;
    }

    // Read provider from settings
    QString provider = m_settings.value().defaultProvider;
    if (provider == "default" || provider.isEmpty()) {
        provider = "ollama";  // fallback
    }

    // Build options map
    QVariantMap options;
    if (provider == "ollama") {
        options["model"] = m_settings.value().ollama.model;
    } else if (provider == "llama-cpp") {
        options["host"] = m_settings.value().remoteLlamaCpp.baseUrl.isEmpty()
                              ? "127.0.0.1"
                              : m_settings.value().remoteLlamaCpp.baseUrl;
        options["model"] = m_settings.value().remoteLlamaCpp.model;
    }

    // Start the session
    auto sessionId = m_sessionManager.startSession(provider, repoDir, "", options);
    m_activeSessionId = sessionId;

    // Switch to timeline panel
    m_sidebar->selectPanel(Sidebar::PanelId::Timeline);

    // Update UI
    refreshSessionList();
    updateStatusBarSessionState();

    // Add system event to timeline
    EventModel::EventItem evt;
    evt.type = EventModel::SystemEvent;
    evt.content = QString("Session started with provider '%1' in %2")
                      .arg(provider, repoDir);
    evt.timestamp = QDateTime::currentMSecsSinceEpoch();
    addTimelineEvent(evt);
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
    refreshSessionList();
    updateStatusBarSessionState();

    // If timeline is visible, add a system event
    if (qobject_cast<EventTimeline *>(m_content->currentWidget()) == m_panelManager->timelinePanel()) {
        EventModel::EventItem evt;
        evt.type = EventModel::SystemEvent;
        evt.content = QString("Session started (%1) — provider: %2")
                          .arg(sessionId.left(8), record.provider);
        evt.timestamp = QDateTime::currentMSecsSinceEpoch();
        addTimelineEvent(evt);
    }
}

void MainWindow::onSessionStopped(const QString &sessionId) {
    if (m_activeSessionId == sessionId) {
        m_activeSessionId.clear();
    }
    refreshSessionList();
    updateStatusBarSessionState();

    // Add system event to timeline if visible
    if (qobject_cast<EventTimeline *>(m_content->currentWidget()) == m_panelManager->timelinePanel()) {
        EventModel::EventItem evt;
        evt.type = EventModel::SystemEvent;
        evt.content = QString("Session stopped (%1)").arg(sessionId.left(8));
        evt.timestamp = QDateTime::currentMSecsSinceEpoch();
        addTimelineEvent(evt);
    }
}

void MainWindow::onSessionError(const QString &sessionId, const QString &error) {
    if (m_activeSessionId == sessionId) {
        m_activeSessionId.clear();
    }
    refreshSessionList();
    updateStatusBarSessionState();

    // Add error event to timeline
    EventModel::EventItem evt;
    evt.type = EventModel::Error;
    evt.content = QString("Session error (%1): %2").arg(sessionId.left(8), error);
    evt.timestamp = QDateTime::currentMSecsSinceEpoch();
    addTimelineEvent(evt);
}

void MainWindow::onOutputReceived(const QString &sessionId, const QString &data) {
    if (data.isEmpty()) return;

    // Add as command output event to timeline
    EventModel::EventItem evt;
    evt.type = EventModel::CommandOutput;
    evt.content = data.trimmed();
    evt.sessionId = sessionId;
    evt.timestamp = QDateTime::currentMSecsSinceEpoch();
    addTimelineEvent(evt);
}

void MainWindow::onSessionSelected(const QString &sessionId) {
    m_activeSessionId = sessionId;
    updateStatusBarSessionState();

    // Switch to timeline to show the session activity
    m_sidebar->selectPanel(Sidebar::PanelId::Timeline);

    EventModel::EventItem evt;
    evt.type = EventModel::SystemEvent;
    evt.content = QString("Switched to session %1").arg(sessionId.left(8));
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
    m_sessionManager.sendCommand(m_activeSessionId, command);
}
