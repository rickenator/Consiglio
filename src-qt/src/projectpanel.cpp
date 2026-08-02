#include "projectpanel.h"

#include "uimetrics.h"

#include <QDateTime>
#include <QVBoxLayout>

ProjectPanel::ProjectPanel(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(UiMetrics::panelMargin(), UiMetrics::panelMargin(),
                               UiMetrics::panelMargin(), UiMetrics::panelMargin());
    layout->setSpacing(UiMetrics::panelSpacing());

    auto *title = new QLabel(tr("Projects"), this);
    title->setFont(UiMetrics::titleFont());
    layout->addWidget(title);

    auto *description = new QLabel(
        tr("Projects organize your work by workspace. Each project contains multiple sessions."),
        this);
    description->setWordWrap(true);
    description->setFont(UiMetrics::secondaryFont());
    description->setStyleSheet("color: #8b949e;");
    layout->addWidget(description);

    m_projectList = new QListWidget(this);
    m_projectList->setObjectName("projectList");
    m_projectList->setSpacing(UiMetrics::px(8));
    m_projectList->setStyleSheet(R"(
        QListWidget { background: #0d1117; border: 1px solid #30363d;
            border-radius: 14px; padding: 16px; }
        QListWidget::item { padding: 22px; border-radius: 10px; }
        QListWidget::item:selected { background: rgba(88, 166, 255, 0.18);
            color: #f0f6fc; }
    )");
    layout->addWidget(m_projectList, 1);

    m_emptyLabel = new QLabel(
        tr("No projects yet. Starting a session creates one from its workspace."), this);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setFont(UiMetrics::secondaryFont());
    m_emptyLabel->setStyleSheet("color: #8b949e;");
    layout->addWidget(m_emptyLabel);

    connect(m_projectList, &QListWidget::itemClicked, this,
            [this](QListWidgetItem *item) {
        emit projectSelected(item->data(Qt::UserRole).toString(),
                             item->data(Qt::UserRole + 1).toString());
    });
}

void ProjectPanel::setProjects(const QList<ProjectRecord> &projects) {
    m_projectList->clear();
    for (const auto &project : projects) {
        const QString activity = project.lastActivity > 0
            ? QDateTime::fromMSecsSinceEpoch(project.lastActivity).toString("MMM d, h:mm AP")
            : tr("No activity");
        auto *item = new QListWidgetItem(
            tr("%1\n%2\n%3 session(s) · %4")
                .arg(project.name, project.workspace)
                .arg(project.sessionCount)
                .arg(activity), m_projectList);
        item->setData(Qt::UserRole, project.id);
        item->setData(Qt::UserRole + 1, project.name);
        item->setSizeHint(QSize(0, UiMetrics::px(108)));
    }
    m_emptyLabel->setVisible(projects.isEmpty());
}
