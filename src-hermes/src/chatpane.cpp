#include "chatpane.h"

#include "theme.h"
#include "uiutil.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QFontDatabase>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QScrollBar>
#include <QStyle>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextTable>
#include <QtMath>

namespace {

QString timestamp() {
    return QDateTime::currentDateTime().toString(QStringLiteral("h:mm AP"));
}

Theme &theme() { return Theme::instance(); }

QString monoFamily() {
    static const QString family = []() {
        const QStringList installed = QFontDatabase().families();
        for (const char *f : {"JetBrains Mono", "Fira Code", "Cascadia Code", "SF Mono", "Menlo",
                              "Consolas", "DejaVu Sans Mono", "Liberation Mono"}) {
            if (installed.contains(QString::fromLatin1(f), Qt::CaseInsensitive)) return QString::fromLatin1(f);
        }
        return QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
    }();
    return family;
}

QFont sizedFont(int px, bool mono = false) {
    QFont f = QApplication::font();
    if (mono) f.setFamily(monoFamily());
    f.setPixelSize(qMax(8, qRound(px * theme().scale())));
    return f;
}

void repolish(QWidget *w) {
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
}

QString toolIcon(const QString &kind) {
    static const QHash<QString, QString> icons = {
        {"execute", "square-terminal"}, {"read", "file-text"}, {"edit", "pencil"},
        {"delete", "trash-2"}, {"move", "folder"}, {"search", "search"}, {"fetch", "download"},
        {"think", "brain"}, {"switch_mode", "toggle-left"},
    };
    return icons.value(kind, QStringLiteral("wrench"));
}

QString statusLabel(const QString &status) {
    if (status == "pending") return QObject::tr("Pending");
    if (status == "in_progress") return QObject::tr("Running");
    if (status == "completed") return QObject::tr("Done");
    if (status == "failed") return QObject::tr("Failed");
    if (status.isEmpty()) return QString();
    QString s = status;
    s.replace('_', ' ');
    s[0] = s.at(0).toUpper();
    return s;
}

// A QTextEdit used as a read-only, auto-height text block.
QTextEdit *makeBody(const QString &objectName, QWidget *parent) {
    auto *body = new QTextEdit(parent);
    body->setObjectName(objectName);
    body->setReadOnly(true);
    body->setFrameShape(QFrame::NoFrame);
    body->setLineWrapMode(QTextEdit::WidgetWidth);
    body->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    body->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    body->setTextInteractionFlags(Qt::TextBrowserInteraction);
    body->setContextMenuPolicy(Qt::DefaultContextMenu);
    body->document()->setDocumentMargin(0);
    body->viewport()->setAutoFillBackground(false);
    body->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return body;
}

} // namespace
static QString preserveLineBreaks(const QString &md);

// ─── ChatBlock ─────────────────────────────────────────────────────────────

ChatBlock::ChatBlock(Kind kind, int chatPx, int thinkingPx, int toolPx, QWidget *parent)
    : QWidget(parent), m_kind(kind), m_chatPx(chatPx), m_thinkingPx(thinkingPx), m_toolPx(toolPx) {
    m_started.start();
    build();
    restyle();
}

ChatBlock::~ChatBlock() = default;

ChatBlock::Kind ChatBlock::kind() const { return m_kind; }
QString ChatBlock::toolId() const { return m_toolId; }

