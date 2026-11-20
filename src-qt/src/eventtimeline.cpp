#include "eventtimeline.h"
#include "uimetrics.h"
#include <QDateTime>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTextDocument>
#include <QTextEdit>
#include <QtMath>

namespace {

// ─── Markdown + math rendering (shared by assistant bubbles) ──────────────
// These helpers are unchanged from the previous timeline: they convert GitHub
// markdown and common TeX into native rich text so the transcript reads cleanly.

QString replaceMathExpressions(QString markdown, const QRegularExpression &pattern,
                               const QString &tokenPrefix, QVector<QString> &expressions) {
    QString result;
    int previousEnd = 0;
    auto iterator = pattern.globalMatch(markdown);
    while (iterator.hasNext()) {
        const auto match = iterator.next();
        result += markdown.mid(previousEnd, match.capturedStart() - previousEnd);
        const QString token = QString("CONSIGLIO%1%2TOKEN")
                                  .arg(tokenPrefix).arg(expressions.size());
        expressions.append(match.captured(1).trimmed());
        result += token;
        previousEnd = match.capturedEnd();
    }
    result += markdown.mid(previousEnd);
    return result;
}

QString mathHtml(QString expression) {
    expression = expression.toHtmlEscaped().trimmed();

    QRegularExpression fraction(R"(\\frac\{([^{}]*)\}\{([^{}]*)\})");
    while (expression.contains(fraction)) {
        expression.replace(fraction, "<span><sup>\\1</sup>&frasl;<sub>\\2</sub></span>");
    }
    expression.replace(QRegularExpression(R"(\\sqrt\{([^{}]*)\})"), "&radic;<span style='text-decoration: overline;'>\\1</span>");
    expression.replace(QRegularExpression(R"(\\boxed\{([^{}]*)\})"), "<span style='border: 1px solid #8b949e; padding: 0.15em 0.35em;'>\\1</span>");
    expression.replace(QRegularExpression(R"(\^\{([^{}]*)\})"), "<sup>\\1</sup>");
    expression.replace(QRegularExpression(R"(_\{([^{}]*)\})"), "<sub>\\1</sub>");
    expression.replace(QRegularExpression(R"(\^([A-Za-z0-9+-]+))"), "<sup>\\1</sup>");
    expression.replace(QRegularExpression(R"(_([A-Za-z0-9+-]+))"), "<sub>\\1</sub>");
    expression.replace(QRegularExpression(R"(\\(?:text|mathrm|mathbf|mathit)\{([^{}]*)\})"), "\\1");
    expression.replace(QRegularExpression(R"(\\begin\{[^{}]+\}|\\end\{[^{}]+\})"), "");

    const std::pair<const char *, const char *> symbols[] = {
        {"\\Delta", "&Delta;"}, {"\\partial", "&part;"}, {"\\nabla", "&nabla;"},
        {"\\Omega", "&Omega;"}, {"\\omega", "&omega;"}, {"\\sigma", "&sigma;"},
        {"\\lambda", "&lambda;"}, {"\\phi", "&phi;"}, {"\\infty", "&infin;"},
        {"\\sum", "&sum;"}, {"\\int", "&int;"}, {"\\prod", "&prod;"},
        {"\\langle", "&lang;"}, {"\\rangle", "&rang;"}, {"\\leq", "&le;"},
        {"\\geq", "&ge;"}, {"\\neq", "&ne;"}, {"\\approx", "&asymp;"},
        {"\\cdot", "&middot;"}, {"\\circ", "&#8728;"}, {"\\times", "&times;"},
        {"\\forall", "&forall;"}, {"\\in", "&isin;"}, {"\\to", "&rarr;"},
        {"\\mathbb{R}", "&#8477;"}, {"\\|", "&#8214;"}
    };
    for (const auto &symbol : symbols) {
        expression.replace(symbol.first, symbol.second);
    }
    expression.replace(QRegularExpression(R"(\\(?:left|right|big|Big|bigg|Bigg)\b)"), "");
    expression.replace("\\,", "&thinsp;");
    expression.replace("\\;", "&ensp;");
    expression.replace("\\!", "");
    expression.replace("\\\\", "<br>");
    expression.replace('\n', "<br>");
    return expression;
}

QString markdownBody(const QString &source) {
    QVector<QString> blockMath;
    QVector<QString> inlineMath;
    QString markdown = replaceMathExpressions(
        source, QRegularExpression(R"(\\\[([\s\S]*?)\\\])"), "BLOCKMATH", blockMath);
    markdown = replaceMathExpressions(
        markdown, QRegularExpression(R"(\$\$([\s\S]*?)\$\$)"), "BLOCKMATH", blockMath);
    markdown = replaceMathExpressions(
        markdown, QRegularExpression(R"(\\\(.*?\\\))"), "INLINEMATH", inlineMath);

    QTextDocument document;
    document.setMarkdown(markdown, QTextDocument::MarkdownDialectGitHub);
    const QString fullHtml = document.toHtml();
    const int bodyTag = fullHtml.indexOf("<body");
    const int bodyStart = bodyTag < 0 ? -1 : fullHtml.indexOf('>', bodyTag);
    const int bodyEnd = fullHtml.lastIndexOf("</body>");
    QString body = bodyStart >= 0 && bodyEnd > bodyStart
        ? fullHtml.mid(bodyStart + 1, bodyEnd - bodyStart - 1)
        : source.toHtmlEscaped();

    for (int i = 0; i < blockMath.size(); ++i) {
        const QString token = QString("CONSIGLIOBLOCKMATH%1TOKEN").arg(i);
        const QString equation = mathHtml(blockMath.at(i));
        const QString rendered = QString(
            "<div style='margin: 0.8em 0; padding: 0.8em; text-align: center; "
            "background: #161b22; border: 1px solid #30363d; border-radius: 0.5em;'> "
            "<span style=\"font-family: 'STIX Two Math', 'Cambria Math', serif;\">%1</span></div>")
            .arg(equation);
        body.replace(QRegularExpression(
            QString("<p[^>]*>\\s*%1\\s*</p>").arg(token)), rendered);
        body.replace(token, rendered);
    }
    for (int i = 0; i < inlineMath.size(); ++i) {
        const QString token = QString("CONSIGLIOINLINEMATH%1TOKEN").arg(i);
        const QString rendered = QString(
            "<span style=\"font-family: 'STIX Two Math', 'Cambria Math', serif; "
            "color: #f0f6fc;\">%1</span>").arg(mathHtml(inlineMath.at(i)));
        body.replace(token, rendered);
    }
    return body;
}

QString formatTimestamp(qint64 ms) {
    if (ms == 0) return "";
    return QDateTime::fromMSecsSinceEpoch(ms).toString("HH:mm:ss");
}

} // namespace

