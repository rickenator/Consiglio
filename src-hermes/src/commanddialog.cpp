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
    ui::fitOnShow(this);
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