void ChatBlock::build() {
    auto &t = theme();
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(t.px(6));

    switch (m_kind) {
        case User: {
            auto *row = new QHBoxLayout();
            row->setContentsMargins(0, 0, 0, 0);
            row->addStretch(1);
            auto *col = new QVBoxLayout();
            col->setSpacing(t.px(4));
            m_bubble = new QFrame(this);
            m_bubble->setObjectName("userBubble");
            auto *bl = new QHBoxLayout(m_bubble);
            bl->setContentsMargins(t.px(14), t.px(9), t.px(14), t.px(9));
            bl->setSpacing(t.px(8));
            m_icon = new QLabel(m_bubble);
            m_icon->setVisible(false);
            bl->addWidget(m_icon, 0, Qt::AlignTop);
            auto *label = new QLabel(m_bubble);
            label->setObjectName("userBubbleText");
            label->setWordWrap(true);
            label->setTextFormat(Qt::PlainText);
            label->setTextInteractionFlags(Qt::TextSelectableByMouse);
            label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            bl->addWidget(label, 1);
            m_body = label;
            col->addWidget(m_bubble, 0, Qt::AlignRight);
            m_time = new QLabel(timestamp(), this);
            m_time->setObjectName("blockTime");
            col->addWidget(m_time, 0, Qt::AlignRight);
            row->addLayout(col);
            outer->addLayout(row);
            break;
        }

        case Agent: {
            m_header = new QWidget(this);
            auto *header = new QHBoxLayout(m_header);
            header->setContentsMargins(0, 0, 0, 0);
            header->setSpacing(t.px(8));
            m_avatar = new QLabel(m_header);
            m_avatar->setFixedSize(t.px(22), t.px(22));
            header->addWidget(m_avatar);
            auto *author = new QLabel(tr("Hermes"), m_header);
            author->setObjectName("blockAuthor");
            header->addWidget(author);
            m_time = new QLabel(timestamp(), m_header);
            m_time->setObjectName("blockTime");
            header->addWidget(m_time);
            header->addStretch(1);
            m_copyBtn = ui::iconButton("copy", tr("Copy response"), m_header, "blockAction", 15);
            m_copyBtn->setVisible(false);
            header->addWidget(m_copyBtn);
            outer->addWidget(m_header);
            connect(m_copyBtn, &QToolButton::clicked, this, [this]() {
                QApplication::clipboard()->setText(m_accumulated);
                m_copyBtn->setIcon(theme().icon("check", theme().tokens().success));
                m_copyBtn->setToolTip(tr("Copied"));
                QTimer::singleShot(1400, m_copyBtn, [this]() {
                    m_copyBtn->setIcon(theme().icon("copy"));
                    m_copyBtn->setToolTip(tr("Copy response"));
                });
            });

            auto *bodyRow = new QHBoxLayout();
            bodyRow->setContentsMargins(t.px(30), 0, 0, 0);
            auto *body = makeBody("agentBody", this);
            body->setAccessibleName(tr("Hermes response"));
            bodyRow->addWidget(body);
            outer->addLayout(bodyRow);
            m_body = body;
            break;
        }

        case Thinking: {
            auto *headerRow = new QHBoxLayout();
            headerRow->setContentsMargins(t.px(30), 0, 0, 0);
            m_toggleBtn = new QToolButton(this);
            m_toggleBtn->setObjectName("blockToggle");
            m_toggleBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            m_toggleBtn->setCursor(Qt::PointingHandCursor);
            m_toggleBtn->setText(tr("Thinking…"));
            headerRow->addWidget(m_toggleBtn);
            headerRow->addStretch(1);
            outer->addLayout(headerRow);

            auto *cardRow = new QHBoxLayout();
            cardRow->setContentsMargins(t.px(36), 0, 0, 0);
            m_card = new QFrame(this);
            m_card->setObjectName("thinkingCard");
            auto *cl = new QVBoxLayout(m_card);
            cl->setContentsMargins(t.px(12), t.px(2), 0, t.px(2));
            auto *body = makeBody("thinkingBody", m_card);
            body->setAccessibleName(tr("Hermes reasoning"));
            cl->addWidget(body);
            cardRow->addWidget(m_card);
            outer->addLayout(cardRow);
            m_body = body;
            connect(m_toggleBtn, &QToolButton::clicked, this, [this]() { setExpanded(m_collapsed); });
            updateToggle();
            break;
        }

        case Tool: {
            auto *cardRow = new QHBoxLayout();
            cardRow->setContentsMargins(t.px(30), 0, 0, 0);
            m_card = new QFrame(this);
            m_card->setObjectName("toolCard");
            auto *cl = new QVBoxLayout(m_card);
            cl->setContentsMargins(0, 0, 0, 0);
            cl->setSpacing(0);
            m_header = new QWidget(m_card);
            m_header->setCursor(Qt::PointingHandCursor);
            m_header->installEventFilter(this);
            auto *hl = new QHBoxLayout(m_header);
            hl->setContentsMargins(t.px(10), t.px(7), t.px(6), t.px(7));
            hl->setSpacing(t.px(8));
            m_icon = new QLabel(m_header);
            hl->addWidget(m_icon);
            m_toolTitle = new ui::ElidedLabel(m_header, Qt::ElideMiddle);
            m_toolTitle->setObjectName("toolTitle");
            hl->addWidget(m_toolTitle, 1);
            m_badge = new QLabel(m_header);
            m_badge->setObjectName("statusBadge");
            m_badge->setVisible(false);
            hl->addWidget(m_badge);
            m_toggleBtn = ui::iconButton("chevron-right", tr("Show output"), m_header, "blockAction", 14);
            hl->addWidget(m_toggleBtn);
            cl->addWidget(m_header);
            auto *body = new QTextEdit(m_card);
            body->setObjectName("toolBody");
            body->setReadOnly(true);
            body->setFrameShape(QFrame::NoFrame);
            body->setLineWrapMode(QTextEdit::WidgetWidth);
            body->setAccessibleName(tr("Tool output"));
            body->setMaximumHeight(t.px(280));
            body->setVisible(false);
            cl->addWidget(body);
            cardRow->addWidget(m_card);
            outer->addLayout(cardRow);
            m_body = body;
            m_collapsed = true;
            connect(m_toggleBtn, &QToolButton::clicked, this, [this]() { setExpanded(m_collapsed); });
            break;
        }

        case Notice: {
            auto *row = new QHBoxLayout();
            row->setContentsMargins(0, t.px(2), 0, t.px(2));
            row->setSpacing(t.px(6));
            row->addStretch(1);
            m_icon = new QLabel(this);
            row->addWidget(m_icon);
            auto *label = new QLabel(this);
            label->setObjectName("noticeText");
            label->setWordWrap(true);
            label->setTextFormat(Qt::PlainText);
            label->setTextInteractionFlags(Qt::TextSelectableByMouse);
            row->addWidget(label, 0);
            row->addStretch(1);
            outer->addLayout(row);
            m_body = label;
            break;
        }
    }
    applyFontSizes(m_chatPx, m_thinkingPx, m_toolPx);
}

bool ChatBlock::eventFilter(QObject *obj, QEvent *event) {
    if (obj == m_header && m_kind == Tool && event->type() == QEvent::MouseButtonRelease) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton) setExpanded(m_collapsed);
        return true;
    }
    return QWidget::eventFilter(obj, event);
}

void ChatBlock::setExpanded(bool expanded) {
    if (m_kind != Thinking && m_kind != Tool) return;
    m_collapsed = !expanded;
    if (m_kind == Thinking) m_card->setVisible(expanded);
    else m_body->setVisible(expanded);
    updateToggle();
    if (expanded && m_kind == Thinking) fitBody(false);
}

void ChatBlock::updateToggle() {
    auto &t = theme();
    if (!m_toggleBtn) return;
    if (m_kind == Thinking) {
        m_toggleBtn->setIcon(t.icon(m_collapsed ? "chevron-right" : "chevron-down"));
        m_toggleBtn->setIconSize(QSize(t.px(14), t.px(14)));
        QString text;
        if (!m_finalized) text = tr("Thinking…");
        else if (m_thoughtSecs >= 1) text = tr("Thought for %1 s").arg(m_thoughtSecs);
        else text = tr("Thoughts");
        m_toggleBtn->setText(text);
        ui::describe(m_toggleBtn, text, m_collapsed ? tr("Show reasoning") : tr("Hide reasoning"));
    } else if (m_kind == Tool) {
        m_toggleBtn->setIcon(t.icon(m_collapsed ? "chevron-right" : "chevron-down"));
        ui::describe(m_toggleBtn, m_collapsed ? tr("Show output") : tr("Hide output"));
    }
}

