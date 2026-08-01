#pragma once

#include <QObject>
#include <QStackedWidget>
#include "sidebar.h"

class QWidget;

class PanelManager : public QObject {
    Q_OBJECT

public:
    explicit PanelManager(QStackedWidget *stack, QObject *parent = nullptr);
    ~PanelManager();

    void createPanels(QWidget *mainWindow);

private:
    QStackedWidget *m_stack = nullptr;
    QWidget *createWelcomePanel();
    QWidget *createSessionsPanel();
    QWidget *createTimelinePanel();
    QWidget *createFilesPanel();
    QWidget *createDiscussionsPanel();
    QWidget *createSecretsPanel();
    QWidget *createMobilePanel();
};
