#include "panelmanager.h"
#include "sessionlist.h"
#include "eventtimeline.h"
#include "projectpanel.h"
#include <QWidget>
#include <QVBoxLayout>

PanelManager::PanelManager(QStackedWidget *stack, QObject *parent)
    : QObject(parent), m_stack(stack)
{
}

PanelManager::~PanelManager() = default;

void PanelManager::createPanels(QWidget *mainWindow) {
    // Sessions panel (index 0) - shows recent conversations
    m_sessionsPanel = new SessionList(mainWindow);
    m_stack->addWidget(m_sessionsPanel);

    // Timeline panel (index 1) - shows session activity
    m_timelinePanel = new EventTimeline(mainWindow);
    m_stack->addWidget(m_timelinePanel);

    // Projects panel (index 2) - workspace hierarchy
    m_projectsPanel = new ProjectPanel(mainWindow);
    m_stack->addWidget(m_projectsPanel);
}
