#include "panelmanager.h"
#include "sessionlist.h"
#include "eventtimeline.h"
#include "filebrowser.h"
#include "discussionpanel.h"
#include "secretsmanager.h"
#include "mobilepairing.h"
#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>

PanelManager::PanelManager(QStackedWidget *stack, QObject *parent)
    : QObject(parent), m_stack(stack)
{
}

PanelManager::~PanelManager() = default;

void PanelManager::createPanels(QWidget *mainWindow) {
    // Welcome panel
    m_stack->addWidget(createWelcomePanel());

    // Sessions panel
    m_sessionsPanel = new SessionList(mainWindow);
    m_stack->addWidget(m_sessionsPanel);

    // Timeline panel
    m_timelinePanel = new EventTimeline(mainWindow);
    m_stack->addWidget(m_timelinePanel);

    // Files panel
    m_filesPanel = new FileBrowser(mainWindow);
    m_stack->addWidget(m_filesPanel);

    // Discussions panel
    m_discussionsPanel = new DiscussionPanel(mainWindow);
    m_stack->addWidget(m_discussionsPanel);

    // Secrets panel
    m_stack->addWidget(createSecretsPanel());

    // Mobile panel
    m_stack->addWidget(createMobilePanel());
}

QWidget *PanelManager::createWelcomePanel() {
    auto *widget = new QWidget;
    auto *layout = new QVBoxLayout(widget);
    layout->setAlignment(Qt::AlignCenter);

    auto *title = new QLabel("Welcome to Consiglio", widget);
    title->setStyleSheet("font-size: 28px; font-weight: bold; color: #f0f6fc;");
    layout->addWidget(title, 0, Qt::AlignCenter);

    auto *subtitle = new QLabel("A native C++ desktop control plane for AI agents.\nSelect a panel from the sidebar to get started.", widget);
    subtitle->setStyleSheet("font-size: 14px; color: #8b949e; text-align: center;");
    subtitle->setWordWrap(true);
    layout->addWidget(subtitle, 0, Qt::AlignCenter);

    return widget;
}

QWidget *PanelManager::createSecretsPanel() {
    auto *widget = new SecretsManager;
    return widget;
}

QWidget *PanelManager::createMobilePanel() {
    auto *widget = new MobilePairing;
    return widget;
}