void ChatBlock::restyle() {
    auto &t = theme();
    const auto &tk = t.tokens();
    if (m_avatar) m_avatar->setPixmap(QIcon(QStringLiteral(":/logo.svg")).pixmap(m_avatar->size()));
    if (m_copyBtn) m_copyBtn->setIcon(t.icon("copy"));
    if (m_kind == User && m_isCommand && m_icon) {
        m_icon->setPixmap(t.pixmap("square-terminal", t.px(15), tk.accent));
    }
    if (m_kind == Tool && m_icon) m_icon->setPixmap(t.pixmap(toolIcon(m_toolKind), t.px(15), tk.textMuted));
    if (m_kind == Notice && m_icon) {
        QString icon = "info";
        QColor c = tk.textMuted;
        if (m_level == Success) { icon = "circle-check"; c = tk.success; }
        else if (m_level == Warning) { icon = "triangle-alert"; c = tk.warning; }
        else if (m_level == Error) { icon = "circle-alert"; c = tk.danger; }
        m_icon->setPixmap(t.pixmap(icon, t.px(14), c));
    }
    updateToggle();
    for (QWidget *w : findChildren<QWidget *>()) {
        if (w->property("state").isValid() || w->property("level").isValid()) repolish(w);
    }
    if (m_finalized && (m_kind == Agent || m_kind == Thinking)) {
        static_cast<QTextEdit *>(m_body)->document()->setMarkdown(preserveLineBreaks(m_accumulated),
                                                                  QTextDocument::MarkdownDialectGitHub);
        styleMarkdown();
        fitBody(false);
    }
}

void ChatBlock::setLevel(Level level) {
    m_level = level;
    if (m_body) {
        static const char *names[] = {"info", "success", "warning", "error"};
        m_body->setProperty("level", QString::fromLatin1(names[level]));
        repolish(m_body);
    }
    restyle();
}

void ChatBlock::appendText(const QString &text) {
    m_accumulated += text;
    if (m_kind == User) {
        auto *label = static_cast<QLabel *>(m_body);
        const QString trimmed = m_accumulated.trimmed();
        const bool command = trimmed.startsWith('/') && !trimmed.contains('\n');
        if (command != m_isCommand) {
            m_isCommand = command;
            m_bubble->setObjectName(command ? "commandPill" : "userBubble");
            label->setObjectName(command ? "commandPillText" : "userBubbleText");
            m_icon->setVisible(command);
            repolish(m_bubble);
            repolish(label);
            restyle();
            applyFontSizes(m_chatPx, m_thinkingPx, m_toolPx);
        }
        label->setText(m_accumulated);
        label->setAccessibleName(command ? tr("Command %1").arg(trimmed) : tr("Your message"));
        updateBubbleWidth();
    } else if (m_kind == Notice) {
        static_cast<QLabel *>(m_body)->setText(m_accumulated);
    } else if (m_kind == Tool) {
        // Tool blocks don't stream text; appendText sets the title.
        m_toolTitle->setFullText(m_accumulated.trimmed());
    } else {
        auto *body = static_cast<QTextEdit *>(m_body);
        // Append the delta at the end rather than replacing the whole document:
        // cheaper, and it doesn't disturb the layout of text already on screen.
        QTextCursor c(body->document());
        c.movePosition(QTextCursor::End);
        c.insertText(text);
        resizeToFit(); // grow to fit while streaming so it isn't clipped
    }
}

void ChatBlock::fitBody(bool growOnly) {
    if (m_kind != Thinking && m_kind != Agent) return;
    auto *body = static_cast<QTextEdit *>(m_body);
    const int width = body->viewport()->width() > 0 ? body->viewport()->width() : body->width();
    body->document()->setTextWidth(qMax(50, width));
    const int h = qCeil(body->document()->size().height()) + body->frameWidth() * 2 + 2;
    if (growOnly) {
        // While streaming, the fixed height only ever GROWS. A fresh measurement
        // can dip below the previous one (re-layout, partial lines), and letting
        // it shrink is what makes the panel jump up and down.
        m_streamHeight = qMax(m_streamHeight, qMax(h, theme().px(20)));
        body->setFixedHeight(m_streamHeight);
    } else {
        m_streamHeight = 0;
        body->setFixedHeight(qMax(h, theme().px(20)));
    }
}

void ChatBlock::resizeToFit() { fitBody(true); }

// A word-wrapped QLabel picks a narrow "pleasant" width on its own; give the
// bubble its natural width (up to 78% of the column) so short and medium
// messages stay on one line and long ones wrap at a readable measure.
void ChatBlock::updateBubbleWidth() {
    if (m_kind != User || !m_bubble || width() <= 0) return;
    auto *label = static_cast<QLabel *>(m_body);
    const int maxBubble = qMax(theme().px(200), int(width() * 0.78));
    m_bubble->setMaximumWidth(maxBubble);
    const QMargins bm = m_bubble->layout() ? m_bubble->layout()->contentsMargins() : QMargins();
    int chrome = bm.left() + bm.right();
    if (m_icon && m_isCommand) chrome += m_icon->sizeHint().width() + (m_bubble->layout() ? m_bubble->layout()->spacing() : 0);
    const QFontMetrics fm(label->font());
    int natural = 0;
    for (const QString &line : m_accumulated.split(QLatin1Char('\n'))) natural = qMax(natural, fm.horizontalAdvance(line));
    label->setMinimumWidth(qMin(natural + 4, qMax(theme().px(60), maxBubble - chrome)));
}

void ChatBlock::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    if (m_kind == User && m_bubble) updateBubbleWidth();
    if (m_kind == Agent || m_kind == Thinking) {
        // A width change reflows the text: re-measure (a reflow may shrink).
        QTimer::singleShot(0, this, [this]() { fitBody(false); });
    }
}

void ChatBlock::styleMarkdown() {
    auto *body = static_cast<QTextEdit *>(m_body);
    QTextDocument *doc = body->document();
    const auto &tk = theme().tokens();
    const QString mono = monoFamily();
    QTextCursor edit(doc);
    edit.beginEditBlock();
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        QTextBlockFormat bf = b.blockFormat();
        const bool code = bf.hasProperty(QTextFormat::BlockCodeFence) || bf.nonBreakableLines();
        QTextCursor bc(b);
        const bool inTable = bc.currentTable() != nullptr;
        if (inTable) {
            // No paragraph margins inside cells (they misalign header rows).
            bf.setTopMargin(0);
            bf.setBottomMargin(0);
            bc.setBlockFormat(bf);
        } else if (code) {
            bf.setBackground(tk.code);
            bf.setLeftMargin(theme().px(10));
            bf.setRightMargin(theme().px(10));
            bc.setBlockFormat(bf);
        } else if (bf.headingLevel() == 0) {
            bf.setLineHeight(135, QTextBlockFormat::ProportionalHeight);
            if (bf.bottomMargin() < theme().px(6)) bf.setBottomMargin(theme().px(6));
            bc.setBlockFormat(bf);
        }
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            if (!f.isValid()) continue;
            QTextCharFormat cf = f.charFormat();
            QTextCursor fc(doc);
            fc.setPosition(f.position());
            fc.setPosition(f.position() + f.length(), QTextCursor::KeepAnchor);
            if (cf.fontFixedPitch() || code) {
                cf.setFontFamily(mono);
                cf.clearProperty(QTextFormat::FontPointSize);
                if (!code) cf.setBackground(tk.code);
                fc.setCharFormat(cf);
            } else if (cf.isAnchor()) {
                cf.setForeground(tk.accent);
                fc.setCharFormat(cf);
            }
        }
    }
    for (QTextFrame *frame : doc->rootFrame()->childFrames()) {
        if (auto *table = qobject_cast<QTextTable *>(frame)) {
            QTextTableFormat tf = table->format();
            tf.setBorder(1);
            tf.setBorderBrush(tk.borderStrong);
            tf.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
            tf.setCellSpacing(0);
            tf.setCellPadding(theme().px(6));
            tf.setBorderCollapse(true);
            table->setFormat(tf);
            for (int r = 0; r < table->rows(); ++r) {
                for (int c = 0; c < table->columns(); ++c) {
                    QTextTableCell cell = table->cellAt(r, c);
                    QTextCharFormat cf = cell.format();
                    cf.setVerticalAlignment(QTextCharFormat::AlignMiddle);
                    if (r == 0) cf.setBackground(tk.code);
                    cell.setFormat(cf);
                }
            }
        }
    }
    edit.endEditBlock();
}

