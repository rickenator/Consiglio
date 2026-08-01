#pragma once

#include <QObject>
#include <QStackedWidget>
#include "sidebar.h"

class QWidget;
class SessionList;
class EventTimeline;
class FileBrowser;
class DiscussionPanel;
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
    DiscussionPanel *discussionsPanel() const { return m_discussionsPanel; }

private:
    QStackedWidget *m_stack = nullptr;
    QWidget *createWelcomePanel();
    QWidget *createSessionsPanel();
    QWidget *createTimelinePanel();
    QWidget *createFilesPanel();
    QWidget *createDiscussionsPanel();
    QWidget *createSecretsPanel();
    QWidget *createMobilePanel();

    SessionList *m_sessionsPanel = nullptr;
    EventTimeline *m_timelinePanel = nullptr;
    FileBrowser *m_filesPanel = nullptr;
    DiscussionPanel *m_discussionsPanel = nullptr;
};
