#include "commanddialog.h"

#include "theme.h"
#include "uiutil.h"

#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QRegularExpression>
#include <QShortcut>
#include <QVBoxLayout>

namespace {

// Icon in a soft accent tile + title + mono subtitle.
QWidget *dialogHeader(const QString &icon, const QString &title, const QString &subtitle, QWidget *parent) {
    auto &t = Theme::instance();
    auto *w = new QWidget(parent);
    auto *l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(t.px(12));
    auto *tile = new QLabel(w);
    const int s = t.px(40);
    tile->setFixedSize(s, s);
    tile->setAlignment(Qt::AlignCenter);
    // QSS wants #RRGGBBAA → use rgba() instead for portability.
    const QColor soft = t.tokens().accentSoft;
    tile->setStyleSheet(QStringLiteral("background: rgba(%1,%2,%3,%4); border-radius: %5px;")
                            .arg(soft.red()).arg(soft.green()).arg(soft.blue())
                            .arg(QString::number(soft.alphaF() * 1.6, 'f', 3))
                            .arg(t.px(10)));
    tile->setPixmap(t.pixmap(icon, t.px(20), t.tokens().accent));
    l->addWidget(tile, 0, Qt::AlignTop);
    auto *col = new QVBoxLayout();
    col->setSpacing(t.px(2));
    auto *ttl = new QLabel(title, w);
    ttl->setProperty("role", "heading");
    col->addWidget(ttl);
    if (!subtitle.isEmpty()) {
        auto *sub = new QLabel(subtitle, w);
        sub->setProperty("role", "faint");
        sub->setStyleSheet(QStringLiteral("font-family: \"JetBrains Mono\", \"DejaVu Sans Mono\", monospace;"));
        sub->setTextInteractionFlags(Qt::TextSelectableByMouse);
        col->addWidget(sub);
    }
    l->addLayout(col, 1);
    return w;
}

QLabel *section(const QString &text, QWidget *parent) {
    auto *l = new QLabel(text.toUpper(), parent);
    l->setProperty("role", "section");
    return l;
}

QFrame *previewCard(QLabel **label, QWidget *parent) {
    auto &t = Theme::instance();
    auto *card = new QFrame(parent);
    card->setProperty("role", "card");
    auto *l = new QHBoxLayout(card);
    l->setContentsMargins(t.px(12), t.px(9), t.px(12), t.px(9));
    l->setSpacing(t.px(8));
    auto *icon = new QLabel(card);
    icon->setPixmap(t.pixmap("square-terminal", t.px(15), t.tokens().accent));
    l->addWidget(icon, 0, Qt::AlignTop);
    *label = new QLabel(card);
    (*label)->setProperty("role", "mono");
    (*label)->setWordWrap(true);
    (*label)->setTextInteractionFlags(Qt::TextSelectableByMouse);
    (*label)->setAccessibleName(QObject::tr("Command preview"));
    l->addWidget(*label, 1);
    return card;
}

} // namespace

// ─── CommandDialog ────────────────────────────────────────────────────────