// Hermes's own command replies (and many model answers) use single newlines
// as line breaks; CommonMark would join them into one paragraph. Keep them,
// except inside code fences and tables.
static QString preserveLineBreaks(const QString &md) {
    const QStringList lines = md.split(QLatin1Char('\n'));
    QStringList out;
    bool fence = false;
    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines[i];
        const QString t = line.trimmed();
        if (t.startsWith(QLatin1String("```")) || t.startsWith(QLatin1String("~~~"))) { fence = !fence; out << line; continue; }
        const QString next = i + 1 < lines.size() ? lines[i + 1].trimmed() : QString();
        if (!fence && !t.isEmpty() && !next.isEmpty() && !t.startsWith(QLatin1Char('|')) && !next.startsWith(QLatin1Char('|'))
            && !t.startsWith(QLatin1Char('#')) && !next.startsWith(QLatin1String("```")) && !line.endsWith(QLatin1String("  "))
            && !line.endsWith(QLatin1Char('\\')))
            line += QStringLiteral("  ");
        out << line;
    }
    return out.join(QLatin1Char('\n'));
}

void ChatBlock::finalize() {
    if (m_finalized) return;
    m_finalized = true;
    if (m_kind == Agent || m_kind == Thinking) {
        if (m_kind == Thinking) m_thoughtSecs = int(m_started.elapsed() / 1000);
        auto *body = static_cast<QTextEdit *>(m_body);
        body->document()->setMarkdown(preserveLineBreaks(m_accumulated), QTextDocument::MarkdownDialectGitHub);
        styleMarkdown();
        // Size the body to its content. This is a one-time settle at turn end, so
        // unlike the grow-only streaming path it may shrink to the true height.
        fitBody(false);
        if (m_copyBtn) m_copyBtn->setVisible(!m_accumulated.trimmed().isEmpty());
        updateToggle();
    }
    // NOTE: thinking is intentionally NOT collapsed here — it persists until the
    // next round so the user can read the reasoning. The toggle still works.
}

void ChatBlock::setToolInfo(const QString &title, const QString &kind) {
    m_toolKind = kind;
    m_accumulated = title;
    if (m_toolTitle) m_toolTitle->setFullText(title.trimmed().isEmpty() ? tr("Tool call") : title.trimmed());
    setAccessibleName(tr("Tool call: %1").arg(title));
    restyle();
}

void ChatBlock::setToolStatus(const QString &status, const QString &output) {
    if (m_kind != Tool) return;
    m_toolStatus = status;
    auto *body = static_cast<QTextEdit *>(m_body);
    if (!output.isEmpty()) body->setPlainText(output);
    const QString label = statusLabel(status);
    m_badge->setText(label);
    m_badge->setVisible(!label.isEmpty());
    m_badge->setProperty("state", status);
    m_card->setProperty("state", status);
    repolish(m_badge);
    repolish(m_card);
    // Failures open up so the error is visible; successes stay compact.
    if (status == "failed" && !output.isEmpty() && m_collapsed) setExpanded(true);
}

QString ChatBlock::plainText() const { return m_accumulated; }

void ChatBlock::applyFontSizes(int chatPx, int thinkingPx, int toolPx) {
    m_chatPx = chatPx;
    m_thinkingPx = thinkingPx;
    m_toolPx = toolPx;
    switch (m_kind) {
        case User:
            m_body->setFont(sizedFont(m_isCommand ? qMax(10, chatPx - 2) : chatPx, m_isCommand));
            break;
        case Agent:
            m_body->setFont(sizedFont(chatPx));
            static_cast<QTextEdit *>(m_body)->document()->setDefaultFont(sizedFont(chatPx));
            break;
        case Thinking:
            m_body->setFont(sizedFont(thinkingPx));
            static_cast<QTextEdit *>(m_body)->document()->setDefaultFont(sizedFont(thinkingPx));
            break;
        case Tool:
            m_body->setFont(sizedFont(toolPx, true));
            if (m_toolTitle) m_toolTitle->setFont(sizedFont(toolPx, true));
            break;
        case Notice:
            break;
    }
    // Re-measure so a size change reflows the body height.
    if (m_kind == Agent || m_kind == Thinking) fitBody(false);
}

// ─── ChatPane ──────────────────────────────────────────────────────────────

ChatPane::ChatPane(QWidget *parent) : QWidget(parent) {
    setObjectName("chatPane");
    setupUI();
    connect(&Theme::instance(), &Theme::changed, this, &ChatPane::restyle);
}