// ─── MessageWidget ────────────────────────────────────────────────────────

MessageWidget::MessageWidget(const EventModel::EventItem &event, QWidget *parent)
    : QWidget(parent) {
    build(event);
}

void MessageWidget::build(const EventModel::EventItem &event) {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    const QString ts = formatTimestamp(event.timestamp);

    switch (event.type) {
        case EventModel::UserMessage: {
            // Right-aligned bubble for the user's own prompt.
            auto *row = new QHBoxLayout();
            row->setContentsMargins(0, 0, 0, 0);
            row->addStretch();
            auto *bubble = new QLabel(event.content, this);
            bubble->setObjectName("userBubble");
            bubble->setWordWrap(true);
            bubble->setAlignment(Qt::AlignTop | Qt::AlignRight);
            bubble->setStyleSheet(R"(
                QLabel#userBubble {
                    background: #1f6feb; color: #ffffff;
                    border-radius: 12px; padding: 10px 14px;
                    font-family: 'Segoe UI', sans-serif;
                }
            )");
            bubble->setMaximumWidth(UiMetrics::px(520));
            row->addWidget(bubble, 0);
            if (!ts.isEmpty()) {
                auto *stamp = new QLabel(ts, this);
                stamp->setStyleSheet("color: #484f58;");
                stamp->setFont(UiMetrics::secondaryFont());
                row->addWidget(stamp, 0);
            }
            outer->addLayout(row);
            break;
        }

        case EventModel::AssistantMessage: {
            // Left-aligned rich-text bubble. A read-only QTextEdit renders the
            // GitHub markdown + math natively (and keeps the transcript testable).
            auto *header = new QHBoxLayout();
            header->setContentsMargins(0, 0, 0, 0);
            auto *tag = new QLabel("AGENT", this);
            tag->setStyleSheet("color: #3fb950; font-weight: 700;");
            tag->setFont(UiMetrics::secondaryFont());
            header->addWidget(tag);
            if (!ts.isEmpty()) {
                auto *stamp = new QLabel(ts, this);
                stamp->setStyleSheet("color: #768390;");
                stamp->setFont(UiMetrics::secondaryFont());
                header->addWidget(stamp);
            }
            header->addStretch();
            outer->addLayout(header);

            auto *body = new QTextEdit(this);
            body->setObjectName("assistantBody");
            body->setReadOnly(true);
            body->setFrameShape(QFrame::NoFrame);
            body->setLineWrapMode(QTextEdit::WidgetWidth);
            body->setStyleSheet(R"(
                QTextEdit#assistantBody {
                    background: transparent; border: none;
                    color: #c9d1d9;
                    font-family: 'Inter', 'Noto Sans', sans-serif;
                }
            )");
            body->setHtml(markdownBody(event.content));
            // Size to content so the bubble hugs the text.
            body->document()->adjustSize();
            const int docHeight = qRound(body->document()->size().height()) + 8;
            body->setFixedHeight(qMax(docHeight, UiMetrics::px(24)));
            outer->addWidget(body);
            break;
        }

        case EventModel::CommandOutput: {
            // Collapsible tool/command output block.
            auto *row = new QHBoxLayout();
            row->setContentsMargins(0, 0, 0, 0);
            m_toggleBtn = new QPushButton("▸ tool output", this);
            m_toggleBtn->setCursor(Qt::PointingHandCursor);
            m_toggleBtn->setStyleSheet(R"(
                QPushButton {
                    background: #161b22; color: #8b949e; border: 1px solid #30363d;
                    border-radius: 8px; padding: 6px 10px; text-align: left;
                }
                QPushButton:hover { background: #21262d; color: #c9d1d9; }
            )");
            row->addWidget(m_toggleBtn, 0);
            if (!ts.isEmpty()) {
                auto *stamp = new QLabel(ts, this);
                stamp->setStyleSheet("color: #484f58;");
                stamp->setFont(UiMetrics::secondaryFont());
                row->addWidget(stamp);
            }
            row->addStretch();
            outer->addLayout(row);

            m_collapsibleBody = new QTextEdit(this);
            m_collapsibleBody->setObjectName("toolOutput");
            m_collapsibleBody->setReadOnly(true);
            m_collapsibleBody->setFrameShape(QFrame::NoFrame);
            m_collapsibleBody->setLineWrapMode(QTextEdit::WidgetWidth);
            m_collapsibleBody->setPlainText(event.content);
            m_collapsibleBody->setStyleSheet(R"(
                QTextEdit#toolOutput {
                    background: #161b22; border: 1px solid #30363d;
                    border-radius: 8px; padding: 10px;
                    font-family: 'JetBrains Mono', 'Fira Code', monospace;
                    color: #c9d1d9;
                }
            )");
            outer->addWidget(m_collapsibleBody);

            // Start collapsed by default so the conversation stays readable.
            setCollapsed(true);
            connect(m_toggleBtn, &QPushButton::clicked, this, [this]() {
                setCollapsed(!m_collapsed);
            });
            break;
        }

        case EventModel::SystemEvent:
        case EventModel::Error:
        case EventModel::ApprovalRequest: {
            const QString color = event.type == EventModel::Error ? "#f85149"
                                    : event.type == EventModel::ApprovalRequest ? "#d29922"
                                    : "#8b949e";
            const QString label = event.type == EventModel::Error ? "ERROR"
                                    : event.type == EventModel::ApprovalRequest ? "APPROVAL"
                                    : "SYSTEM";
            auto *row = new QHBoxLayout();
            row->setContentsMargins(0, 0, 0, 0);
            auto *tag = new QLabel(QString("[%1]").arg(label), this);
            tag->setStyleSheet(QString("color: %1; font-weight: 700;").arg(color));
            tag->setFont(UiMetrics::secondaryFont());
            row->addWidget(tag);
            auto *text = new QLabel(event.content, this);
            text->setWordWrap(true);
            text->setStyleSheet(QString("color: %1;").arg(color));
            text->setFont(UiMetrics::secondaryFont());
            row->addWidget(text, 1);
            if (!ts.isEmpty()) {
                auto *stamp = new QLabel(ts, this);
                stamp->setStyleSheet("color: #484f58;");
                stamp->setFont(UiMetrics::secondaryFont());
                row->addWidget(stamp);
            }
            outer->addLayout(row);
            break;
        }
    }
}

