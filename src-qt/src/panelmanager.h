#pragma once

#include <QObject>
#include <QStackedWidget>
#include "sidebar.h"

class QWidget;
class SessionList;
class EventTimeline;
class FileBrowser;
class ProjectPanel;
class SecretsManager;
class MobilePairing;

class PanelManager : public QObject {
    Q_OBJECT

public:
    explicit PanelManager(QStackedWidget *stack, QObject *parent = nullptr);
    ~PanelManager();

    void createPanels(QWidget *mainWindow);

    SessionList *sessionsPanel() const { return m_sessionsPanel; }
    EventTimeline *timelinePanel() const { return m_timelinePanel; }
    FileBrowser *filesPanel() const { return m_filesPanel; }
    ProjectPanel *projectsPanel() const { return m_projectsPanel; }

private:
    QStackedWidget *m_stack = nullptr;
    QWidget *createSessionsPanel();
    QWidget *createTimelinePanel();
    QWidget *createFilesPanel();
    QWidget *createSecretsPanel();
    QWidget *createMobilePanel();

    SessionList *m_sessionsPanel = nullptr;
    EventTimeline *m_timelinePanel = nullptr;
    FileBrowser *m_filesPanel = nullptr;
    ProjectPanel *m_projectsPanel = nullptr;
};
