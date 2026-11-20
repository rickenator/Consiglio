#include "chatpane.h"

#include <QDateTime>
#include <QKeyEvent>
#include <QScrollBar>
#include <QTextCursor>
#include <QTextDocument>
#include <QtMath>

namespace {

// Convert GitHub markdown + simple math into rich text for agent/thinking bodies.
QString renderMarkdown(const QString &source) {
    QTextDocument document;
    document.setMarkdown(source, QTextDocument::MarkdownDialectGitHub);
    const QString html = document.toHtml();
    const int bodyTag = html.indexOf("<body");
    const int start = bodyTag < 0 ? -1 : html.indexOf('>', bodyTag);
    const int end = html.lastIndexOf("</body>");
    if (start >= 0 && end > start) return html.mid(start + 1, end - start - 1);
    return source.toHtmlEscaped();
}

QString timestamp() {
    return QDateTime::currentDateTime().toString("HH:mm:ss");
}

// Per-kind body stylesheets so font sizes can be set independently and
// re-applied live (see ChatBlock::applyFontSizes).
QString userBubbleStyle(int chatPx) {
    return QStringLiteral(
        "QLabel#userBubble {"
        " background: #1f6feb; color: #ffffff;"
        " border-radius: 12px; padding: 10px 14px;"
        " font-family: 'Segoe UI', sans-serif; font-size: %1px; }")
        .arg(chatPx);
}

QString agentBodyStyle(int chatPx) {
    return QStringLiteral(
        "QTextEdit#agentBody {"
        " background: transparent; border: none; color: #c9d1d9;"
        " font-family: 'Inter', 'Noto Sans', sans-serif; font-size: %1px; }")
        .arg(chatPx);
}

QString thinkingBodyStyle(int thinkingPx) {
    return QStringLiteral(
        "QTextEdit#thinkingBody {"
        " background: #161b22; border: 1px solid #30363d;"
        " border-radius: 8px; padding: 10px; color: #8b949e;"
        " font-family: 'Inter', sans-serif; font-size: %1px; }")
        .arg(thinkingPx);
}

QString toolBodyStyle(int toolPx) {
    return QStringLiteral(
        "QTextEdit#toolBody {"
        " background: #161b22; border: 1px solid #30363d;"
        " border-radius: 8px; padding: 8px; color: #c9d1d9;"
        " font-family: 'JetBrains Mono', 'Fira Code', monospace; font-size: %1px; }")
        .arg(toolPx);
}

} // namespace

// ─── ChatBlock ─────────────────────────────────────────────────────────────

ChatBlock::ChatBlock(Kind kind, int chatPx, int thinkingPx, int toolPx, QWidget *parent)
    : QWidget(parent), m_kind(kind) {
    build(kind, chatPx, thinkingPx, toolPx);
}

ChatBlock::~ChatBlock() = default;

ChatBlock::Kind ChatBlock::kind() const { return m_kind; }
QString ChatBlock::toolId() const { return m_toolId; }