void MessageWidget::setCollapsed(bool collapsed) {
    m_collapsed = collapsed;
    if (m_collapsibleBody) m_collapsibleBody->setVisible(!collapsed);
    if (m_toggleBtn) m_toggleBtn->setText(collapsed ? "▸ tool output" : "▾ tool output");
}

bool MessageWidget::isCollapsed() const { return m_collapsed; }

// ─── EventTimeline ────────────────────────────────────────────────────────

EventTimeline::EventTimeline(QWidget *parent) : QWidget(parent) {
    setupUI();
}

void EventTimeline::setupUI() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Scrollable conversation area.
    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName("conversationScroll");
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setStyleSheet(R"(
        QScrollArea#conversationScroll { background: #0d1117; border: none; }
    )");

    m_messagesHost = new QWidget(this);
    m_messagesLayout = new QVBoxLayout(m_messagesHost);
    m_messagesLayout->setContentsMargins(
        UiMetrics::panelMargin(), UiMetrics::panelMargin(),
        UiMetrics::panelMargin(), UiMetrics::panelMargin());
    m_messagesLayout->setSpacing(UiMetrics::px(14));

    m_emptyLabel = new QLabel(tr("No messages yet. Start a session and send a prompt."),
                               m_messagesHost);
    m_emptyLabel->setObjectName("emptyLabel");
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setFont(UiMetrics::secondaryFont());
    m_emptyLabel->setStyleSheet("color: #8b949e;");
    m_messagesLayout->addWidget(m_emptyLabel, 0, Qt::AlignCenter);

    m_scroll->setWidget(m_messagesHost);
    mainLayout->addWidget(m_scroll, 1);

    // Composer.
    auto *composer = new QWidget(this);
    composer->setObjectName("composer");
    auto *composerLayout = new QHBoxLayout(composer);
    composerLayout->setContentsMargins(
        UiMetrics::panelMargin(), 0, UiMetrics::panelMargin(),
        UiMetrics::panelMargin());
    composerLayout->setSpacing(UiMetrics::px(10));

    m_commandInput = new QLineEdit(composer);
    m_commandInput->setObjectName("composerInput");
    m_commandInput->setPlaceholderText(tr("Type a message…  ( / for commands )"));
    m_commandInput->setStyleSheet(R"(
        QLineEdit#composerInput {
            background: #161b22; border: 1px solid #30363d;
            border-radius: 12px; padding: 14px 18px; color: #c9d1d9;
            font-family: 'Segoe UI', sans-serif;
        }
        QLineEdit#composerInput:focus { border-color: #58a6ff; }
    )");
    composerLayout->addWidget(m_commandInput, 1);

    m_thinkingIndicator = new QLabel(composer);
    m_thinkingIndicator->setObjectName("thinkingIndicator");
    m_thinkingIndicator->setFont(UiMetrics::secondaryFont());
    m_thinkingIndicator->setStyleSheet(
        "color: #58a6ff; padding: 0 0.55em; font-weight: 600;");
    m_thinkingIndicator->setAccessibleName(tr("Agent is thinking"));
    m_thinkingIndicator->setVisible(false);
    composerLayout->addWidget(m_thinkingIndicator, 0);

    m_sendBtn = new QPushButton(tr("Send"), composer);
    m_sendBtn->setObjectName("sendButton");
    m_sendBtn->setStyleSheet(R"(
        QPushButton#sendButton {
            background: #238636; color: white; padding: 14px 26px;
            border-radius: 12px; font-weight: bold;
        }
        QPushButton#sendButton:hover { background: #2ea043; }
        QPushButton#sendButton:disabled { background: #484f58; color: #8b949e; }
    )");
    composerLayout->addWidget(m_sendBtn, 0);

    mainLayout->addWidget(composer);

    connect(m_sendBtn, &QPushButton::clicked, this, &EventTimeline::onSendCommand);
    connect(m_commandInput, &QLineEdit::returnPressed, this, &EventTimeline::onSendCommand);

    m_thinkingTimer = new QTimer(this);
    m_thinkingTimer->setInterval(90);
    connect(m_thinkingTimer, &QTimer::timeout, this, [this]() {
        static const QStringList frames = {
            QStringLiteral("◐"), QStringLiteral("◓"),
            QStringLiteral("◑"), QStringLiteral("◒")
        };
        m_thinkingIndicator->setText(
            tr("%1 Thinking").arg(frames.at(m_thinkingFrame % frames.size())));
        ++m_thinkingFrame;
    });
}