void ChatPane::setupUI() {
    auto &t = theme();
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ── Banner ──
    auto *bannerWrap = new QWidget(this);
    auto *bwl = new QHBoxLayout(bannerWrap);
    bwl->setContentsMargins(t.px(16), t.px(12), t.px(16), 0);
    m_banner = new QFrame(bannerWrap);
    m_banner->setObjectName("banner");
    m_banner->setAccessibleName(tr("Notification"));
    auto *bl = new QHBoxLayout(m_banner);
    bl->setContentsMargins(t.px(14), t.px(10), t.px(10), t.px(10));
    bl->setSpacing(t.px(10));
    m_bannerIcon = new QLabel(m_banner);
    bl->addWidget(m_bannerIcon, 0, Qt::AlignTop);
    auto *btext = new QVBoxLayout();
    btext->setSpacing(t.px(2));
    m_bannerTitle = new QLabel(m_banner);
    m_bannerTitle->setStyleSheet("font-weight: 600; background: transparent;");
    m_bannerText = new QLabel(m_banner);
    m_bannerText->setWordWrap(true);
    m_bannerText->setProperty("role", "muted");
    m_bannerText->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_bannerText->setStyleSheet("background: transparent;");
    btext->addWidget(m_bannerTitle);
    btext->addWidget(m_bannerText);
    bl->addLayout(btext, 1);
    m_bannerActions = new QWidget(m_banner);
    auto *bal = new QHBoxLayout(m_bannerActions);
    bal->setContentsMargins(0, 0, 0, 0);
    bal->setSpacing(t.px(6));
    bl->addWidget(m_bannerActions, 0, Qt::AlignVCenter);
    bwl->addWidget(m_banner);
    bannerWrap->setVisible(false);
    mainLayout->addWidget(bannerWrap);

    // ── Conversation / empty state ──
    m_stack = new QStackedWidget(this);

    m_stackEmptyPage = new QWidget(m_stack);
    m_stackEmptyPage->setObjectName("emptyState");
    auto *el = new QVBoxLayout(m_stackEmptyPage);
    el->setContentsMargins(t.px(24), t.px(24), t.px(24), t.px(24));
    el->addStretch(3);
    m_empty = new QWidget(m_stackEmptyPage);
    m_empty->setObjectName("emptyState");
    m_empty->setMaximumWidth(t.px(620));
    auto *ec = new QVBoxLayout(m_empty);
    ec->setSpacing(t.px(10));
    ec->setContentsMargins(0, 0, 0, 0);
    auto *logoRow = new QHBoxLayout();
    m_emptyLogo = new QLabel(m_empty);
    m_emptyLogo->setFixedSize(t.px(56), t.px(56));
    m_emptyLogo->setAccessibleName(tr("Hermes logo"));
    m_emptySpinner = new ui::Spinner(m_empty, 22);
    m_emptySpinner->setVisible(false);
    logoRow->addStretch();
    logoRow->addWidget(m_emptyLogo);
    logoRow->addWidget(m_emptySpinner);
    logoRow->addStretch();
    ec->addLayout(logoRow);
    ec->addSpacing(t.px(4));
    m_emptyTitle = new QLabel(m_empty);
    m_emptyTitle->setObjectName("emptyTitle");
    m_emptyTitle->setAlignment(Qt::AlignCenter);
    m_emptyTitle->setWordWrap(true);
    ec->addWidget(m_emptyTitle);
    m_emptySubtitle = new QLabel(m_empty);
    m_emptySubtitle->setObjectName("emptySubtitle");
    m_emptySubtitle->setAlignment(Qt::AlignCenter);
    m_emptySubtitle->setWordWrap(true);
    ec->addWidget(m_emptySubtitle);
    ec->addSpacing(t.px(14));
    m_emptyActions = new QWidget(m_empty);
    ec->addWidget(m_emptyActions);
    auto *emptyRow = new QHBoxLayout();
    emptyRow->addStretch(1);
    emptyRow->addWidget(m_empty, 4);
    emptyRow->addStretch(1);
    el->addLayout(emptyRow);
    el->addStretch(4);
    m_stack->addWidget(m_stackEmptyPage);

    m_scroll = new QScrollArea(m_stack);
    m_scroll->setObjectName("conversationScroll");
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setAccessibleName(tr("Conversation"));
    // Track the vertical scrollbar to make auto-scroll sticky: if the user scrolls
    // UP away from the bottom, stop pinning; if they return to the bottom, resume.
    connect(m_scroll->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        auto *vbar = m_scroll->verticalScrollBar();
        m_followingBottom = (value >= vbar->maximum() - 4); // small tolerance
    });
    connect(m_scroll->verticalScrollBar(), &QScrollBar::rangeChanged, this, [this](int, int max) {
        if (m_followingBottom) m_scroll->verticalScrollBar()->setValue(max);
    });

    m_host = new QWidget(m_scroll);
    m_host->setObjectName("conversationHost");
    auto *hostRow = new QHBoxLayout(m_host);
    hostRow->setContentsMargins(t.px(24), t.px(20), t.px(24), t.px(20));
    hostRow->addStretch(0);
    m_column = new QWidget(m_host);
    m_column->setObjectName("conversationColumn");
    m_column->setMaximumWidth(t.px(860));
    m_column->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    hostRow->addWidget(m_column, 1);
    hostRow->addStretch(0);
    m_layout = new QVBoxLayout(m_column);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(t.px(18));
    m_layout->addStretch(1);
    m_scroll->setWidget(m_host);
    m_stack->addWidget(m_scroll);
    mainLayout->addWidget(m_stack, 1);

    // ── Composer ──
    auto *composerArea = new QWidget(this);
    composerArea->setObjectName("composerArea");
    auto *ca = new QHBoxLayout(composerArea);
    ca->setContentsMargins(t.px(24), t.px(4), t.px(24), t.px(18));
    m_composerCard = new QFrame(composerArea);
    m_composerCard->setObjectName("composerCard");
    m_composerCard->setMaximumWidth(t.px(860));
    auto *cl = new QVBoxLayout(m_composerCard);
    cl->setContentsMargins(t.px(14), t.px(12), t.px(10), t.px(10));
    cl->setSpacing(t.px(8));

    m_input = new QPlainTextEdit(m_composerCard);
    m_input->setObjectName("composerInput");
    m_input->setFrameShape(QFrame::NoFrame);
    m_input->setTabChangesFocus(true);
    m_input->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_input->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_input->document()->setDocumentMargin(0);
    m_input->installEventFilter(this); // "/" → palette or file picker; Enter → send
    m_placeholder = tr("Message Hermes…");
    m_inputPlaceholder = new QLabel(m_placeholder, m_input->viewport());
    m_inputPlaceholder->setObjectName("composerPlaceholder");
    m_inputPlaceholder->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_inputPlaceholder->move(0, 0);
    connect(m_input, &QPlainTextEdit::textChanged, this, &ChatPane::updatePlaceholder);
    m_input->viewport()->installEventFilter(this);
    m_input->setAccessibleName(tr("Message"));
    m_input->setAccessibleDescription(tr("Type a message. Enter sends, Shift+Enter adds a line, / opens commands."));
    cl->addWidget(m_input);

    auto *row = new QHBoxLayout();
    row->setSpacing(t.px(6));
    m_commandsBtn = ui::iconButton("command", ui::withShortcut(tr("Commands"), QKeySequence(Qt::CTRL | Qt::Key_K)),
                                   m_composerCard, "composerTool", 17);
    row->addWidget(m_commandsBtn);
    m_spinner = new ui::Spinner(m_composerCard, 14);
    m_spinner->setVisible(false);
    row->addWidget(m_spinner);
    m_busyLabel = new QLabel(m_composerCard);
    m_busyLabel->setObjectName("busyLabel");
    m_busyLabel->setVisible(false);
    row->addWidget(m_busyLabel);
    m_hint = new QLabel(tr("Enter to send · Shift+Enter for a new line · / for commands"), m_composerCard);
    m_hint->setObjectName("composerHint");
    m_hint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    row->addWidget(m_hint, 1);
    m_sendBtn = new QToolButton(m_composerCard);
    m_sendBtn->setObjectName("sendButton");
    m_sendBtn->setCursor(Qt::PointingHandCursor);
    m_sendBtn->setFixedSize(t.px(32), t.px(32));
    m_sendBtn->setIconSize(QSize(t.px(18), t.px(18)));
    row->addWidget(m_sendBtn);
    cl->addLayout(row);
    ca->addStretch(0);
    ca->addWidget(m_composerCard, 1);
    ca->addStretch(0);
    mainLayout->addWidget(composerArea);

    connect(m_sendBtn, &QToolButton::clicked, this, [this]() {
        if (m_busy) emit stopRequested();
        else submit();
    });
    connect(m_commandsBtn, &QToolButton::clicked, this, &ChatPane::commandsButtonClicked);
    connect(m_input, &QPlainTextEdit::textChanged, this, [this]() {
        updateComposerHeight();
        updateSendButton();
    });

    m_busyTimer = new QTimer(this);
    m_busyTimer->setInterval(500);
    connect(m_busyTimer, &QTimer::timeout, this, [this]() {
        const qint64 s = m_busyClock.elapsed() / 1000;
        m_busyLabel->setText(tr("%1 · %2:%3 · Esc to stop")
                                 .arg(m_busyText)
                                 .arg(s / 60)
                                 .arg(s % 60, 2, 10, QLatin1Char('0')));
    });

    updateComposerHeight();
    updateSendButton();
    restyle();
    updateEmptyVisibility();
}

