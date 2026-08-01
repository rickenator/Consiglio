#include "mainwindow.h"
#include "sidebar.h"
#include "panelmanager.h"
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

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("Consiglio");
    setMinimumSize(900, 600);
    resize(1200, 750);

    setupUI();
    loadSettings();
    showStartupWizardIfNeeded();
}

MainWindow::~MainWindow() = default;

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
    connect(m_sidebar, &Sidebar::panelChanged, m_content, [this](Sidebar::PanelId id) { m_content->setCurrentIndex(static_cast<int>(id)); });
    connect(m_sidebar, &Sidebar::newSessionRequested, this, &MainWindow::onNewSession);
    connect(m_sidebar, &Sidebar::settingsRequested, this, &MainWindow::onSettings);

    // Approval signals
    connect(&m_approvalRouter, &ApprovalRouter::approvalRequested,
            this, &MainWindow::onApprovalRequested);
    connect(&m_approvalRouter, &ApprovalRouter::pendingCountChanged,
            this, &MainWindow::onPendingApprovalsChanged);

    setCentralWidget(central);
    setupMenuBar();
    setupStatusBar();
    setupTray();

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
               "<p>Qt6 / C++20</p>")
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

    statusBar->addPermanentWidget(new QLabel("Ready", statusBar));
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

void MainWindow::onNewSession() {
    // TODO: Show session creation dialog
    QMessageBox::information(this, tr("New Session"),
        tr("Session creation will be implemented with provider selection."));
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
