#pragma once

#include <QWidget>
#include <QObject>

class SessionList;
class EventTimeline;
class ProjectPanel;

class PanelManager : public QObject {
    Q_OBJECT

public:
    explicit PanelManager(QObject *parent = nullptr);
    ~PanelManager();

    void createPanels(QWidget *mainWindow);

    SessionList *sessionsPanel() const { return m_sessionsPanel; }
    EventTimeline *timelinePanel() const { return m_timelinePanel; }
    ProjectPanel *projectsPanel() const { return m_projectsPanel; }

private:
    QWidget *createSessionsPanel();
    QWidget *createTimelinePanel();
    QWidget *createProjectsPanel();

    SessionList *m_sessionsPanel = nullptr;
    EventTimeline *m_timelinePanel = nullptr;
    ProjectPanel *m_projectsPanel = nullptr;
};