CommandDialog::CommandDialog(const CommandCatalog::Command &command, QWidget *parent)
    : QDialog(parent), m_command(command) {
    auto &t = Theme::instance();
    setWindowTitle(command.label);
    setModal(true);
    setMinimumWidth(t.px(520));
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(t.px(22), t.px(20), t.px(22), t.px(18));
    l->setSpacing(t.px(12));

    l->addWidget(dialogHeader(command.icon, command.label, QStringLiteral("/") + command.name, this));
    if (!command.description.isEmpty()) {
        auto *desc = new QLabel(command.description, this);
        desc->setWordWrap(true);
        desc->setProperty("role", "muted");
        l->addWidget(desc);
    }

    l->addSpacing(t.px(4));
    l->addWidget(section(command.requiresArgs() ? tr("Arguments (required)") : tr("Arguments (optional)"), this));
    m_args = new QLineEdit(this);
    QString placeholder = command.inputHint;
    if (placeholder.isEmpty()) placeholder = command.argsHint;
    m_args->setPlaceholderText(placeholder);
    m_args->setClearButtonEnabled(true);
    ui::describe(m_args, tr("Arguments for /%1").arg(command.name), placeholder);
    l->addWidget(m_args);

    if (!command.argsHint.isEmpty()) {
        auto *syntax = new QLabel(tr("Syntax:  /%1 %2").arg(command.name, command.argsHint.toHtmlEscaped()), this);
        syntax->setTextFormat(Qt::PlainText);
        syntax->setText(QStringLiteral("Syntax:  /%1 %2").arg(command.name, command.argsHint));
        syntax->setProperty("role", "faint");
        syntax->setWordWrap(true);
        syntax->setTextInteractionFlags(Qt::TextSelectableByMouse);
        l->addWidget(syntax);
    }

    const QStringList choices = command.choices();
    if (!choices.isEmpty()) {
        l->addSpacing(t.px(2));
        l->addWidget(section(tr("Quick options"), this));
        auto *chips = new QWidget(this);
        auto *flow = new ui::FlowLayout(chips, t.px(6), t.px(6));
        for (const QString &choice : choices) {
            auto *chip = new QPushButton(choice, chips);
            chip->setProperty("role", "chip");
            chip->setCursor(Qt::PointingHandCursor);
            chip->setCheckable(true);
            chip->setAutoExclusive(false);
            ui::describe(chip, tr("Use “%1”").arg(choice), tr("Fill in “%1”").arg(choice));
            connect(chip, &QPushButton::clicked, this, [this, choice]() { m_args->setText(choice); });
            flow->addWidget(chip);
            connect(m_args, &QLineEdit::textChanged, chip, [chip, choice](const QString &text) {
                chip->setChecked(text.trimmed() == choice);
            });
        }
        l->addWidget(chips);
    }

    l->addSpacing(t.px(4));
    l->addWidget(section(tr("Will send"), this));
    l->addWidget(previewCard(&m_preview, this));
    l->addStretch(1);

    auto *buttons = new QDialogButtonBox(this);
    auto *cancel = buttons->addButton(QDialogButtonBox::Cancel);
    ui::describe(cancel, tr("Cancel"), ui::withShortcut(tr("Close without running"), QKeySequence(Qt::Key_Escape)));
    m_run = buttons->addButton(tr("Run"), QDialogButtonBox::AcceptRole);
    m_run->setProperty("role", "primary");
    m_run->setIcon(t.icon("play", t.tokens().accentText));
    m_run->setDefault(true);
    ui::describe(m_run, tr("Run"), ui::withShortcut(tr("Send this command to Hermes"), QKeySequence(Qt::Key_Return)));
    l->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() { if (canRun()) accept(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_args, &QLineEdit::textChanged, this, &CommandDialog::updatePreview);
    connect(m_args, &QLineEdit::returnPressed, this, [this]() { if (canRun()) accept(); });
    auto *ctrlEnter = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
    connect(ctrlEnter, &QShortcut::activated, this, [this]() { if (canRun()) accept(); });
    updatePreview();
    m_args->setFocus();
}

QString CommandDialog::arguments() const { return m_args->text().trimmed(); }
void CommandDialog::setArguments(const QString &args) { m_args->setText(args); }

QString CommandDialog::commandLine() const {
    const QString args = arguments();
    return args.isEmpty() ? QStringLiteral("/") + m_command.name
                          : QStringLiteral("/%1 %2").arg(m_command.name, args);
}

bool CommandDialog::canRun() const { return !m_command.requiresArgs() || !arguments().isEmpty(); }

void CommandDialog::updatePreview() {
    m_preview->setText(commandLine());
    m_run->setEnabled(canRun());
}

// ─── GoalDialog ───────────────────────────────────────────────────────────

GoalDialog::GoalDialog(const CommandCatalog::GoalStatus &current, bool draftAvailable, QWidget *parent)
    : QDialog(parent) {
    auto &t = Theme::instance();
    setWindowTitle(tr("Set a Goal"));
    setModal(true);
    setMinimumWidth(t.px(560));
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(t.px(22), t.px(20), t.px(22), t.px(18));
    l->setSpacing(t.px(12));
    l->addWidget(dialogHeader("target", current.state == "none" || current.goal.isEmpty()
                                            ? tr("Set a Goal") : tr("Replace the Goal"),
                              QString(), this));
    auto *intro = new QLabel(tr("Hermes keeps working toward a standing goal across turns. After each turn a "
                                "judge checks whether it's done; Hermes continues until it is, you pause or "
                                "clear it, or the turn budget runs out."), this);
    intro->setWordWrap(true);
    intro->setProperty("role", "muted");
    l->addWidget(intro);

    if (!current.goal.isEmpty()) {
        auto *card = new QFrame(this);
        card->setProperty("role", "card");
        auto *cl = new QVBoxLayout(card);
        cl->setContentsMargins(t.px(12), t.px(10), t.px(12), t.px(10));
        cl->setSpacing(t.px(4));
        auto *top = new QHBoxLayout();
        top->addWidget(section(tr("Current goal"), card));
        top->addStretch();
        auto *badge = new QLabel(current.state.isEmpty() ? tr("set") : current.state, card);
        badge->setProperty("role", "badge");
        top->addWidget(badge);
        cl->addLayout(top);
        auto *g = new QLabel(current.goal, card);
        g->setWordWrap(true);
        g->setTextInteractionFlags(Qt::TextSelectableByMouse);
        cl->addWidget(g);
        if (!current.meta.isEmpty()) {
            auto *meta = new QLabel(current.meta, card);
            meta->setProperty("role", "faint");
            cl->addWidget(meta);
        }
        l->addWidget(card);
    }

    l->addWidget(section(current.goal.isEmpty() ? tr("Goal") : tr("New goal"), this));
    m_text = new QPlainTextEdit(this);
    m_text->setPlaceholderText(tr("Describe the outcome you want, e.g. “Get the test suite passing and "
                                  "summarise what changed.”\n\nOptional contract lines (one per line):\n"
                                  "verify: npm test"));
    m_text->setTabChangesFocus(true);
    m_text->setMinimumHeight(t.px(120));
    ui::describe(m_text, tr("Goal text"), tr("The outcome Hermes should work toward"));
    l->addWidget(m_text);

    m_draft = new QCheckBox(tr("Draft a completion contract first (Hermes proposes how “done” is checked)"), this);
    m_draft->setVisible(draftAvailable);
    ui::describe(m_draft, tr("Draft a completion contract first"),
                 tr("Sends /goal draft: an auxiliary model turns the goal into reviewable success criteria"));
    l->addWidget(m_draft);

    l->addWidget(section(tr("Will send"), this));
    l->addWidget(previewCard(&m_preview, this));

    auto *buttons = new QDialogButtonBox(this);
    auto *cancel = buttons->addButton(QDialogButtonBox::Cancel);
    ui::describe(cancel, tr("Cancel"), ui::withShortcut(tr("Close without changing the goal"), QKeySequence(Qt::Key_Escape)));
    m_set = buttons->addButton(current.goal.isEmpty() ? tr("Set Goal") : tr("Replace Goal"),
                               QDialogButtonBox::AcceptRole);
    m_set->setProperty("role", "primary");
    m_set->setIcon(t.icon("target", t.tokens().accentText));
    m_set->setDefault(true);
    ui::describe(m_set, m_set->text(),
                 ui::withShortcut(tr("Send the goal to Hermes"), QKeySequence(Qt::CTRL | Qt::Key_Return)));
    l->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() { if (!goalText().isEmpty()) accept(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_text, &QPlainTextEdit::textChanged, this, &GoalDialog::updateState);
    connect(m_draft, &QCheckBox::toggled, this, &GoalDialog::updateState);
    auto *ctrlEnter = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
    connect(ctrlEnter, &QShortcut::activated, this, [this]() { if (!goalText().isEmpty()) accept(); });
    updateState();
    m_text->setFocus();
}

QString GoalDialog::goalText() const { return m_text->toPlainText().trimmed(); }
void GoalDialog::setGoalText(const QString &text) { m_text->setPlainText(text); }
bool GoalDialog::draft() const { return m_draft->isVisible() && m_draft->isChecked(); }
void GoalDialog::setDraft(bool draft) { m_draft->setChecked(draft); }

QString GoalDialog::commandLine() const {
    QString text = goalText();
    if (draft()) return QStringLiteral("/goal draft ") + text;
    // A goal that starts with a control word ("pause the cron…") would be read
    // as a control command — Hermes's "--" form sends it verbatim.
    static const QStringList control = {"status", "show", "pause", "resume", "continue", "unpause",
                                        "clear", "stop", "done", "unwait", "wait", "gate", "draft", "--"};
    const QString first = text.section(QRegularExpression(QStringLiteral("\\s+")), 0, 0).toLower();
    if (control.contains(first)) return QStringLiteral("/goal -- ") + text;
    return QStringLiteral("/goal ") + text;
}

void GoalDialog::updateState() {
    const bool ok = !goalText().isEmpty();
    m_set->setEnabled(ok);
    QString preview = ok ? commandLine() : QStringLiteral("/goal …");
    if (preview.count('\n') > 3) preview = preview.section('\n', 0, 3) + QStringLiteral("\n…");
    m_preview->setText(preview);
}