void ChatPane::restyle() {
    auto &t = theme();
    const auto &tk = t.tokens();
    m_emptyLogo->setPixmap(QIcon(QStringLiteral(":/logo.svg")).pixmap(m_emptyLogo->size()));
    m_commandsBtn->setIcon(t.icon("command"));
    updateSendButton();
    if (!m_bannerLevel.isEmpty()) {
        const QString icon = m_bannerLevel == "error" ? "wifi-off"
                           : m_bannerLevel == "warning" ? "triangle-alert" : "info";
        const QColor c = m_bannerLevel == "error" ? tk.danger
                       : m_bannerLevel == "warning" ? tk.warning : tk.accent;
        m_bannerIcon->setPixmap(t.pixmap(icon, t.px(18), c));
        repolish(m_banner);
    }
    // Rebuild empty-state buttons (icons are theme-tinted).
    setEmptyState(m_emptyTitle->text(), m_emptySubtitle->text(), m_quickActions, m_emptySpinner->isVisible());
    for (ChatBlock *b : blocks()) b->restyle();
    repolish(m_composerCard);
}

void ChatPane::setEmptyState(const QString &title, const QString &subtitle,
                             const QList<QuickAction> &actions, bool spinner) {
    auto &t = theme();
    m_quickActions = actions;
    m_emptyTitle->setText(title);
    m_emptySubtitle->setText(subtitle);
    m_emptySubtitle->setVisible(!subtitle.isEmpty());
    m_emptyLogo->setVisible(!spinner);
    m_emptySpinner->setVisible(spinner);
    m_emptySpinner->setRunning(spinner);

    delete m_emptyActions->layout();
    for (QWidget *w : m_emptyActions->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
        w->deleteLater();
    }
    auto *grid = new QGridLayout(m_emptyActions);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(t.px(10));
    grid->setVerticalSpacing(t.px(10));
    const int cols = actions.size() > 2 ? 2 : qMax(1, actions.size());
    for (int i = 0; i < actions.size(); ++i) {
        const auto &a = actions[i];
        auto *b = new QPushButton(m_emptyActions);
        b->setObjectName("emptyAction");
        b->setCursor(Qt::PointingHandCursor);
        // Icon · title (strong) over description (muted), as child labels so
        // the two lines get their own typography.
        auto *bl = new QHBoxLayout(b);
        bl->setContentsMargins(t.px(14), t.px(10), t.px(14), t.px(10));
        bl->setSpacing(t.px(12));
        auto *ic = new QLabel(b);
        ic->setPixmap(t.pixmap(a.icon, t.px(18), a.enabled ? t.tokens().accent : t.tokens().textFaint));
        ic->setAttribute(Qt::WA_TransparentForMouseEvents);
        ic->setProperty("themeIcon", a.icon);
        bl->addWidget(ic, 0, Qt::AlignVCenter);
        auto *col = new QVBoxLayout();
        col->setSpacing(t.px(1));
        auto *ttl = new QLabel(a.label, b);
        ttl->setObjectName("emptyActionTitle");
        ttl->setAttribute(Qt::WA_TransparentForMouseEvents);
        col->addWidget(ttl);
        if (!a.description.isEmpty()) {
            auto *ds = new QLabel(a.description, b);
            ds->setObjectName("emptyActionText");
            ds->setAttribute(Qt::WA_TransparentForMouseEvents);
            col->addWidget(ds);
        }
        bl->addLayout(col, 1);
        b->setEnabled(a.enabled);
        for (QLabel *lab : b->findChildren<QLabel *>()) {
            lab->ensurePolished();
            if (!a.enabled) lab->setStyleSheet(QStringLiteral("color: %1;").arg(t.tokens().textFaint.name()));
        }
        b->setMinimumHeight(qMax(t.px(a.description.isEmpty() ? 44 : 58), bl->sizeHint().height()));
        b->setMinimumWidth(t.px(250));
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setProperty("actionId", a.id);
        ui::describe(b, a.label, ui::withShortcut(a.description.isEmpty() ? a.label : a.description,
                                                  QKeySequence(a.shortcut)));
        connect(b, &QPushButton::clicked, this, [this, id = a.id]() { emit quickActionTriggered(id); });
        grid->addWidget(b, i / cols, i % cols);
    }
}

