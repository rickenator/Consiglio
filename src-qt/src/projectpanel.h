#pragma once

#include "backend/appdatabase.h"

#include <QLabel>
#include <QListWidget>
#include <QWidget>

class ProjectPanel : public QWidget {
    Q_OBJECT
public:
    explicit ProjectPanel(QWidget *parent = nullptr);
    void setProjects(const QList<ProjectRecord> &projects);

signals:
    void projectSelected(const QString &projectId, const QString &projectName);

private:
    QListWidget *m_projectList = nullptr;
    QLabel *m_emptyLabel = nullptr;
};