void EventTimeline::addEvent(const EventModel::EventItem &event) {
    appendEvent(event);
    m_emptyLabel->setVisible(false);
}

void EventTimeline::setEvents(const QList<EventModel::EventItem> &events) {
    // Remove any previously appended message widgets (keep the empty-state label).
    for (int i = m_messagesLayout->count() - 1; i >= 0; --i) {
        auto *item = m_messagesLayout->itemAt(i);
        if (!item) continue;
        QWidget *w = item->widget();
        if (w && w != m_emptyLabel) {
            m_messagesLayout->removeItem(item);
            w->deleteLater();
        }
    }
    m_emptyLabel->setVisible(events.isEmpty());
    for (const auto &event : events) appendEvent(event);
}

void EventTimeline::clearEvents() {
    setEvents({});
    emit eventsCleared();
}

void EventTimeline::onSendCommand() {
    if (m_thinking) return;
    auto cmd = m_commandInput->text().trimmed();
    if (!cmd.isEmpty()) {
        emit commandExecuted(cmd, {});
        m_commandInput->clear();
    }
}

void EventTimeline::setThinking(bool thinking) {
    if (m_thinking == thinking) return;
    m_thinking = thinking;
    m_sendBtn->setEnabled(!thinking);
    m_thinkingIndicator->setVisible(thinking);
    if (thinking) {
        m_thinkingFrame = 1;
        m_thinkingIndicator->setText(tr("◐ Thinking"));
        m_thinkingTimer->start();
    } else {
        m_thinkingTimer->stop();
        m_thinkingIndicator->clear();
    }
}

bool EventTimeline::isThinking() const { return m_thinking; }

void EventTimeline::appendEvent(const EventModel::EventItem &event) {
    auto *widget = new MessageWidget(event, m_messagesHost);
    m_messagesLayout->addWidget(widget);
    scrollToBottom();
}

void EventTimeline::scrollToBottom() {
    if (auto *vbar = m_scroll->verticalScrollBar())
        vbar->setValue(vbar->maximum());
}
