#include "panelmanager.h"
#include "sessionlist.h"
#include "eventtimeline.h"
#include "projectpanel.h"

PanelManager::PanelManager(QObject *parent)
    : QObject(parent)
{
}

PanelManager::~PanelManager() = default;

void PanelManager::createPanels(QWidget *mainWindow) {
    m_sessionsPanel = new SessionList(mainWindow);
    m_timelinePanel = new EventTimeline(mainWindow);
    m_projectsPanel = new ProjectPanel(mainWindow);
}
