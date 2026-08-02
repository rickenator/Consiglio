#include "eventtimeline.h"
#include "uimetrics.h"
#include <QDateTime>
#include <QRegularExpression>
#include <QScrollBar>
#include <QLineEdit>
#include <QTextDocument>

namespace {

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

    // Handle common structural TeX before replacing individual symbols. This is
    // intentionally small, deterministic native rendering rather than a web view.
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
        markdown, QRegularExpression(R"(\\\((.*?)\\\))"), "INLINEMATH", inlineMath);

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
            "background: #161b22; border: 1px solid #30363d; border-radius: 0.5em;'>"
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

} // namespace

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
            html = QString(
                "<div style=\"margin: 0.8em 0; padding: 0.8em; border-left: 3px solid #3fb950; "
                "font-family: 'Inter', 'Noto Sans', sans-serif; line-height: 1.35; color: #c9d1d9;\">"
                "<div><b style='color: #3fb950;'>[AGENT]</b> "
                "<span style='color: #768390;'>%2</span></div>%1</div>")
                       .arg(markdownBody(event.content), ts);
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
