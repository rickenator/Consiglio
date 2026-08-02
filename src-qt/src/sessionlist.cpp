#include "sessionlist.h"
#include <QHeaderView>
#include <QDateTime>
#include <QMessageBox>
#include "uimetrics.h"

SessionList::SessionList(QWidget *parent) : QWidget(parent) {
    setupUI();
}

void SessionList::setupUI() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(UiMetrics::panelMargin(), UiMetrics::panelMargin(),
                                   UiMetrics::panelMargin(), UiMetrics::panelMargin());
    mainLayout->setSpacing(UiMetrics::panelSpacing());

    // Header with title and stop button
    auto *headerLayout = new QHBoxLayout();
    auto *titleLabel = new QLabel(tr("Your sessions"), this);
    titleLabel->setFont(UiMetrics::titleFont());
    titleLabel->setStyleSheet("color: #f0f6fc;");
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();

    m_stopBtn = new QPushButton(tr("Stop Selected"), this);
    m_stopBtn->setStyleSheet(R"(
        QPushButton {
            background: #da3633;
            color: white;
            padding: 18px 28px;
            border-radius: 12px;
        }
        QPushButton:hover { background: #f85149; }
        QPushButton:disabled { background: #484f58; color: #8b949e; }
    )");
    m_stopBtn->setEnabled(false);
    headerLayout->addWidget(m_stopBtn);
    mainLayout->addLayout(headerLayout);

    // Tree view for sessions
    m_treeView = new QTreeView(this);
    m_treeView->setAlternatingRowColors(true);
    m_treeView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_treeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_treeView->setHeaderHidden(true);
    m_treeView->setStyleSheet(R"(
        QTreeView {
            background: #0d1117;
            border: 1px solid #30363d;
            border-radius: 14px;
            padding: 16px;
        }
        QTreeView::item {
            padding: 24px;
            border-radius: 10px;
        }
        QTreeView::item:selected {
            background: rgba(88, 166, 255, 0.15);
        }
    )");

    m_model = new SessionModel(this);
    m_treeView->setModel(m_model);
    m_treeView->setColumnWidth(0, 300);

    mainLayout->addWidget(m_treeView);

    // Empty state label
    m_emptyLabel = new QLabel(tr("No sessions yet. Start a session to see it here."), this);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setFont(UiMetrics::secondaryFont());
    m_emptyLabel->setStyleSheet("color: #8b949e;");
    mainLayout->addWidget(m_emptyLabel);

    // Connections
    connect(m_treeView, &QTreeView::doubleClicked, this, &SessionList::onSessionDoubleClicked);
    connect(m_stopBtn, &QPushButton::clicked, this, &SessionList::onStopSession);
    connect(m_treeView->selectionModel(), &QItemSelectionModel::currentChanged,
            this, [this](const QModelIndex &current) {
        m_stopBtn->setEnabled(current.isValid() &&
            m_model->data(current, SessionModel::StatusRole).toString() == "running");
    });
}

void SessionList::setSessions(const QList<SessionRecord> &sessions) {
    m_model->setSessions(sessions);
    m_emptyLabel->setVisible(sessions.isEmpty());
    const QModelIndex current = m_treeView->currentIndex();
    m_stopBtn->setEnabled(current.isValid() &&
        m_model->data(current, SessionModel::StatusRole).toString() == "running");
}

void SessionList::onSessionDoubleClicked(const QModelIndex &index) {
    if (index.isValid()) {
        emit sessionSelected(m_model->data(index, SessionModel::IdRole).toString());
    }
}

void SessionList::onStopSession() {
    auto idx = m_treeView->currentIndex();
    if (idx.isValid()) {
        if (m_model->data(idx, SessionModel::StatusRole).toString() != "running") return;
        auto sessionId = m_model->data(idx, SessionModel::IdRole).toString();
        emit sessionStopped(sessionId);
    }
}