void ChatBlock::build(Kind kind, int chatPx, int thinkingPx, int toolPx) {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    switch (kind) {
        case User: {
            auto *row = new QHBoxLayout();
            row->setContentsMargins(0, 0, 0, 0);
            row->addStretch();
            // Timestamp sits just left of the right-aligned bubble.
            auto *stamp = new QLabel(timestamp(), this);
            stamp->setStyleSheet("color: #768390;");
            row->addWidget(stamp, 0);
            auto *bubble = new QLabel(this);
            bubble->setObjectName("userBubble");
            bubble->setWordWrap(true);
            bubble->setAlignment(Qt::AlignTop | Qt::AlignRight);
            bubble->setStyleSheet(userBubbleStyle(chatPx));
            bubble->setMaximumWidth(520);
            row->addWidget(bubble, 0);
            outer->addLayout(row);
            m_body = bubble;
            break;
        }

        case Agent: {
            auto *header = new QHBoxLayout();
            header->setContentsMargins(0, 0, 0, 0);
            auto *tag = new QLabel("AGENT", this);
            tag->setStyleSheet("color: #3fb950; font-weight: 700;");
            header->addWidget(tag);
            auto *stamp = new QLabel(timestamp(), this);
            stamp->setStyleSheet("color: #768390;");
            header->addWidget(stamp);
            header->addStretch();
            outer->addLayout(header);

            auto *body = new QTextEdit(this);
            body->setObjectName("agentBody");
            body->setReadOnly(true);
            body->setFrameShape(QFrame::NoFrame);
            body->setLineWrapMode(QTextEdit::WidgetWidth);
            body->setStyleSheet(agentBodyStyle(chatPx));
            outer->addWidget(body);
            m_body = body;
            break;
        }

        case Thinking: {
            auto *header = new QHBoxLayout();
            header->setContentsMargins(0, 0, 0, 0);
            m_toggleBtn = new QPushButton("▾ thinking", this);
            m_toggleBtn->setCursor(Qt::PointingHandCursor);
            m_toggleBtn->setStyleSheet(R"(
                QPushButton {
                    background: #161b22; color: #d29922; border: 1px solid #30363d;
                    border-radius: 8px; padding: 5px 10px; text-align: left;
                }
                QPushButton:hover { background: #21262d; }
            )");
            header->addWidget(m_toggleBtn, 0);
            auto *stamp = new QLabel(timestamp(), this);
            stamp->setStyleSheet("color: #768390;");
            header->addWidget(stamp, 0);
            header->addStretch();
            outer->addLayout(header);

            auto *body = new QTextEdit(this);
            body->setObjectName("thinkingBody");
            body->setReadOnly(true);
            body->setFrameShape(QFrame::NoFrame);
            body->setLineWrapMode(QTextEdit::WidgetWidth);
            body->setStyleSheet(thinkingBodyStyle(thinkingPx));
            outer->addWidget(body);
            m_body = body;
            connect(m_toggleBtn, &QPushButton::clicked, this, [this]() {
                m_collapsed = !m_collapsed;
                m_body->setVisible(!m_collapsed);
                m_toggleBtn->setText(m_collapsed ? "▸ thinking" : "▾ thinking");
            });
            break;
        }

        case Tool: {
            auto *header = new QHBoxLayout();
            header->setContentsMargins(0, 0, 0, 0);
            m_toggleBtn = new QPushButton("▸ tool", this);
            m_toggleBtn->setCursor(Qt::PointingHandCursor);
            m_toggleBtn->setStyleSheet(R"(
                QPushButton {
                    background: #161b22; color: #58a6ff; border: 1px solid #30363d;
                    border-radius: 8px; padding: 5px 10px; text-align: left;
                }
                QPushButton:hover { background: #21262d; }
            )");
            header->addWidget(m_toggleBtn, 0);
            auto *stamp = new QLabel(timestamp(), this);
            stamp->setStyleSheet("color: #768390;");
            header->addWidget(stamp, 0);
            header->addStretch();
            outer->addLayout(header);

            auto *body = new QTextEdit(this);
            body->setObjectName("toolBody");
            body->setReadOnly(true);
            body->setFrameShape(QFrame::NoFrame);
            body->setLineWrapMode(QTextEdit::WidgetWidth);
            body->setStyleSheet(toolBodyStyle(toolPx));
            outer->addWidget(body);
            m_body = body;
            // Start collapsed; expand on first output.
            m_collapsed = true;
            m_body->setVisible(false);
            connect(m_toggleBtn, &QPushButton::clicked, this, [this]() {
                m_collapsed = !m_collapsed;
                m_body->setVisible(!m_collapsed);
                m_toggleBtn->setText(m_collapsed ? "▸ tool" : "▾ tool");
            });
            break;
        }
    }
}

void ChatBlock::appendText(const QString &text) {
    m_accumulated += text;
    if (m_kind == User) {
        static_cast<QLabel *>(m_body)->setText(m_accumulated);
    } else {
        auto *body = static_cast<QTextEdit *>(m_body);
        // Append the delta at the end rather than replacing the whole document:
        // cheaper, and it doesn't disturb the layout of text already on screen.
        body->moveCursor(QTextCursor::End);
        body->insertPlainText(text);
        resizeToFit(); // grow to fit while streaming so it isn't clipped
    }
}

void ChatBlock::resizeToFit() {
    if (m_kind != Thinking && m_kind != Agent) return;
    auto *body = static_cast<QTextEdit *>(m_body);
    body->document()->adjustSize();
    const int h = qRound(body->document()->size().height()) + 8;
    // While streaming, the fixed height only ever GROWS. A fresh measurement can
    // dip below the previous one (re-layout, partial lines), and letting it shrink
    // is what makes the panel jump up and down. Grow-only keeps it stable.
    m_streamHeight = qMax(m_streamHeight, qMax(h, 24));
    body->setFixedHeight(m_streamHeight);
}

void ChatBlock::finalize() {
    if (m_kind == Agent || m_kind == Thinking) {
        auto *body = static_cast<QTextEdit *>(m_body);
        body->setHtml(renderMarkdown(m_accumulated));
        // Size the body to its content. This is a one-time settle at turn end, so
        // unlike the grow-only streaming path it may shrink to the true height.
        m_streamHeight = 0;
        body->document()->adjustSize();
        const int h = qRound(body->document()->size().height()) + 8;
        body->setFixedHeight(qMax(h, 24));
    }
    // NOTE: thinking is intentionally NOT collapsed here — it persists until the
    // next round so the user can read the reasoning. The ▾/▸ toggle still works.
}

void ChatBlock::setToolStatus(const QString &status, const QString &output) {
    if (m_kind != Tool) return;
    auto *body = static_cast<QTextEdit *>(m_body);
    QString header = QStringLiteral("status: %1\n").arg(status);
    if (!output.isEmpty()) header += output;
    body->setPlainText(header);
    // Expand to show the result.
    if (m_collapsed) {
        m_collapsed = false;
        m_body->setVisible(true);
        m_toggleBtn->setText("▾ tool");
    }
}

QString ChatBlock::plainText() const { return m_accumulated; }

void ChatBlock::applyFontSizes(int chatPx, int thinkingPx, int toolPx) {
    switch (m_kind) {
        case User:
            static_cast<QLabel *>(m_body)->setStyleSheet(userBubbleStyle(chatPx));
            break;
        case Agent:
            static_cast<QTextEdit *>(m_body)->setStyleSheet(agentBodyStyle(chatPx));
            break;
        case Thinking:
            static_cast<QTextEdit *>(m_body)->setStyleSheet(thinkingBodyStyle(thinkingPx));
            break;
        case Tool:
            static_cast<QTextEdit *>(m_body)->setStyleSheet(toolBodyStyle(toolPx));
            break;
    }
    // Re-measure so a size change reflows the body height.
    if (m_kind == Agent || m_kind == Thinking) resizeToFit();
}

// ─── ChatPane ──────────────────────────────────────────────────────────────

ChatPane::ChatPane(QWidget *parent) : QWidget(parent) {
    setupUI();
}

void ChatPane::setupUI() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName("conversationScroll");
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Track the vertical scrollbar to make auto-scroll sticky: if the user scrolls
    // UP away from the bottom, stop pinning; if they return to the bottom, resume.
    connect(m_scroll->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        auto *vbar = m_scroll->verticalScrollBar();
        const bool atBottom = (value >= vbar->maximum() - 4); // small tolerance
        m_followingBottom = atBottom;
    });
    m_scroll->setStyleSheet(R"(
        QScrollArea#conversationScroll { background: #0d1117; border: none; }
        QScrollArea#conversationScroll > QWidget > QScrollBar:vertical {
            background: #0d1117; width: 12px; margin: 0;
        }
        QScrollArea#conversationScroll > QWidget > QScrollBar::handle:vertical {
            background: #30363d; border-radius: 6px; min-height: 40px;
        }
        QScrollArea#conversationScroll > QWidget > QScrollBar::handle:vertical:hover {
            background: #484f58;
        }
        QScrollArea#conversationScroll > QWidget > QScrollBar::add-line:vertical,
        QScrollArea#conversationScroll > QWidget > QScrollBar::sub-line:vertical {
            height: 0;
        }
    )");

    m_host = new QWidget(this);
    m_layout = new QVBoxLayout(m_host);
    m_layout->setContentsMargins(24, 24, 24, 24);
    m_layout->setSpacing(14);

    m_emptyLabel = new QLabel(tr("Start a session and send a prompt.\n"
                                  "Type / for slash commands."), m_host);
    m_emptyLabel->setObjectName("emptyLabel");
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setStyleSheet("color: #8b949e;");
    m_layout->addWidget(m_emptyLabel, 0, Qt::AlignCenter);

    m_scroll->setWidget(m_host);
    mainLayout->addWidget(m_scroll, 1);

    // Composer.
    auto *composer = new QWidget(this);
    composer->setObjectName("composer");
    auto *composerLayout = new QHBoxLayout(composer);
    composerLayout->setContentsMargins(24, 0, 24, 24);
    composerLayout->setSpacing(10);

    m_input = new QLineEdit(composer);
    m_input->setObjectName("composerInput");
    m_input->setPlaceholderText(tr("Message…   ( / for commands, Enter to send )"));
    m_input->installEventFilter(this); // catch "/" → open command palette
    m_input->setStyleSheet(R"(
        QLineEdit#composerInput {
            background: #161b22; border: 1px solid #30363d;
            border-radius: 12px; padding: 14px 18px; color: #c9d1d9;
            font-family: 'Segoe UI', sans-serif;
        }
        QLineEdit#composerInput:focus { border-color: #58a6ff; }
    )");
    composerLayout->addWidget(m_input, 1);

    m_statusIndicator = new QLabel(composer);
    m_statusIndicator->setObjectName("thinkingIndicator");
    m_statusIndicator->setStyleSheet("color: #58a6ff; padding: 0 0.55em; font-weight: 600;");
    m_statusIndicator->setVisible(false);
    composerLayout->addWidget(m_statusIndicator, 0);

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

    connect(m_sendBtn, &QPushButton::clicked, this, [this]() {
        const QString text = m_input->text().trimmed();
        if (text.isEmpty() || m_busy) return;
        emit promptSubmitted(text);
        m_input->clear();
    });
    connect(m_input, &QLineEdit::returnPressed, this, [this]() {
        const QString text = m_input->text().trimmed();
        if (text.isEmpty() || m_busy) return;
        emit promptSubmitted(text);
        m_input->clear();
    });

    m_thinkTimer = new QTimer(this);
    m_thinkTimer->setInterval(90);
    connect(m_thinkTimer, &QTimer::timeout, this, [this]() {
        static const QStringList frames = {"◐", "◓", "◑", "◒"};
        m_statusIndicator->setText(tr("%1 working…").arg(frames.at(m_frame % frames.size())));
        ++m_frame;
    });
}

