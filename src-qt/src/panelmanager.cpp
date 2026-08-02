#include "panelmanager.h"
#include "sessionlist.h"
#include "eventtimeline.h"
#include "filebrowser.h"
#include "projectpanel.h"
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
    // Sessions panel
    m_sessionsPanel = new SessionList(mainWindow);
    m_stack->addWidget(m_sessionsPanel);

    // Timeline panel
    m_timelinePanel = new EventTimeline(mainWindow);
    m_stack->addWidget(m_timelinePanel);

    // Files panel
    m_filesPanel = new FileBrowser(mainWindow);
    m_stack->addWidget(m_filesPanel);

    // Durable project workspace/session hierarchy
    m_projectsPanel = new ProjectPanel(mainWindow);
    m_stack->addWidget(m_projectsPanel);

    // Secrets panel
    m_stack->addWidget(createSecretsPanel());

    // Mobile panel
    m_stack->addWidget(createMobilePanel());
}

QWidget *PanelManager::createSecretsPanel() {
    auto *widget = new SecretsManager;
    return widget;
}

QWidget *PanelManager::createMobilePanel() {
    auto *widget = new MobilePairing;
    return widget;
}