void ChatPane::showBanner(const QString &level, const QString &title, const QString &message,
                          const QList<BannerAction> &actions) {
    auto &t = theme();
    m_bannerLevel = level;
    m_banner->setProperty("level", level);
    m_bannerTitle->setText(title);
    m_bannerText->setText(message);
    m_bannerText->setVisible(!message.isEmpty());
    m_banner->setAccessibleDescription(title + QStringLiteral(". ") + message);
    for (QWidget *w : m_bannerActions->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
        w->deleteLater();
    }
    for (const auto &a : actions) {
        auto *b = new QPushButton(a.label, m_bannerActions);
        b->setCursor(Qt::PointingHandCursor);
        if (a.primary) b->setProperty("role", "primary");
        b->setProperty("actionId", a.id);
        ui::describe(b, a.label);
        connect(b, &QPushButton::clicked, this, [this, id = a.id]() { emit bannerActionTriggered(id); });
        m_bannerActions->layout()->addWidget(b);
    }
    Q_UNUSED(t);
    restyle();
    m_banner->parentWidget()->setVisible(true);
}

void ChatPane::hideBanner() {
    m_bannerLevel.clear();
    m_banner->parentWidget()->setVisible(false);
}

bool ChatPane::isBannerVisible() const { return !m_banner->parentWidget()->isHidden(); }
QString ChatPane::bannerTitle() const { return isBannerVisible() ? m_bannerTitle->text() : QString(); }

void ChatPane::setComposerEnabled(bool enabled, const QString &placeholder) {
    m_composerEnabled = enabled;
    m_input->setReadOnly(!enabled);
    m_inputPlaceholder->setText(placeholder.isEmpty() ? m_placeholder : placeholder);
    updatePlaceholder();
    m_composerCard->setProperty("enabled", enabled ? "true" : "false");
    repolish(m_composerCard);
    updateSendButton();
}

void ChatPane::updatePlaceholder() {
    if (!m_inputPlaceholder) return;
    m_inputPlaceholder->setVisible(m_input->document()->isEmpty());
    m_inputPlaceholder->resize(m_input->viewport()->width(), m_inputPlaceholder->sizeHint().height());
}

void ChatPane::focusComposer() { m_input->setFocus(); }
QString ChatPane::composerText() const { return m_input->toPlainText(); }
void ChatPane::setComposerText(const QString &text) {
    m_input->setPlainText(text);
    m_input->moveCursor(QTextCursor::End);
}

QList<ChatBlock *> ChatPane::blocks() const {
    QList<ChatBlock *> out;
    for (int i = 0; i < m_layout->count(); ++i) {
        if (auto *b = qobject_cast<ChatBlock *>(m_layout->itemAt(i)->widget())) out << b;
    }
    return out;
}

int ChatPane::blockCount() const { return blocks().size(); }
bool ChatPane::hasConversation() const { return blockCount() > 0; }

void ChatPane::updateEmptyVisibility() {
    m_stack->setCurrentIndex(hasConversation() ? 1 : 0);
}

void ChatPane::updateComposerHeight() {
    auto &t = theme();
    const QFontMetrics fm(m_input->font());
    const int lines = qBound(1, qCeil(m_input->document()->size().height()), 8);
    const int h = lines * fm.lineSpacing() + t.px(4);
    if (m_input->height() != h) m_input->setFixedHeight(qMax(h, fm.lineSpacing() + t.px(4)));
}

void ChatPane::updateSendButton() {
    auto &t = theme();
    const auto &tk = t.tokens();
    if (m_busy) {
        m_sendBtn->setProperty("mode", "stop");
        m_sendBtn->setIcon(t.icon("square", tk.window));
        m_sendBtn->setEnabled(true);
        ui::describe(m_sendBtn, tr("Stop"), ui::withShortcut(tr("Stop the current turn"), QKeySequence(Qt::Key_Escape)));
    } else {
        m_sendBtn->setProperty("mode", "send");
        m_sendBtn->setIcon(t.icon("arrow-up", tk.accentText));
        m_sendBtn->setEnabled(m_composerEnabled && !m_input->toPlainText().trimmed().isEmpty());
        ui::describe(m_sendBtn, tr("Send"), ui::withShortcut(tr("Send message"), QKeySequence(Qt::Key_Return)));
    }
    repolish(m_sendBtn);
}

void ChatPane::submit() {
    const QString text = m_input->toPlainText().trimmed();
    if (text.isEmpty() || m_busy || !m_composerEnabled) return;
    if (m_history.isEmpty() || m_history.last() != text) m_history << text;
    m_historyPos = -1;
    emit promptSubmitted(text);
    // The owner decides whether it was accepted; clear only if it was taken
    // (a rejected prompt stays in the composer so nothing is lost).
}

