#include "eventtimeline.h"
#include <QLineEdit>
#include "uimetrics.h"
#include <QDateTime>
#include <QScrollBar>
#include <QLineEdit>

EventTimeline::EventTimeline(QWidget *parent) : QWidget(parent) {
    setupUI();
}

void EventTimeline::setupUI() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(UiMetrics::panelMargin(), UiMetrics::panelMargin(),
                                   UiMetrics::panelMargin(), UiMetrics::panelMargin());
    mainLayout->setSpacing(UiMetrics::panelSpacing());

    // Header
    auto *headerLayout = new QHBoxLayout();
    auto *titleLabel = new QLabel(tr("Timeline"), this);
    titleLabel->setFont(UiMetrics::titleFont());
    titleLabel->setStyleSheet("color: #f0f6fc;");
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();

    auto *clearBtn = new QPushButton(tr("Clear"), this);
    clearBtn->setStyleSheet(R"(
        QPushButton {
            background: transparent;
            border: 1px solid #30363d;
            color: #c9d1d9;
            padding: 18px 28px;
            border-radius: 12px;
        }
        QPushButton:hover { background: rgba(255,255,255,0.06); }
    )");
    headerLayout->addWidget(clearBtn);
    mainLayout->addLayout(headerLayout);

    // Text edit for events
    m_textEdit = new QTextEdit(this);
    m_textEdit->setReadOnly(true);
    m_textEdit->setStyleSheet(R"(
        QTextEdit {
            background: #0d1117;
            border: 1px solid #30363d;
            border-radius: 14px;
            padding: 24px;
            font-family: 'JetBrains Mono', 'Fira Code', monospace;
        }
    )");

    mainLayout->addWidget(m_textEdit);

    // Command input area
    auto *inputLayout = new QHBoxLayout();
    m_commandInput = new QLineEdit(this);
    m_commandInput->setPlaceholderText(tr("Enter command to execute..."));
    m_commandInput->setStyleSheet(R"(
        QLineEdit {
            background: #161b22;
            border: 1px solid #30363d;
            border-radius: 12px;
            padding: 20px;
            color: #c9d1d9;
        }
        QLineEdit:focus { border-color: #58a6ff; }
    )");
    inputLayout->addWidget(m_commandInput);

    m_sendBtn = new QPushButton(tr("Send"), this);
    m_sendBtn->setStyleSheet(R"(
        QPushButton {
            background: #238636;
            color: white;
            padding: 20px 32px;
            border-radius: 12px;
            font-weight: bold;
        }
        QPushButton:hover { background: #2ea043; }
    )");
    inputLayout->addWidget(m_sendBtn);

    mainLayout->addLayout(inputLayout);

    // Empty state label
    m_emptyLabel = new QLabel(tr("No events yet. Start a session to see activity here."), this);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setFont(UiMetrics::secondaryFont());
    m_emptyLabel->setStyleSheet("color: #8b949e;");
    mainLayout->addWidget(m_emptyLabel);

    // Connections
    connect(clearBtn, &QPushButton::clicked, this, &EventTimeline::clearEvents);
    connect(m_sendBtn, &QPushButton::clicked, this, &EventTimeline::onSendCommand);
    connect(m_commandInput, &QLineEdit::returnPressed, this, &EventTimeline::onSendCommand);
}

void EventTimeline::addEvent(const EventModel::EventItem &event) {
    appendEvent(event);
    m_emptyLabel->setVisible(false);
}

void EventTimeline::clearEvents() {
    m_textEdit->clear();
    m_emptyLabel->setVisible(true);
}

void EventTimeline::onSendCommand() {
    auto cmd = m_commandInput->text().trimmed();
    if (!cmd.isEmpty()) {
        emit commandExecuted(cmd, {});
        m_commandInput->clear();
    }
}

void EventTimeline::appendEvent(const EventModel::EventItem &event) {
    QString html;
    auto ts = formatTimestamp(event.timestamp);
    QString safeContent = event.content.toHtmlEscaped();
    safeContent.replace("\n", "<br>");

    switch (event.type) {
        case EventModel::SystemEvent:
            html = QString("<div style='color: #8b949e; margin: 4px 0;'><b>[SYSTEM]</b> %1 <span style='color: #484f58;'>%2</span></div>")
                       .arg(safeContent).arg(ts);
            break;
        case EventModel::UserMessage:
            html = QString("<div style='margin: 6px 0;'><b style='color: #58a6ff;'>[YOU]</b> %1 <span style='color: #484f58;'>%2</span></div>")
                       .arg(safeContent).arg(ts);
            break;
        case EventModel::AssistantMessage:
            html = QString("<div style='margin: 6px 0;'><b style='color: #3fb950;'>[AGENT]</b> %1 <span style='color: #484f58;'>%2</span></div>")
                       .arg(safeContent).arg(ts);
            break;
        case EventModel::CommandOutput:
            html = QString("<pre style='background: #161b22; padding: 1em; border-radius: 0.5em; margin: 0.5em 0; color: #c9d1d9;'>%1</pre><span style='color: #768390;'>%2</span>")
                       .arg(event.content.toHtmlEscaped()).arg(ts);
            break;
        case EventModel::Error:
            html = QString("<div style='margin: 6px 0;'><b style='color: #f85149;'>[ERROR]</b> %1 <span style='color: #484f58;'>%2</span></div>")
                       .arg(safeContent).arg(ts);
            break;
        case EventModel::ApprovalRequest:
            html = QString("<div style='margin: 6px 0;'><b style='color: #d29922;'>[APPROVAL]</b> %1 <span style='color: #484f58;'>%2</span></div>")
                       .arg(safeContent).arg(ts);
            break;
    }

    m_textEdit->append(html);

    // Auto-scroll to bottom
    auto *vbar = m_textEdit->verticalScrollBar();
    vbar->setValue(vbar->maximum());
}

QString EventTimeline::formatTimestamp(qint64 ms) const {
    if (ms == 0) return "";
    auto dt = QDateTime::fromMSecsSinceEpoch(ms);
    return dt.toString("HH:mm:ss");
}
