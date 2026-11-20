#include "sessionrail.h"

#include <QDir>
#include <QFileDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidgetItem>

SessionRail::SessionRail(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto *title = new QLabel(tr("Session"), this);
    title->setStyleSheet("color: #f0f6fc; font-weight: 700;");
    layout->addWidget(title);

    auto *dirLabel = new QLabel(tr("Workspace"), this);
    dirLabel->setStyleSheet("color: #8b949e;");
    layout->addWidget(dirLabel);

    auto *dirRow = new QHBoxLayout();
    m_dirEdit = new QLineEdit(QDir::homePath(), this);
    m_dirEdit->setObjectName("railDir");
    m_dirEdit->setStyleSheet(R"(
        QLineEdit#railDir {
            background: #0d1117; border: 1px solid #30363d;
            border-radius: 8px; padding: 8px 10px; color: #c9d1d9;
        }
    )");
    auto *browseBtn = new QPushButton(tr("…"), this);
    browseBtn->setFixedWidth(36);
    dirRow->addWidget(m_dirEdit, 1);
    dirRow->addWidget(browseBtn);
    layout->addLayout(dirRow);

    m_startBtn = new QPushButton(tr("Start Session"), this);
    m_startBtn->setObjectName("railStart");
    m_startBtn->setStyleSheet(R"(
        QPushButton#railStart {
            background: #238636; color: white; padding: 10px;
            border-radius: 8px; font-weight: bold;
        }
        QPushButton#railStart:hover { background: #2ea043; }
    )");
    layout->addWidget(m_startBtn);

    m_infoLabel = new QLabel(tr("No active session."), this);
    m_infoLabel->setObjectName("railInfo");
    m_infoLabel->setWordWrap(true);
    m_infoLabel->setStyleSheet("color: #8b949e;");
    layout->addWidget(m_infoLabel);

    auto *sessionsTitle = new QLabel(tr("Previous Sessions"), this);
    sessionsTitle->setStyleSheet("color: #f0f6fc; font-weight: 700; margin-top: 8px;");
    layout->addWidget(sessionsTitle);

    m_sessionsList = new QListWidget(this);
    m_sessionsList->setObjectName("railSessions");
    m_sessionsList->setStyleSheet(R"(
        QListWidget#railSessions {
            background: #0d1117; border: 1px solid #30363d;
            border-radius: 8px; color: #c9d1d9;
        }
        QListWidget#railSessions::item {
            padding: 6px 8px; border-radius: 6px;
        }
        QListWidget#railSessions::item:selected {
            background: rgba(88, 166, 255, 0.18); color: #f0f6fc;
        }
    )");
    layout->addWidget(m_sessionsList, 1);

    connect(browseBtn, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(
            this, tr("Select Workspace"), m_dirEdit->text(),
            QFileDialog::ShowDirsOnly);
        if (!dir.isEmpty()) m_dirEdit->setText(dir);
    });

    // Refresh the previous-sessions list whenever the workspace changes.
    connect(m_dirEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        const QString cwd = QDir::cleanPath(text.trimmed());
        if (!cwd.isEmpty() && QDir(cwd).exists()) emit dirChanged(cwd);
    });

    connect(m_startBtn, &QPushButton::clicked, this, [this]() {
        if (m_startBtn->text() == tr("Stop Session")) {
            emit stopRequested();
        } else {
            const QString cwd = QDir::cleanPath(m_dirEdit->text().trimmed());
            if (QDir(cwd).exists()) emit startRequested(cwd);
        }
    });

    // Clicking a previous session resumes it.
    connect(m_sessionsList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const QString id = item->data(Qt::UserRole).toString();
        const QString cwd = item->data(Qt::UserRole + 1).toString();
        emit resumeRequested(id, cwd);
    });
}

void SessionRail::setSessionActive(bool active, const QString &title, const QString &cwd) {
    if (active) {
        m_startBtn->setText(tr("Stop Session"));
        m_startBtn->setStyleSheet(R"(
            QPushButton#railStart {
                background: #da3633; color: white; padding: 10px;
                border-radius: 8px; font-weight: bold;
            }
            QPushButton#railStart:hover { background: #f85149; }
        )");
        const QString name = title.isEmpty() ? tr("(untitled)") : title;
        m_infoLabel->setText(QString("<b>%1</b><br>%2").arg(name, cwd));
    } else {
        m_startBtn->setText(tr("Start Session"));
        m_startBtn->setStyleSheet(R"(
            QPushButton#railStart {
                background: #238636; color: white; padding: 10px;
                border-radius: 8px; font-weight: bold;
            }
            QPushButton#railStart:hover { background: #2ea043; }
        )");
        m_infoLabel->setText(tr("No active session."));
    }
}

void SessionRail::setSessions(const QList<AcpClient::SessionInfo> &sessions) {
    m_sessionsList->blockSignals(true);
    m_sessionsList->clear();
    for (const auto &s : sessions) {
        auto *item = new QListWidgetItem(s.title.isEmpty() ? tr("(untitled)") : s.title);
        item->setData(Qt::UserRole, s.id);
        item->setData(Qt::UserRole + 1, s.cwd);
        item->setToolTip(QString("%1\n%2").arg(s.updatedAt, s.cwd));
        m_sessionsList->addItem(item);
    }
    m_sessionsList->blockSignals(false);
}