bool ChatPane::eventFilter(QObject *obj, QEvent *event) {
    if (m_input && obj == m_input->viewport() && event->type() == QEvent::Resize) updatePlaceholder();
    if (obj == m_input && event->type() == QEvent::FocusIn) {
        m_composerCard->setProperty("focused", "true");
        repolish(m_composerCard);
    } else if (obj == m_input && event->type() == QEvent::FocusOut) {
        m_composerCard->setProperty("focused", "false");
        repolish(m_composerCard);
    }
    if (obj == m_input && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        const bool shift = key->modifiers() & Qt::ShiftModifier;
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) && !shift
            && !(key->modifiers() & Qt::AltModifier)) {
            submit();
            return true;
        }
        if (key->key() == Qt::Key_Escape && m_busy) {
            emit stopRequested();
            return true;
        }
        if (key->key() == Qt::Key_Up && !m_history.isEmpty()
            && (m_input->toPlainText().isEmpty() || m_historyPos >= 0)
            && m_input->textCursor().blockNumber() == 0) {
            m_historyPos = m_historyPos < 0 ? m_history.size() - 1 : qMax(0, m_historyPos - 1);
            setComposerText(m_history.at(m_historyPos));
            return true;
        }
        if (key->key() == Qt::Key_Down && m_historyPos >= 0) {
            ++m_historyPos;
            if (m_historyPos >= m_history.size()) {
                m_historyPos = -1;
                m_input->clear();
            } else {
                setComposerText(m_history.at(m_historyPos));
            }
            return true;
        }
        if (key->key() == Qt::Key_Slash && !m_input->isReadOnly()) {
            if (m_input->toPlainText().isEmpty()) {
                // First character of an empty composer → command palette.
                emit slashCommandRequested();
                return true; // don't insert the "/" character.
            }
            // A "/" at any non-first position → file-system picker (Hermes CLI).
            // The "/" itself is NOT inserted; the chosen path replaces it.
            // If a "." immediately precedes the cursor, the user typed "./" —
            // "current directory": the picker roots at cwd and inserts "./rel".
            const int pos = m_input->textCursor().position();
            const QString text = m_input->toPlainText();
            const bool relative = pos > 0 && text.at(pos - 1) == '.';
            emit filePathRequested(pos, relative);
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void ChatPane::addBlock(ChatBlock *block) {
    // Insert before the trailing stretch.
    m_layout->insertWidget(m_layout->count() - 1, block);
    updateEmptyVisibility();
}

ChatBlock *ChatPane::activeBlock(ChatBlock::Kind kind) {
    switch (kind) {
        case ChatBlock::Thinking:
            if (!m_activeThinking) {
                m_activeThinking = new ChatBlock(ChatBlock::Thinking, m_chatPx, m_thinkingPx, m_toolPx, m_column);
                addBlock(m_activeThinking);
            }
            return m_activeThinking;
        case ChatBlock::Agent:
            if (!m_activeAgent) {
                // The answer starts: reasoning so far is complete.
                if (m_activeThinking) {
                    m_activeThinking->finalize();
                    m_activeThinking = nullptr;
                }
                m_activeAgent = new ChatBlock(ChatBlock::Agent, m_chatPx, m_thinkingPx, m_toolPx, m_column);
                addBlock(m_activeAgent);
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
    // press. Insert the chosen path at the recorded cursor position — the spot
    // where the user typed the "/" — then return focus to the composer. When
    // replaceDot is true the user typed "./": the dot before the "/" is also
    // replaced, so the whole "./" becomes the chosen path.
    const QString text = m_input->toPlainText();
    const int pos = qBound(0, column, text.length());
    const int start = (replaceDot && pos > 0) ? pos - 1 : pos;
    m_input->setPlainText(text.left(start) + path + text.mid(pos));
    QTextCursor c = m_input->textCursor();
    c.setPosition(start + path.length());
    m_input->setTextCursor(c);
    m_input->setFocus();
}

void ChatPane::appendUserMessage(const QString &text) {
    // A new user message closes any open streaming blocks from the prior turn.
    closeActiveBlocks();
    // A new turn always resumes following the bottom (the user just sent a prompt).
    m_followingBottom = true;
    auto *block = new ChatBlock(ChatBlock::User, m_chatPx, m_thinkingPx, m_toolPx, m_column);
    block->appendText(text);
    addBlock(block);
    // The prompt was accepted: clear the composer.
    if (m_input->toPlainText().trimmed() == text.trimmed()) m_input->clear();
    scrollToBottom();
}

void ChatPane::appendNotice(const QString &text, ChatBlock::Level level) {
    closeActiveBlocks();
    auto *block = new ChatBlock(ChatBlock::Notice, m_chatPx, m_thinkingPx, m_toolPx, m_column);
    block->appendText(text);
    block->setLevel(level);
    addBlock(block);
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
    // A tool call closes the current thinking/agent blocks so ordering is preserved.
    closeActiveBlocks();
    auto *block = new ChatBlock(ChatBlock::Tool, m_chatPx, m_thinkingPx, m_toolPx, m_column);
    block->m_toolId = id;
    block->setToolInfo(title, kind);
    block->setToolStatus(QStringLiteral("in_progress"), QString());
    addBlock(block);
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
    // Tools still "running" when the turn ends didn't report back.
    for (ChatBlock *b : qAsConst(m_toolBlocks)) {
        if (b->toolStatus() == "in_progress" || b->toolStatus() == "pending") {
            b->setToolStatus(QStringLiteral("completed"), QString());
        }
    }
    m_toolBlocks.clear();
    scrollToBottom();
}

void ChatPane::setBusy(bool busy, const QString &label) {
    m_busy = busy;
    m_busyText = label.isEmpty() ? tr("Hermes is working") : label;
    m_spinner->setVisible(busy);
    m_spinner->setRunning(busy);
    m_busyLabel->setVisible(busy);
    m_hint->setVisible(!busy);
    if (busy) {
        m_busyClock.start();
        m_busyLabel->setText(tr("%1 · 0:00 · Esc to stop").arg(m_busyText));
        m_busyTimer->start();
    } else {
        m_busyTimer->stop();
    }
    updateSendButton();
}

void ChatPane::clearConversation() {
    for (ChatBlock *b : blocks()) {
        m_layout->removeWidget(b);
        b->deleteLater();
    }
    m_activeThinking = nullptr;
    m_activeAgent = nullptr;
    m_toolBlocks.clear();
    updateEmptyVisibility();
}

void ChatPane::applyFontSizes(int chatPx, int thinkingPx, int toolPx) {
    m_chatPx = chatPx;
    m_thinkingPx = thinkingPx;
    m_toolPx = toolPx;
    QFont f = m_input->font();
    f.setPixelSize(qMax(8, qRound(chatPx * theme().scale())));
    m_input->setFont(f);
    m_inputPlaceholder->setFont(f);
    updatePlaceholder();
    updateComposerHeight();
    // Restyle every existing block so the change is visible immediately.
    for (ChatBlock *b : blocks()) b->applyFontSizes(chatPx, thinkingPx, toolPx);
}

void ChatPane::scrollToBottom() {
    // Sticky: only pin to the bottom while we're actually following. If the user
    // has scrolled up to read, don't yank them back down on every chunk. The
    // rangeChanged hook pins once layout has caught up with new content.
    if (!m_followingBottom) return;
    QTimer::singleShot(0, this, [this]() {
        if (!m_followingBottom) return;
        if (auto *vbar = m_scroll->verticalScrollBar()) vbar->setValue(vbar->maximum());
    });
}