bool ChatPane::eventFilter(QObject *obj, QEvent *event) {
    if (obj == m_input && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Slash) {
            if (m_input->text().isEmpty()) {
                // First character of an empty composer → command palette.
                emit slashCommandRequested();
                return true; // don't insert the "/" character.
            }
            // A "/" at any non-first position → file-system picker (Hermes CLI).
            // The "/" itself is NOT inserted; the chosen path replaces it.
            // If a "." immediately precedes the cursor, the user typed "./" —
            // "current directory": the picker roots at cwd and inserts "./rel".
            const int pos = m_input->cursorPosition();
            const bool relative = pos > 0 && m_input->text().at(pos - 1) == '.';
            emit filePathRequested(pos, relative);
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

ChatBlock *ChatPane::activeBlock(ChatBlock::Kind kind) {
    switch (kind) {
        case ChatBlock::Thinking:
            if (!m_activeThinking) {
                m_activeThinking = new ChatBlock(ChatBlock::Thinking,
                                                 m_chatPx, m_thinkingPx, m_toolPx, m_host);
                m_layout->addWidget(m_activeThinking);
            }
            return m_activeThinking;
        case ChatBlock::Agent:
            if (!m_activeAgent) {
                m_activeAgent = new ChatBlock(ChatBlock::Agent,
                                              m_chatPx, m_thinkingPx, m_toolPx, m_host);
                m_layout->addWidget(m_activeAgent);
            }
            return m_activeAgent;
        default:
            return nullptr;
    }
}

void ChatPane::closeActiveBlocks() {
    if (m_activeThinking) { m_activeThinking->finalize(); m_activeThinking = nullptr; }
    if (m_activeAgent) { m_activeAgent->finalize(); m_activeAgent = nullptr; }
}

void ChatPane::insertPathAt(int column, const QString &path, bool replaceDot) {
    // The "/" that triggered the picker was never inserted (the eventFilter
    // swallowed it), so the composer text is exactly as it was before the key
    // press. Insert the chosen path at the recorded cursor column — the spot
    // where the user typed the "/" — then return focus to the composer. When
    // replaceDot is true the user typed "./": the dot before the "/" is also
    // replaced, so the whole "./" becomes the chosen path.
    const QString text = m_input->text();
    const int pos = qBound(0, column, text.length());
    // The "/" occupies `pos` (it was swallowed, so nothing sits there). For "./"
    // the dot sits at pos-1; drop it too so the entire "./" is replaced.
    const int start = (replaceDot && pos > 0) ? pos - 1 : pos;
    m_input->setText(text.left(start) + path + text.mid(pos));
    m_input->setCursorPosition(start + path.length());
    m_input->setFocus();
}

void ChatPane::appendUserMessage(const QString &text) {
    // A new user message closes any open streaming blocks from the prior turn.
    closeActiveBlocks();
    // A new turn always resumes following the bottom (the user just sent a prompt).
    m_followingBottom = true;
    auto *block = new ChatBlock(ChatBlock::User, m_chatPx, m_thinkingPx, m_toolPx, m_host);
    block->appendText(text);
    m_layout->addWidget(block);
    m_emptyLabel->setVisible(false);
    scrollToBottom();
}

void ChatPane::appendAgentChunk(const QString &text) {
    auto *block = activeBlock(ChatBlock::Agent);
    block->appendText(text);
    scrollToBottom();
}

void ChatPane::appendThoughtChunk(const QString &text) {
    auto *block = activeBlock(ChatBlock::Thinking);
    block->appendText(text);
    scrollToBottom();
}

void ChatPane::addToolCall(const QString &id, const QString &title, const QString &kind) {
    Q_UNUSED(kind);
    // A tool call closes the current thinking/agent blocks so ordering is preserved.
    closeActiveBlocks();
    auto *block = new ChatBlock(ChatBlock::Tool, m_chatPx, m_thinkingPx, m_toolPx, m_host);
    block->appendText(title);
    m_layout->addWidget(block);
    m_toolBlocks.insert(id, block);
    scrollToBottom();
}

void ChatPane::updateToolCall(const QString &id, const QString &status, const QString &output) {
    auto it = m_toolBlocks.find(id);
    if (it == m_toolBlocks.end()) return;
    it.value()->setToolStatus(status, output);
    scrollToBottom();
}

void ChatPane::finalizeTurn() {
    closeActiveBlocks();
    m_toolBlocks.clear();
    scrollToBottom();
}

void ChatPane::setBusy(bool busy) {
    m_busy = busy;
    m_sendBtn->setEnabled(!busy);
    m_statusIndicator->setVisible(busy);
    if (busy) {
        m_frame = 1;
        m_statusIndicator->setText(tr("◐ working…"));
        m_thinkTimer->start();
    } else {
        m_thinkTimer->stop();
        m_statusIndicator->clear();
    }
}

void ChatPane::clearConversation() {
    for (int i = m_layout->count() - 1; i >= 0; --i) {
        auto *item = m_layout->itemAt(i);
        if (!item) continue;
        QWidget *w = item->widget();
        if (w && w != m_emptyLabel) {
            m_layout->removeItem(item);
            w->deleteLater();
        }
    }
    m_activeThinking = nullptr;
    m_activeAgent = nullptr;
    m_toolBlocks.clear();
    m_emptyLabel->setVisible(true);
}

void ChatPane::applyFontSizes(int chatPx, int thinkingPx, int toolPx) {
    m_chatPx = chatPx;
    m_thinkingPx = thinkingPx;
    m_toolPx = toolPx;
    // Restyle every existing block so the change is visible immediately.
    for (int i = 0; i < m_layout->count(); ++i) {
        auto *item = m_layout->itemAt(i);
        if (!item) continue;
        if (auto *block = qobject_cast<ChatBlock *>(item->widget()))
            block->applyFontSizes(chatPx, thinkingPx, toolPx);
    }
}

void ChatPane::scrollToBottom() {
    // Sticky: only pin to the bottom while we're actually following. If the user
    // has scrolled up to read, don't yank them back down on every chunk.
    if (!m_followingBottom) return;
    // Defer to the next event-loop tick: the just-appended content hasn't been
    // laid out yet, so maximum() is still stale. Pinning synchronously would land
    // the viewport short of the true bottom (the "jumps" you see). After layout,
    // maximum() is current and the pin lands exactly on the bottom.
    QTimer::singleShot(0, this, [this]() {
        if (!m_followingBottom) return;
        if (auto *vbar = m_scroll->verticalScrollBar())
            vbar->setValue(vbar->maximum());
    });
}
