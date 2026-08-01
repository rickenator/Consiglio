#include "discussionpanel.h"
#include <QDateTime>
#include <QScrollBar>

DiscussionPanel::DiscussionPanel(QWidget *parent) : QWidget(parent) {
    setupUI();
}

void DiscussionPanel::setupUI() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);

    // Header
    auto *headerLayout = new QHBoxLayout();
    auto *titleLabel = new QLabel(tr("Discussions"), this);
    titleLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #58a6ff;");
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();
    mainLayout->addLayout(headerLayout);

    // Message list
    m_messageList = new QListWidget(this);
    m_messageList->setStyleSheet(R"(
        QListWidget {
            background: #0d1117;
            border: 1px solid #30363d;
            border-radius: 6px;
            padding: 8px;
        }
        QListWidget::item {
            padding: 10px;
            border-radius: 6px;
            margin: 2px 0;
        }
        QListWidget::item:selected {
            background: rgba(88, 166, 255, 0.15);
        }
    )");

    mainLayout->addWidget(m_messageList);

    // Input area
    auto *inputLayout = new QHBoxLayout();
    m_inputEdit = new QTextEdit(this);
    m_inputEdit->setMaximumHeight(100);
    m_inputEdit->setPlaceholderText(tr("Type a message..."));
    m_inputEdit->setStyleSheet(R"(
        QTextEdit {
            background: #161b22;
            border: 1px solid #30363d;
            border-radius: 4px;
            padding: 8px;
            color: #c9d1d9;
        }
        QTextEdit:focus { border-color: #58a6ff; }
    )");
    inputLayout->addWidget(m_inputEdit);

    m_sendBtn = new QPushButton(tr("Send"), this);
    m_sendBtn->setStyleSheet(R"(
        QPushButton {
            background: #238636;
            color: white;
            padding: 8px 16px;
            border-radius: 4px;
            font-weight: bold;
        }
        QPushButton:hover { background: #2ea043; }
    )");
    inputLayout->addWidget(m_sendBtn);

    mainLayout->addLayout(inputLayout);

    // Empty state label
    m_emptyLabel = new QLabel(tr("No messages yet. Start a discussion."), this);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setStyleSheet("color: #8b949e; font-size: 13px; padding: 20px;");
    mainLayout->addWidget(m_emptyLabel);

    // Connections
    connect(m_sendBtn, &QPushButton::clicked, this, [this]() {
        auto msg = m_inputEdit->toPlainText().trimmed();
        if (!msg.isEmpty()) {
            emit messageSent(msg);
            addMessage("You", msg, true);
            m_inputEdit->clear();
        }
    });

    connect(m_inputEdit, &QTextEdit::textChanged, this, [this]() {
        // Auto-resize based on content
        auto doc = m_inputEdit->document();
        auto height = doc->size().height() + 20;
        m_inputEdit->setFixedHeight(qMin(static_cast<int>(height), 150));
    });
}

void DiscussionPanel::addMessage(const QString &sender, const QString &content, bool isUser) {
    auto ts = QDateTime::currentDateTime().toString("HH:mm:ss");
    auto color = isUser ? "#58a6ff" : "#3fb950";
    auto html = QString("<div style='margin: 6px 0; padding: 10px; background: %1; border-radius: 6px;'><b>%2</b> <span style='color: #484f58;'>%3</span><br>%4</div>")
                   .arg(isUser ? "rgba(88, 166, 255, 0.1)" : "rgba(63, 185, 80, 0.1)")
                   .arg(sender).arg(ts).arg(content);

    m_messageList->addItem(html);
    m_emptyLabel->setVisible(false);

    // Auto-scroll to bottom
    auto *vbar = m_messageList->verticalScrollBar();
    vbar->setValue(vbar->maximum());
}
