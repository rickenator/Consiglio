#include "commandui.h"

#include "commandhost.h"
#include "commandlivehosts.h"
#include "theme.h"
#include "uiutil.h"

#include <QLineEdit>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSet>
#include <QSpinBox>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QString normName(QString name) {
    name = name.trimmed();
    if (name.startsWith(QLatin1Char('/'))) name.remove(0, 1);
    return name.toLower();
}

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

const CommandPlan kPlans[] = {
    {QStringLiteral("model"), {QStringLiteral("models")},
     CommandArchetype::Picker, CommandApply::ConfigOption,
     QStringLiteral("Model"), QStringLiteral("model"), QStringLiteral("model"), {}, {}, false},
    {QStringLiteral("reasoning"), {},
     CommandArchetype::Picker, CommandApply::ConfigOption,
     QStringLiteral("Reasoning"), QStringLiteral("reasoning_effort"), QStringLiteral("thought_level"), {}, {}, false},
    {QStringLiteral("fast"), {},
     CommandArchetype::Picker, CommandApply::ConfigOption,
     QStringLiteral("Speed"), QStringLiteral("service_tier"), {}, {}, {}, false},
    {QStringLiteral("approvals"), {},
     CommandArchetype::Picker, CommandApply::QuietCommand,
     QStringLiteral("Approvals"), {}, {}, {}, {}, false},
    {QStringLiteral("yolo"), {},
     CommandArchetype::Confirm, CommandApply::QuietCommand,
     QStringLiteral("YOLO mode"), {}, {}, {},
     QStringLiteral("YOLO mode skips approval prompts for dangerous commands in this session. "
                    "You can turn it off the same way."), true},
    {QStringLiteral("personality"), {},
     CommandArchetype::Picker, CommandApply::QuietCommand,
     QStringLiteral("Personality"), {}, {}, {}, {}, false},
    {QStringLiteral("title"), {},
     CommandArchetype::TextPrompt, CommandApply::QuietCommand,
     QStringLiteral("Session title"), {}, {}, {}, {}, false},
    {QStringLiteral("new"), {QStringLiteral("reset")},
     CommandArchetype::Confirm, CommandApply::NewSession,
     QStringLiteral("New session"), {}, {}, {},
     QStringLiteral("Start a new session? The conversation you have open stays in your "
                    "session list and you can return to it later."), false},
    {QStringLiteral("undo"), {},
     CommandArchetype::Form, CommandApply::QuietCommand,
     QStringLiteral("Undo turns"), {}, {}, {}, {}, false},
    {QStringLiteral("retry"), {},
     CommandArchetype::Confirm, CommandApply::QuietCommand,
     QStringLiteral("Retry last message"), {}, {}, {},
     QStringLiteral("Resend your last message and generate a new reply? The previous reply stays in the thread."), false},
    {QStringLiteral("compress"), {QStringLiteral("compact")},
     CommandArchetype::Form, CommandApply::QuietCommand,
     QStringLiteral("Compress context"), {}, {}, {}, {}, false},
    {QStringLiteral("branch"), {QStringLiteral("fork")},
     CommandArchetype::Form, CommandApply::QuietCommand,
     QStringLiteral("Branch session"), {}, {}, {}, {}, false},
    {QStringLiteral("status"), {},
     CommandArchetype::Info, CommandApply::QuietCommand,
     QStringLiteral("Session status"), {}, {}, {}, {}, false},
    {QStringLiteral("usage"), {},
     CommandArchetype::Info, CommandApply::QuietCommand,
     QStringLiteral("Usage"), {}, {}, {}, {}, false},
    {QStringLiteral("context"), {QStringLiteral("ctx")},
     CommandArchetype::Info, CommandApply::QuietCommand,
     QStringLiteral("Context window"), {}, {}, {}, {}, false},
    {QStringLiteral("version"), {QStringLiteral("v")},
     CommandArchetype::Info, CommandApply::QuietCommand,
     QStringLiteral("Version"), {}, {}, {}, {}, false},
    {QStringLiteral("profile"), {},
     CommandArchetype::Info, CommandApply::QuietCommand,
     QStringLiteral("Profile"), {}, {}, {}, {}, false},
    {QStringLiteral("whoami"), {},
     CommandArchetype::Info, CommandApply::QuietCommand,
     QStringLiteral("Who am I"), {}, {}, {}, {}, false},
    {QStringLiteral("tools"), {},
     CommandArchetype::Form, CommandApply::QuietCommand,
     QStringLiteral("Tools"), {}, {}, {}, {}, false},
    {QStringLiteral("save"), {},
     CommandArchetype::Form, CommandApply::QuietCommand,
     QStringLiteral("Export conversation"), {}, {}, {}, {}, false},
    {QStringLiteral("plan"), {},
     CommandArchetype::TextPrompt, CommandApply::QuietCommand,
     QStringLiteral("Write a plan"), {}, {}, {}, {}, false},
    {QStringLiteral("queue"), {QStringLiteral("q")},
     CommandArchetype::LivePanel, CommandApply::QuietCommand,
     QStringLiteral("Queue"), {}, {}, {}, {}, false},
    {QStringLiteral("goal"), {},
     CommandArchetype::LivePanel, CommandApply::QuietCommand,
     QStringLiteral("Goal"), {}, {}, {}, {}, false},
    {QStringLiteral("subgoal"), {},
     CommandArchetype::LivePanel, CommandApply::QuietCommand,
     QStringLiteral("Goal"), {}, {}, {}, {}, false},
    {QStringLiteral("heartbeat"), {QStringLiteral("hb")},
     CommandArchetype::LivePanel, CommandApply::QuietCommand,
     QStringLiteral("Heartbeat"), {}, {}, {}, {}, false},
    {QStringLiteral("loop"), {QStringLiteral("proactive")},
     CommandArchetype::LivePanel, CommandApply::QuietCommand,
     QStringLiteral("Loop"), {}, {}, {}, {}, false},
    {QStringLiteral("agents"), {QStringLiteral("tasks")},
     CommandArchetype::LivePanel, CommandApply::QuietCommand,
     QStringLiteral("Agents"), {}, {}, {}, {}, false},
    {QStringLiteral("memory"), {},
     CommandArchetype::LivePanel, CommandApply::QuietCommand,
     QStringLiteral("Memory"), {}, {}, {}, {}, false},
    {QStringLiteral("skills"), {},
     CommandArchetype::LivePanel, CommandApply::QuietCommand,
     QStringLiteral("Skills"), {}, {}, {}, {}, false},
    {QStringLiteral("rollback"), {},
     CommandArchetype::LivePanel, CommandApply::QuietCommand,
     QStringLiteral("Checkpoints"), {}, {}, {}, {}, true},
    {QStringLiteral("snapshot"), {QStringLiteral("snap")},
     CommandArchetype::LivePanel, CommandApply::QuietCommand,
     QStringLiteral("Snapshots"), {}, {}, {}, {}, false},
    {QStringLiteral("debug"), {},
     CommandArchetype::LivePanel, CommandApply::QuietCommand,
     QStringLiteral("Debug report"), {}, {}, {}, {}, false},
    {QStringLiteral("insights"), {},
     CommandArchetype::LivePanel, CommandApply::QuietCommand,
     QStringLiteral("Usage insights"), {}, {}, {}, {}, false},
    {QStringLiteral("diff"), {},
     CommandArchetype::LivePanel, CommandApply::QuietCommand,
     QStringLiteral("Diff"), {}, {}, {}, {}, false},
    {QStringLiteral("cron"), {},
     CommandArchetype::LivePanel, CommandApply::QuietCommand,
     QStringLiteral("Scheduled tasks"), {}, {}, {}, {}, false},
    {QStringLiteral("suggestions"), {QStringLiteral("suggest")},
     CommandArchetype::LivePanel, CommandApply::QuietCommand,
     QStringLiteral("Suggestions"), {}, {}, {}, {}, false},
    {QStringLiteral("blueprint"), {QStringLiteral("bp")},
     CommandArchetype::LivePanel, CommandApply::QuietCommand,
     QStringLiteral("Blueprints"), {}, {}, {}, {}, false},
    {QStringLiteral("steer"), {QStringLiteral("s")},
     CommandArchetype::TextPrompt, CommandApply::QuietCommand,
     QStringLiteral("Steer this turn"), {}, {}, {}, {}, false},
    {QStringLiteral("busy"), {},
     CommandArchetype::Picker, CommandApply::QuietCommand,
     QStringLiteral("Busy input"), {}, {}, {}, {}, false},
    {QStringLiteral("autoprune"), {},
     CommandArchetype::Picker, CommandApply::QuietCommand,
     QStringLiteral("Auto-prune"), {}, {}, {}, {}, false},
    {QStringLiteral("codex-runtime"), {},
     CommandArchetype::Picker, CommandApply::QuietCommand,
     QStringLiteral("Codex runtime"), {}, {}, {}, {}, false},
    {QStringLiteral("bg"), {},
     CommandArchetype::TextPrompt, CommandApply::SendPrompt,
     QStringLiteral("Run in background"), {}, {}, {}, {}, false},
    {QStringLiteral("btw"), {},
     CommandArchetype::TextPrompt, CommandApply::SendPrompt,
     QStringLiteral("Side question"), {}, {}, {}, {}, false},
    {QStringLiteral("moa"), {},
     CommandArchetype::TextPrompt, CommandApply::SendPrompt,
     QStringLiteral("Mixture of agents"), {}, {}, {}, {}, false},
    {QStringLiteral("refine"), {},
     CommandArchetype::TextPrompt, CommandApply::SendPrompt,
     QStringLiteral("Refine and learn"), {}, {}, {}, {}, false},
    {QStringLiteral("review"), {},
     CommandArchetype::TextPrompt, CommandApply::SendPrompt,
     QStringLiteral("Independent review"), {}, {}, {}, {}, false},
    {QStringLiteral("learn"), {},
     CommandArchetype::TextPrompt, CommandApply::SendPrompt,
     QStringLiteral("Learn a skill"), {}, {}, {}, {}, false},
    {QStringLiteral("init"), {},
     CommandArchetype::TextPrompt, CommandApply::QuietCommand,
     QStringLiteral("Generate AGENTS.md"), {}, {}, {}, {}, false},
    {QStringLiteral("resume"), {},
     CommandArchetype::Picker, CommandApply::ResumeSession,
     QStringLiteral("Resume session"), {}, {}, {}, {}, false},
    {QStringLiteral("bundles"), {},
     CommandArchetype::LivePanel, CommandApply::QuietCommand,
     QStringLiteral("Skill bundles"), {}, {}, {}, {}, false},
    {QStringLiteral("plugins"), {},
     CommandArchetype::LivePanel, CommandApply::QuietCommand,
     QStringLiteral("Plugins"), {}, {}, {}, {}, false},
    {QStringLiteral("toolsets"), {},
     CommandArchetype::LivePanel, CommandApply::QuietCommand,
     QStringLiteral("Toolsets"), {}, {}, {}, {}, false},
    {QStringLiteral("curator"), {},
     CommandArchetype::LivePanel, CommandApply::QuietCommand,
     QStringLiteral("Skill curator"), {}, {}, {}, {}, false},
    {QStringLiteral("kanban"), {},
     CommandArchetype::LivePanel, CommandApply::QuietCommand,
     QStringLiteral("Kanban"), {}, {}, {}, {}, false},
    {QStringLiteral("subscription"), {QStringLiteral("upgrade")},
     CommandArchetype::Info, CommandApply::QuietCommand,
     QStringLiteral("Subscription"), {}, {}, {}, {}, false},
    {QStringLiteral("topup"), {},
     CommandArchetype::Info, CommandApply::QuietCommand,
     QStringLiteral("Nous balance"), {}, {}, {}, {}, false},
    {QStringLiteral("reload"), {},
     CommandArchetype::Confirm, CommandApply::QuietCommand,
     QStringLiteral("Reload environment"), {}, {}, {},
     QStringLiteral("Reload the environment from disk? Skills, tools, and MCP servers are re-read."), true},
    {QStringLiteral("reload-mcp"), {},
     CommandArchetype::Confirm, CommandApply::QuietCommand,
     QStringLiteral("Reload MCP servers"), {}, {}, {},
     QStringLiteral("Reload MCP servers from the Hermes config?"), true},
    {QStringLiteral("reload-skills"), {},
     CommandArchetype::Confirm, CommandApply::QuietCommand,
     QStringLiteral("Reload skills"), {}, {}, {},
     QStringLiteral("Re-scan skills for anything newly installed or removed?"), true},
    {QStringLiteral("help"), {},
     CommandArchetype::AppLink, CommandApply::OpenPalette, {}, {}, {}, {}, {}, false},
    {QStringLiteral("commands"), {},
     CommandArchetype::AppLink, CommandApply::OpenPalette, {}, {}, {}, {}, {}, false},
    {QStringLiteral("palette"), {},
     CommandArchetype::AppLink, CommandApply::OpenPalette, {}, {}, {}, {}, {}, false},
    {QStringLiteral("sessions"), {},
     CommandArchetype::AppLink, CommandApply::FocusSessions, {}, {}, {}, {}, {}, false},
    {QStringLiteral("stop"), {},
     CommandArchetype::AppLink, CommandApply::StopTurn, {}, {}, {}, {}, {}, false},
    {QStringLiteral("config"), {},
     CommandArchetype::AppLink, CommandApply::OpenSettings, {}, {}, {},
     QStringLiteral("config"), {}, false},
    {QStringLiteral("update"), {},
     CommandArchetype::AppLink, CommandApply::CheckUpdates, {}, {}, {}, {}, {}, false},
    {QStringLiteral("login"), {},
     CommandArchetype::AppLink, CommandApply::OpenSetup, {}, {}, {}, {}, {}, false},
    {QStringLiteral("quit"), {QStringLiteral("exit")},
     CommandArchetype::AppLink, CommandApply::QuitApp, {}, {}, {}, {}, {}, false},
};

} // namespace

// ─── CommandUi ────────────────────────────────────────────────────────────

CommandPlan CommandUi::planFor(const QString &name) {
    const QString key = normName(name);
    CommandPlan fallback;
    fallback.name = key;
    fallback.archetype = CommandArchetype::Passthrough;
    fallback.apply = CommandApply::Passthrough;
    if (key.isEmpty()) return fallback;
    for (const CommandPlan &p : kPlans) {
        if (p.name == key) return p;
        for (const QString &alias : p.aliases) {
            if (alias == key) return p;
        }
    }
    return fallback;
}

bool CommandUi::hasNativeUi(const QString &name) {
    const auto a = planFor(name).archetype;
    return a != CommandArchetype::Passthrough;
}

bool CommandUi::isAppLink(const QString &name) {
    return planFor(name).archetype == CommandArchetype::AppLink;
}

bool CommandUi::isHiddenChrome(const QString &name) {
    const QString n = planFor(name).name.isEmpty() ? name.trimmed().toLower() : planFor(name).name;
    static const QSet<QString> hidden = {
        QStringLiteral("history"), QStringLiteral("prompt"), QStringLiteral("compose"),
        QStringLiteral("rewind"), QStringLiteral("clear"), QStringLiteral("redraw"),
        QStringLiteral("worktree"), QStringLiteral("statusbar"), QStringLiteral("battery"),
        QStringLiteral("timestamps"), QStringLiteral("verbose"), QStringLiteral("focus"),
        QStringLiteral("footer"), QStringLiteral("skin"), QStringLiteral("indicator"),
        QStringLiteral("copy"), QStringLiteral("paste"), QStringLiteral("image"),
        QStringLiteral("pet"), QStringLiteral("hatch")
    };
    return hidden.contains(n) || hidden.contains(name.trimmed().toLower());
}

QString CommandUi::badgeFor(const QString &name) {
    return hasNativeUi(name) ? QStringLiteral("Dialog") : QStringLiteral("Chat");
}

QStringList CommandUi::nativeCommandNames() {
    QStringList names;
    for (const CommandPlan &p : kPlans) names << p.name;
    return names;
}


QList<CommandPickerItem> CommandUi::itemsFromReport(const QString &text) {
    QList<CommandPickerItem> items;
    QStringList seen;
    auto add = [&](QString raw) {
        raw = raw.trimmed();
        raw.remove(QLatin1Char('`'));
        raw = raw.trimmed();
        if (raw.endsWith(QLatin1Char(','))) raw.chop(1);
        if (raw.isEmpty() || raw.size() > 64) return;
        const QString key = raw.toLower();
        if (seen.contains(key)) return;
        if (key.contains(QLatin1Char(' ')) && key != QLatin1String("don't ask")) return;
        seen << key;
        items.push_back({raw, raw, QString(), QString()});
    };
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (QString line : lines) {
        line = line.trimmed();
        const int colon = line.indexOf(QLatin1Char(':'));
        QString label, rest = line;
        if (colon > 0 && colon < 24) {
            label = line.left(colon).trimmed().toLower();
            rest = line.mid(colon + 1).trimmed();
        }
        if (label.contains(QLatin1String("available")) || label.contains(QLatin1String("personalit"))
            || label == QLatin1String("modes") || label == QLatin1String("choices")
            || label.contains(QLatin1String("options"))) {
            for (const QString &part : rest.split(QRegularExpression(QStringLiteral("[,;|/]")))) add(part);
            continue;
        }
        if (line.startsWith(QLatin1Char('-')) || line.startsWith(QLatin1Char('*'))
            || line.startsWith(QStringLiteral("•"))) {
            add(line.mid(1));
        }
    }
    return items;
}

QString CommandUi::currentFromReport(const QString &text, const QStringList &known) {
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (QString line : lines) {
        line = line.trimmed();
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon <= 0) continue;
        const QString label = line.left(colon).trimmed().toLower();
        QString rest = line.mid(colon + 1).trimmed();
        rest.remove(QLatin1Char('`'));
        rest = rest.split(QRegularExpression(QStringLiteral("[,;(]"))).first().trimmed();
        if (label.contains(QLatin1String("current")) || label == QLatin1String("mode")
            || label.contains(QLatin1String("personality")) || label == QLatin1String("value")
            || label.contains(QLatin1String("using"))) {
            if (rest.isEmpty()) continue;
            if (known.isEmpty()) return rest;
            for (const QString &k : known) if (k.compare(rest, Qt::CaseInsensitive) == 0) return k;
            return rest;
        }
    }
    if (!known.isEmpty()) {
        const QString lower = text.toLower();
        for (const QString &k : known) {
            if (lower.contains(k.toLower())) return k;
        }
    }
    return {};
}

// ─── CommandHostDialog ────────────────────────────────────────────────────

CommandHostDialog::CommandHostDialog(const CommandPlan &plan, const CommandCatalog::Command &command,
                                     QWidget *parent)
    : QDialog(parent), m_plan(plan), m_command(command) {
    if (m_command.name.isEmpty()) m_command.name = m_plan.name;
    buildShell();
    buildBody();
    updateSaveEnabled();
    QTimer::singleShot(0, this, [this]() { focusFirstField(); });
}

CommandHostDialog::~CommandHostDialog() { delete m_host; }

QWidget *CommandHostDialog::bodyWidget() const {
    return findChild<QWidget *>(QStringLiteral("commandBody"));
}

QVBoxLayout *CommandHostDialog::bodyLayout() const {
    auto *body = bodyWidget();
    return body ? qobject_cast<QVBoxLayout *>(body->layout()) : nullptr;
}

QString CommandHostDialog::defaultTitle() const {
    if (!m_plan.title.isEmpty()) return m_plan.title;
    if (!m_command.label.isEmpty()) return m_command.label;
    return CommandCatalog::labelFor(m_plan.name);
}

QString CommandHostDialog::defaultIcon() const {
    if (!m_command.icon.isEmpty()) return m_command.icon;
    return CommandCatalog::iconFor(m_plan.name, m_command.group);
}

void CommandHostDialog::buildShell() {
    auto &t = Theme::instance();
    setWindowTitle(defaultTitle());
    setModal(true);
    setObjectName(QStringLiteral("commandHost"));
    setMinimumWidth(t.px(440));
    setSizeGripEnabled(true);
    ui::fitOnShow(this);
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(t.px(22), t.px(20), t.px(22), t.px(18));
    l->setSpacing(t.px(12));

    const QString slash = QLatin1Char('/') + (m_command.name.isEmpty() ? m_plan.name : m_command.name);
    l->addWidget(dialogHeader(defaultIcon(), defaultTitle(), slash, this));

    QString desc = m_command.description;
    if (desc.isEmpty() && !m_plan.confirmText.isEmpty() && m_plan.archetype != CommandArchetype::Confirm)
        desc = m_plan.confirmText;
    if (!desc.isEmpty() && m_plan.archetype != CommandArchetype::Confirm) {
        auto *d = new QLabel(desc, this);
        d->setWordWrap(true);
        d->setProperty("role", "muted");
        d->setObjectName(QStringLiteral("commandDescription"));
        l->addWidget(d);
    }

    m_error = new QLabel(this);
    m_error->setObjectName(QStringLiteral("commandErrorBanner"));
    m_error->setWordWrap(true);
    m_error->setVisible(false);
    m_error->setStyleSheet(QStringLiteral("color: %1; background: rgba(%2,%3,%4,0.18); border-radius: %5px; padding: %6px %7px;")
                               .arg(t.tokens().danger.name())
                               .arg(t.tokens().danger.red()).arg(t.tokens().danger.green()).arg(t.tokens().danger.blue())
                               .arg(t.px(8)).arg(t.px(8)).arg(t.px(10)));
    l->addWidget(m_error);

    // Body is added by buildBody() into a stretchable slot: insert before buttons later.
    // We use a container so archetypes can add widgets to it.
    auto *body = new QWidget(this);
    body->setObjectName(QStringLiteral("commandBody"));
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(t.px(10));
    body->setProperty("bodyLayout", QVariant::fromValue(static_cast<void *>(bodyLayout)));
    l->addWidget(body, 1);

    auto *status = new QWidget(this);
    auto *sl = new QHBoxLayout(status);
    sl->setContentsMargins(0, 0, 0, 0);
    sl->setSpacing(t.px(8));
    auto *spin = new ui::Spinner(status, t.px(16));
    spin->setObjectName(QStringLiteral("commandSpinner"));
    spin->setVisible(false);
    m_spinner = spin;
    sl->addWidget(spin, 0, Qt::AlignVCenter);
    m_busyLabel = new QLabel(this);
    m_busyLabel->setProperty("role", "muted");
    m_busyLabel->setVisible(false);
    sl->addWidget(m_busyLabel, 1);
    l->addWidget(status);

    m_buttons = new QDialogButtonBox(this);
    m_cancel = m_buttons->addButton(QDialogButtonBox::Cancel);
    ui::describe(m_cancel, tr("Cancel"), ui::withShortcut(tr("Close without changing anything"),
                                                          QKeySequence(Qt::Key_Escape)));
    const bool info = m_plan.archetype == CommandArchetype::Info;
    const bool live = m_plan.archetype == CommandArchetype::LivePanel;
    const bool confirm = m_plan.archetype == CommandArchetype::Confirm;
    if (info || live) {
        m_refresh = m_buttons->addButton(tr("Refresh"), QDialogButtonBox::ActionRole);
        ui::describe(m_refresh, tr("Refresh"), tr("Ask Hermes for a fresh report"));
        connect(m_refresh, &QPushButton::clicked, this, &CommandHostDialog::refreshRequested);
    }
    if (info) {
        m_save = m_buttons->addButton(tr("Close"), QDialogButtonBox::AcceptRole);
        m_save->setDefault(true);
        ui::describe(m_save, tr("Close"), ui::withShortcut(tr("Close this dialog"), QKeySequence(Qt::Key_Return)));
        m_cancel->setVisible(false);
        m_cancel->setEnabled(false);
    } else {
        const QString label = confirm ? tr("Confirm") : tr("Save");
        m_save = m_buttons->addButton(label, QDialogButtonBox::AcceptRole);
        m_save->setProperty("role", "primary");
        m_save->setDefault(true);
        if (m_plan.dangerous) m_save->setProperty("role", "danger");
        m_save->setObjectName(QStringLiteral("commandSaveButton"));
        ui::describe(m_save, label,
                     ui::withShortcut(confirm ? tr("Confirm this action") : tr("Apply the selection"),
                                      QKeySequence(Qt::Key_Return)));
    }
    l->addWidget(m_buttons);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_save, &QPushButton::clicked, this, &CommandHostDialog::onSave);
    auto *ctrlEnter = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
    connect(ctrlEnter, &QShortcut::activated, this, &CommandHostDialog::onSave);
}

void CommandHostDialog::buildBody() {
    // The body belongs to the command's own host; the shell only supplies the
    // container and the rendering primitives. Unknown names fall back to the
    // archetype's generic host inside the factory, so nothing is ever hidden.
    m_host = createCommandHost(m_plan, m_command);
    if (!m_host) return;
    m_host->build(*this);
}

void CommandHostDialog::buildPickerBody() {
    auto *body = findChild<QWidget *>(QStringLiteral("commandBody"));
    auto *layout = qobject_cast<QVBoxLayout *>(body->layout());
    auto &t = Theme::instance();

    m_infoPanel = new QFrame(body);
    m_infoPanel->setObjectName(QStringLiteral("commandInfoPanel"));
    m_infoPanel->setProperty("role", "card");
    auto *il = new QVBoxLayout(m_infoPanel);
    il->setContentsMargins(t.px(14), t.px(12), t.px(14), t.px(12));
    il->setSpacing(t.px(6));
    auto *top = new QHBoxLayout();
    top->setContentsMargins(0, 0, 0, 0);
    m_infoBadge = new QLabel(m_infoPanel);
    m_infoBadge->setObjectName(QStringLiteral("statusBadge"));
    m_infoBadge->setProperty("state", "pending");
    top->addWidget(m_infoBadge, 0, Qt::AlignVCenter);
    top->addStretch(1);
    il->addLayout(top);
    m_infoHeadline = new QLabel(m_infoPanel);
    m_infoHeadline->setProperty("role", "heading");
    m_infoHeadline->setWordWrap(true);
    m_infoHeadline->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_infoHeadline->setObjectName(QStringLiteral("commandInfoHeadline"));
    il->addWidget(m_infoHeadline);
    m_infoBody = new QLabel(m_infoPanel);
    m_infoBody->setProperty("role", "muted");
    m_infoBody->setWordWrap(true);
    m_infoBody->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_infoBody->setObjectName(QStringLiteral("commandInfoBody"));
    il->addWidget(m_infoBody);
    layout->addWidget(m_infoPanel);

    layout->addWidget(section(tr("Available"), body));

    m_filter = new QLineEdit(body);
    m_filter->setObjectName(QStringLiteral("commandPickerFilter"));
    m_filter->setClearButtonEnabled(true);
    m_filter->setPlaceholderText(tr("Filter…"));
    ui::describe(m_filter, tr("Filter"), tr("Type to narrow the list"));
    layout->addWidget(m_filter);
    connect(m_filter, &QLineEdit::textChanged, this, &CommandHostDialog::rebuildPickerList);

    m_list = new QListWidget(body);
    m_list->setObjectName(QStringLiteral("commandPickerList"));
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setAlternatingRowColors(false);
    m_list->setUniformItemSizes(false);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setMinimumHeight(t.px(220));
    ui::describe(m_list, tr("Choices"), tr("Select an item to preview it. Nothing is changed until you Save."));
    layout->addWidget(m_list, 1);
    connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *) { onPickerCurrentChanged(); });
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *) { onPickerCurrentChanged(); });

    if (m_plan.name == QLatin1String("model")) {
        m_persist = new QCheckBox(tr("Also save as the default for new sessions"), body);
        m_persist->setObjectName(QStringLiteral("commandPersistDefault"));
        ui::describe(m_persist, tr("Save as default"),
                     tr("After applying to this session, also persist the choice with /model --global"));
        layout->addWidget(m_persist);
        connect(m_persist, &QCheckBox::toggled, this, [this](bool) { updateSaveEnabled(); });
    }

    // Seed from catalog alternatives when we have no ACP data yet.
    const QStringList choices = m_command.choices();
    if (!choices.isEmpty() && m_items.isEmpty()) {
        QList<CommandPickerItem> items;
        for (const QString &c : choices) items.push_back({c, c, QString(), QString()});
        setPickerItems(items, QString());
    } else {
        updateInfoPanel();
    }
}

void CommandHostDialog::buildInfoBody() {
    auto *body = findChild<QWidget *>(QStringLiteral("commandBody"));
    auto *layout = qobject_cast<QVBoxLayout *>(body->layout());
    auto &t = Theme::instance();
    m_body = new QPlainTextEdit(body);
    m_body->setObjectName(QStringLiteral("commandInfoBodyText"));
    m_body->setReadOnly(true);
    m_body->setMinimumHeight(t.px(240));
    m_body->setStyleSheet(QStringLiteral("font-family: \"JetBrains Mono\", \"DejaVu Sans Mono\", monospace;"));
    ui::describe(m_body, defaultTitle(), tr("Report from Hermes"));
    layout->addWidget(m_body, 1);
    m_body->setPlainText(tr("Loading…"));
}

void CommandHostDialog::buildConfirmBody() {
    auto *body = findChild<QWidget *>(QStringLiteral("commandBody"));
    auto *layout = qobject_cast<QVBoxLayout *>(body->layout());
    auto *card = new QFrame(body);
    card->setProperty("role", "card");
    auto &t = Theme::instance();
    auto *il = new QVBoxLayout(card);
    il->setContentsMargins(t.px(14), t.px(14), t.px(14), t.px(14));
    auto *text = new QLabel(m_plan.confirmText, card);
    text->setWordWrap(true);
    text->setObjectName(QStringLiteral("commandConfirmText"));
    text->setTextInteractionFlags(Qt::TextSelectableByMouse);
    il->addWidget(text);
    layout->addWidget(card);
    layout->addStretch(1);
}

void CommandHostDialog::buildTextPromptBody() {
    auto *body = findChild<QWidget *>(QStringLiteral("commandBody"));
    auto *layout = qobject_cast<QVBoxLayout *>(body->layout());
    layout->addWidget(section(tr("Value"), body));
    m_text = new QLineEdit(body);
    m_text->setObjectName(QStringLiteral("commandTextArgument"));
    m_text->setClearButtonEnabled(true);
    QString ph = m_command.inputHint;
    if (ph.isEmpty()) ph = m_command.argsHint;
    if (ph.isEmpty()) ph = (m_plan.name == QLatin1String("title")) ? tr("Session title") : tr("Enter a value");
    m_text->setPlaceholderText(ph);
    ui::describe(m_text, defaultTitle(), ph);
    layout->addWidget(m_text);
    if (!m_command.argsHint.isEmpty()) {
        auto *syntax = new QLabel(QStringLiteral("/%1 %2").arg(m_command.name, m_command.argsHint), body);
        syntax->setProperty("role", "faint");
        syntax->setWordWrap(true);
        syntax->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(syntax);
    }
    layout->addStretch(1);
    connect(m_text, &QLineEdit::textChanged, this, [this](const QString &) { updateSaveEnabled(); });
    m_text->setFocus();
}

void CommandHostDialog::buildUndoForm() {
    auto *body = findChild<QWidget *>(QStringLiteral("commandBody"));
    auto *layout = qobject_cast<QVBoxLayout *>(body->layout());
    auto *row = new QHBoxLayout();
    auto *lab = new QLabel(tr("Turns to undo"), body);
    m_undoSpin = new QSpinBox(body);
    m_undoSpin->setObjectName(QStringLiteral("commandUndoCount"));
    m_undoSpin->setRange(1, 50);
    m_undoSpin->setValue(1);
    ui::describe(m_undoSpin, tr("Turns to undo"), tr("Number of your most recent turns to remove"));
    row->addWidget(lab);
    row->addWidget(m_undoSpin, 1);
    layout->addLayout(row);
    auto *note = new QLabel(tr("Hermes will drop that many of your messages and their replies, then wait for you to continue."), body);
    note->setWordWrap(true);
    note->setProperty("role", "muted");
    layout->addWidget(note);
    layout->addStretch(1);
}

void CommandHostDialog::buildCompressForm() {
    auto *body = findChild<QWidget *>(QStringLiteral("commandBody"));
    auto *layout = qobject_cast<QVBoxLayout *>(body->layout());
    auto &t = Theme::instance();
    layout->addWidget(section(tr("Focus (optional)"), body));
    m_text = new QLineEdit(body);
    m_text->setObjectName(QStringLiteral("commandTextArgument"));
    m_text->setClearButtonEnabled(true);
    m_text->setPlaceholderText(tr("Topic to keep in focus"));
    ui::describe(m_text, tr("Focus"), tr("Leave blank to compress the whole conversation"));
    layout->addWidget(m_text);
    m_previewOnly = new QCheckBox(tr("Preview only — don’t compress yet"), body);
    m_previewOnly->setObjectName(QStringLiteral("commandCompressPreview"));
    layout->addWidget(m_previewOnly);
    auto *note = new QLabel(tr("Compression shrinks older turns so there is more room in the context window. "
                               "A preview shows what would happen without changing the session."), body);
    note->setWordWrap(true);
    note->setProperty("role", "muted");
    layout->addWidget(note);
    m_body = new QPlainTextEdit(body);
    m_body->setObjectName(QStringLiteral("commandInfoBodyText"));
    m_body->setReadOnly(true);
    m_body->setVisible(false);
    m_body->setMinimumHeight(t.px(140));
    m_body->setStyleSheet(QStringLiteral("font-family: \"JetBrains Mono\", \"DejaVu Sans Mono\", monospace;"));
    layout->addWidget(m_body, 1);
    layout->addStretch(1);
}

void CommandHostDialog::buildBranchForm() {
    auto *body = findChild<QWidget *>(QStringLiteral("commandBody"));
    auto *layout = qobject_cast<QVBoxLayout *>(body->layout());
    layout->addWidget(section(tr("Branch name (optional)"), body));
    m_text = new QLineEdit(body);
    m_text->setObjectName(QStringLiteral("commandTextArgument"));
    m_text->setClearButtonEnabled(true);
    m_text->setPlaceholderText(tr("Name for the new branch"));
    ui::describe(m_text, tr("Name"), tr("Optional name for the branched session"));
    layout->addWidget(m_text);
    m_stayHere = new QCheckBox(tr("Stay in this window (--here)"), body);
    m_stayHere->setObjectName(QStringLiteral("commandBranchStayHere"));
    m_stayHere->setChecked(true);
    ui::describe(m_stayHere, tr("Stay here"), tr("Keep working in this window instead of moving the new branch elsewhere"));
    layout->addWidget(m_stayHere);
    layout->addStretch(1);
    connect(m_text, &QLineEdit::textChanged, this, [this](const QString &) { updateSaveEnabled(); });
}

void CommandHostDialog::buildSaveForm() {
    auto *body = findChild<QWidget *>(QStringLiteral("commandBody"));
    auto *layout = qobject_cast<QVBoxLayout *>(body->layout());
    layout->addWidget(section(tr("Format"), body));
    m_saveFormat = new QComboBox(body);
    m_saveFormat->setObjectName(QStringLiteral("commandSaveFormat"));
    m_saveFormat->addItem(tr("Markdown"), QStringLiteral("md"));
    m_saveFormat->addItem(tr("JSON"), QStringLiteral("json"));
    m_saveFormat->addItem(tr("HTML"), QStringLiteral("html"));
    layout->addWidget(m_saveFormat);
    layout->addWidget(section(tr("File name (optional)"), body));
    m_saveFile = new QLineEdit(body);
    m_saveFile->setObjectName(QStringLiteral("commandSaveFilename"));
    m_saveFile->setClearButtonEnabled(true);
    m_saveFile->setPlaceholderText(tr("Leave blank for the default name"));
    layout->addWidget(m_saveFile);
    m_saveRedact = new QCheckBox(tr("Redact secrets"), body);
    m_saveRedact->setObjectName(QStringLiteral("commandSaveRedact"));
    m_saveRedact->setChecked(true);
    layout->addWidget(m_saveRedact);
    layout->addStretch(1);
}

void CommandHostDialog::buildToolsForm() {
    auto *body = findChild<QWidget *>(QStringLiteral("commandBody"));
    auto *layout = qobject_cast<QVBoxLayout *>(body->layout());
    auto &t = Theme::instance();
    m_body = new QPlainTextEdit(body);
    m_body->setObjectName(QStringLiteral("commandInfoBodyText"));
    m_body->setReadOnly(true);
    m_body->setMinimumHeight(t.px(180));
    m_body->setStyleSheet(QStringLiteral("font-family: \"JetBrains Mono\", \"DejaVu Sans Mono\", monospace;"));
    m_body->setPlainText(tr("Loading…"));
    layout->addWidget(m_body, 1);
    layout->addWidget(section(tr("Change (optional)"), body));
    auto *row = new QHBoxLayout();
    m_toolsAction = new QComboBox(body);
    m_toolsAction->setObjectName(QStringLiteral("commandToolsAction"));
    m_toolsAction->addItem(tr("Leave as is"), QString());
    m_toolsAction->addItem(tr("Enable"), QStringLiteral("enable"));
    m_toolsAction->addItem(tr("Disable"), QStringLiteral("disable"));
    m_toolsNames = new QLineEdit(body);
    m_toolsNames->setObjectName(QStringLiteral("commandToolsNames"));
    m_toolsNames->setPlaceholderText(tr("Tool names, separated by spaces"));
    m_toolsNames->setClearButtonEnabled(true);
    row->addWidget(m_toolsAction);
    row->addWidget(m_toolsNames, 1);
    layout->addLayout(row);
    connect(m_toolsAction, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { updateSaveEnabled(); });
    connect(m_toolsNames, &QLineEdit::textChanged, this, [this](const QString &) { updateSaveEnabled(); });
}

void CommandHostDialog::buildPersonalityExtras() {
    auto *body = findChild<QWidget *>(QStringLiteral("commandBody"));
    auto *layout = qobject_cast<QVBoxLayout *>(body->layout());
    if (!layout) return;
    if (!m_text) {
        layout->addWidget(section(tr("Or type a name"), body));
        m_text = new QLineEdit(body);
        m_text->setObjectName(QStringLiteral("commandTextArgument"));
        m_text->setClearButtonEnabled(true);
        m_text->setPlaceholderText(tr("Personality name (or “none”)"));
        ui::describe(m_text, tr("Custom personality"), tr("Used if you don’t pick a name from the list"));
        layout->addWidget(m_text);
        connect(m_text, &QLineEdit::textChanged, this, [this](const QString &) { updateSaveEnabled(); });
    }
}

void CommandHostDialog::splitModelIdentity(const CommandPickerItem &item,
                                           QString *provider, QString *model, QString *id) {
    *id = item.value;
    *provider = item.group;
    *model = item.name.isEmpty() ? item.value : item.name;
    const int colon = item.value.indexOf(QLatin1Char(':'));
    if (provider->isEmpty() && colon > 0) {
        *provider = item.value.left(colon);
        if (item.name.isEmpty() || item.name == item.value) *model = item.value.mid(colon + 1);
    }
    const int dot = item.name.indexOf(QStringLiteral(" · "));
    if (dot > 0) {
        if (provider->isEmpty()) *provider = item.name.left(dot);
        *model = item.name.mid(dot + 3);
    }
}

void CommandHostDialog::setConfigOptions(const QList<AcpClient::ConfigOption> &options,
                                         const QStringList &availableModels) {
    if (m_host) m_host->seedConfig(options, availableModels);
}

void CommandHostDialog::refreshPickerPreview() { updateInfoPanel(); }

void CommandHostDialog::setPickerItems(const QList<CommandPickerItem> &items, const QString &currentValue) {
    m_items = items;
    m_currentValue = currentValue;
    if (m_selected.isEmpty()) m_selected = currentValue;
    rebuildPickerList();
    updateInfoPanel();
    updateSaveEnabled();
}

void CommandHostDialog::rebuildPickerList() {
    if (!m_list) return;
    const QString filter = m_filter ? m_filter->text().trimmed() : QString();
    const QString selected = m_selected.isEmpty() ? m_currentValue : m_selected;
    QSignalBlocker block(m_list);
    m_list->clear();

    // Group in the order first seen.
    QStringList groups;
    for (const auto &it : m_items) {
        const QString g = it.group;
        if (!groups.contains(g)) groups << g;
    }
    auto matches = [&](const CommandPickerItem &it) {
        if (filter.isEmpty()) return true;
        const QString hay = (it.name + QLatin1Char(' ') + it.value + QLatin1Char(' ') + it.group
                             + QLatin1Char(' ') + it.description).toLower();
        return hay.contains(filter.toLower());
    };

    QListWidgetItem *select = nullptr;
    for (const QString &group : groups) {
        QList<const CommandPickerItem *> rows;
        for (const auto &it : m_items) if (it.group == group && matches(it)) rows << &it;
        if (rows.isEmpty()) continue;
        if (!group.isEmpty() && (groups.size() > 1 || !groups.first().isEmpty())) {
            auto *header = new QListWidgetItem(group);
            header->setFlags(Qt::NoItemFlags);
            header->setData(Qt::UserRole, QString());
            QFont f = header->font();
            f.setBold(true);
            header->setFont(f);
            m_list->addItem(header);
        }
        for (const auto *it : rows) {
            const QString label = it->name.isEmpty() ? it->value : it->name;
            QString text = label;
            if (!it->value.isEmpty() && it->value != label)
                text += QStringLiteral("\n") + it->value;
            auto *row = new QListWidgetItem(text);
            row->setData(Qt::UserRole, it->value);
            row->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            if (it->value == selected) select = row;
            m_list->addItem(row);
        }
    }
    if (select) m_list->setCurrentItem(select);
    else if (m_list->count() > 0) {
        for (int i = 0; i < m_list->count(); ++i) {
            if (!m_list->item(i)->data(Qt::UserRole).toString().isEmpty()) {
                m_list->setCurrentItem(m_list->item(i));
                break;
            }
        }
    }
}

void CommandHostDialog::onPickerCurrentChanged() {
    if (!m_list) return;
    auto *item = m_list->currentItem();
    if (!item) return;
    const QString value = item->data(Qt::UserRole).toString();
    if (value.isEmpty()) return;
    m_selected = value;
    if (m_text && m_plan.name == QLatin1String("personality")) {
        QSignalBlocker b(m_text);
        m_text->setText(value);
    }
    updateInfoPanel();
    updateSaveEnabled();
}

void CommandHostDialog::updateInfoPanel() {
    if (!m_infoPanel) return;
    CommandPickerItem shown;
    const QString value = m_selected.isEmpty() ? m_currentValue : m_selected;
    for (const auto &it : m_items) if (it.value == value) { shown = it; break; }
    if (shown.value.isEmpty() && !value.isEmpty()) shown = {value, value, QString(), QString()};

    const bool preview = !m_currentValue.isEmpty() && value != m_currentValue;
    if (m_infoBadge) {
        m_infoBadge->setText(preview ? tr("Preview") : tr("Current"));
        m_infoBadge->setProperty("state", preview ? QStringLiteral("pending") : QStringLiteral("completed"));
        m_infoBadge->style()->unpolish(m_infoBadge);
        m_infoBadge->style()->polish(m_infoBadge);
    }
    QString provider, model, id;
    splitModelIdentity(shown, &provider, &model, &id);
    if (m_infoHeadline) {
        m_infoHeadline->setText(shown.name.isEmpty() ? (value.isEmpty() ? tr("Nothing selected") : value) : shown.name);
    }
    if (m_infoBody) {
        QStringList lines;
        if (!provider.isEmpty()) lines << tr("Provider  %1").arg(provider);
        if (!model.isEmpty() && model != provider) lines << tr("Model     %1").arg(model);
        if (!id.isEmpty() && id != model && id != shown.name) lines << tr("Id        %1").arg(id);
        if (!shown.description.isEmpty()) lines << shown.description;
        if (lines.isEmpty() && value.isEmpty()) lines << tr("Select an item below to preview it. Changes are applied only when you Save.");
        m_infoBody->setText(lines.join(QLatin1Char('\n')));
    }
}

void CommandHostDialog::setSessionTitle(const QString &title) {
    if (m_host) m_host->seedSessionTitle(title);
}

void CommandHostDialog::setBodyText(const QString &text) {
    if (!m_body) return;
    m_body->setPlainText(text);
    m_body->setVisible(true);
}

QString CommandHostDialog::bodyText() const {
    return m_body ? m_body->toPlainText() : QString();
}

void CommandHostDialog::setBusy(bool busy) {
    m_busy = busy;
    if (auto *spin = qobject_cast<ui::Spinner *>(m_spinner)) {
        spin->setVisible(busy);
        spin->setRunning(busy);
    }
    if (m_busyLabel) {
        m_busyLabel->setVisible(busy);
        m_busyLabel->setText(busy ? (m_applying ? tr("Saving…") : tr("Loading…")) : QString());
    }
    updateSaveEnabled();
}

QString CommandHostDialog::selectedValue() const {
    if (auto *live = dynamic_cast<LivePanelHost *>(m_host))
        return live->selectedValue();
    if (m_plan.name == QLatin1String("personality") && m_text && !m_text->text().trimmed().isEmpty())
        return m_text->text().trimmed();
    return m_selected;
}

void CommandHostDialog::setSelectedValue(const QString &value) {
    if (auto *live = dynamic_cast<LivePanelHost *>(m_host)) {
        live->setSelectedValue(value);
        return;
    }
    m_selected = value;
    if (m_list) {
        for (int i = 0; i < m_list->count(); ++i) {
            if (m_list->item(i)->data(Qt::UserRole).toString() == value) {
                m_list->setCurrentItem(m_list->item(i));
                break;
            }
        }
    }
    if (m_text && m_plan.name == QLatin1String("personality")) m_text->setText(value);
    updateInfoPanel();
    updateSaveEnabled();
}

QString CommandHostDialog::previewHeadline() const {
    return m_infoHeadline ? m_infoHeadline->text() : QString();
}

QString CommandHostDialog::previewBody() const {
    return m_infoBody ? m_infoBody->text() : QString();
}

bool CommandHostDialog::isDirty() const {
    // The command's host decides what a change is; the shell only reports it.
    return m_host && m_host->dirty();
}

bool CommandHostDialog::persistAsDefault() const { return m_persist && m_persist->isChecked(); }
void CommandHostDialog::setPersistAsDefault(bool on) { if (m_persist) m_persist->setChecked(on); }

int CommandHostDialog::undoCount() const { return m_undoSpin ? m_undoSpin->value() : 1; }
void CommandHostDialog::setUndoCount(int n) { if (m_undoSpin) m_undoSpin->setValue(n); }

QString CommandHostDialog::textArgument() const {
    if (auto *live = dynamic_cast<LivePanelHost *>(m_host))
        return live->textArgument();
    return m_text ? m_text->text().trimmed() : QString();
}
void CommandHostDialog::setTextArgument(const QString &text) {
    if (auto *live = dynamic_cast<LivePanelHost *>(m_host)) {
        live->setTextArgument(text);
        return;
    }
    if (m_text) m_text->setText(text);
}

bool CommandHostDialog::compressPreviewOnly() const { return m_previewOnly && m_previewOnly->isChecked(); }
void CommandHostDialog::setCompressPreviewOnly(bool on) { if (m_previewOnly) m_previewOnly->setChecked(on); }

bool CommandHostDialog::branchStayHere() const { return m_stayHere && m_stayHere->isChecked(); }
void CommandHostDialog::setBranchStayHere(bool on) { if (m_stayHere) m_stayHere->setChecked(on); }

QString CommandHostDialog::saveFormat() const {
    return m_saveFormat ? m_saveFormat->currentData().toString() : QStringLiteral("md");
}
void CommandHostDialog::setSaveFormat(const QString &format) {
    if (!m_saveFormat) return;
    const int i = m_saveFormat->findData(format);
    if (i >= 0) m_saveFormat->setCurrentIndex(i);
}
QString CommandHostDialog::saveFilename() const { return m_saveFile ? m_saveFile->text().trimmed() : QString(); }
void CommandHostDialog::setSaveFilename(const QString &name) { if (m_saveFile) m_saveFile->setText(name); }
bool CommandHostDialog::saveRedact() const { return m_saveRedact && m_saveRedact->isChecked(); }

QString CommandHostDialog::toolsAction() const {
    return m_toolsAction ? m_toolsAction->currentData().toString() : QString();
}
void CommandHostDialog::setToolsAction(const QString &action) {
    if (!m_toolsAction) return;
    const int i = m_toolsAction->findData(action);
    if (i >= 0) m_toolsAction->setCurrentIndex(i);
}
QString CommandHostDialog::toolsNames() const { return m_toolsNames ? m_toolsNames->text().trimmed() : QString(); }
void CommandHostDialog::setToolsNames(const QString &names) { if (m_toolsNames) m_toolsNames->setText(names); }

QString CommandHostDialog::commandLine() const {
    // Transport only: the exact line Hermes receives. The command's host owns
    // the argument syntax; the shell never renders it as UI text.
    return m_host ? m_host->line() : QString();
}

CommandApplyRequest CommandHostDialog::applyRequest() const {
    return m_host ? m_host->request() : CommandApplyRequest();
}

bool CommandHostDialog::canApply() const {
    if (m_busy || m_applying) return false;
    if (m_plan.archetype == CommandArchetype::Info) return true;
    return m_host && m_host->canApply();
}

QString CommandHostDialog::errorText() const { return m_error && m_error->isVisible() ? m_error->text() : QString(); }

void CommandHostDialog::setError(const QString &message) {
    if (!m_error) return;
    m_error->setText(message);
    m_error->setVisible(!message.isEmpty());
}

void CommandHostDialog::clearError() { setError(QString()); }

void CommandHostDialog::beginApply() {
    m_applying = true;
    setBusy(true);
    if (m_save) m_save->setEnabled(false);
    if (m_cancel) m_cancel->setEnabled(false);
}

void CommandHostDialog::releaseApply() {
    m_applying = false;
    setBusy(false);
    if (m_cancel) m_cancel->setEnabled(true);
    updateSaveEnabled();
}

void CommandHostDialog::endApply(bool ok, const QString &error) {
    m_applying = false;
    setBusy(false);
    if (m_cancel) m_cancel->setEnabled(true);
    if (ok) {
        clearError();
        accept();
    } else {
        setError(error.isEmpty() ? tr("Couldn’t apply that change.") : error);
        updateSaveEnabled();
    }
}

int CommandHostDialog::pickerCount() const {
    if (!m_list) return 0;
    int n = 0;
    for (int i = 0; i < m_list->count(); ++i)
        if (!m_list->item(i)->data(Qt::UserRole).toString().isEmpty()) ++n;
    return n;
}

void CommandHostDialog::updateSaveEnabled() {
    if (!m_save) return;
    if (m_refresh) m_refresh->setEnabled(!m_busy && !m_applying);
    if (m_plan.archetype == CommandArchetype::Info) {
        m_save->setEnabled(!m_applying);
        return;
    }
    m_save->setEnabled(canApply());
}

void CommandHostDialog::onSave() {
    if (m_plan.archetype == CommandArchetype::Info) {
        accept();
        return;
    }
    if (!canApply()) return;
    clearError();
    emit applyRequested(applyRequest());
}


QPushButton *CommandHostDialog::addChromeButton(const QString &label) {
    if (!m_buttons) return nullptr;
    return m_buttons->addButton(label, QDialogButtonBox::ActionRole);
}

void CommandHostDialog::setSaveText(const QString &text) {
    if (m_save) m_save->setText(text);
}

void CommandHostDialog::setSaveVisible(bool visible) {
    if (m_save) m_save->setVisible(visible);
}

void CommandHostDialog::submitApply(const CommandApplyRequest &request) {
    clearError();
    emit applyRequested(request);
}

static LivePanelHost *liveHost(CommandHost *host) {
    return dynamic_cast<LivePanelHost *>(host);
}

QString CommandHostDialog::liveAction() const {
    return liveHost(m_host) ? liveHost(m_host)->liveAction() : QString();
}
void CommandHostDialog::setLiveAction(const QString &action) {
    if (auto *h = liveHost(m_host)) h->setLiveAction(action);
}
QList<CommandPickerItem> CommandHostDialog::liveItems() const {
    return liveHost(m_host) ? liveHost(m_host)->liveItems() : QList<CommandPickerItem>();
}
void CommandHostDialog::setLiveItems(const QList<CommandPickerItem> &items, bool markClean) {
    if (auto *h = liveHost(m_host)) h->setLiveItems(items, markClean);
}
void CommandHostDialog::addLiveItem(const QString &text) {
    if (auto *h = liveHost(m_host)) h->addLiveItem(text);
}
void CommandHostDialog::removeSelectedLiveItem() {
    if (auto *h = liveHost(m_host)) h->removeSelectedLiveItem();
}
void CommandHostDialog::moveSelectedLiveItem(int delta) {
    if (auto *h = liveHost(m_host)) h->moveSelectedLiveItem(delta);
}
QString CommandHostDialog::intervalText() const {
    return liveHost(m_host) ? liveHost(m_host)->intervalText() : QString();
}
void CommandHostDialog::setIntervalText(const QString &text) {
    if (auto *h = liveHost(m_host)) h->setIntervalText(text);
}
int CommandHostDialog::insightDays() const {
    return liveHost(m_host) ? liveHost(m_host)->insightDays() : 7;
}
void CommandHostDialog::setInsightDays(int days) {
    if (auto *h = liveHost(m_host)) h->setInsightDays(days);
}
QString CommandHostDialog::diffMode() const {
    return liveHost(m_host) ? liveHost(m_host)->diffMode() : QString();
}
void CommandHostDialog::setDiffMode(const QString &mode) {
    if (auto *h = liveHost(m_host)) h->setDiffMode(mode);
}
bool CommandHostDialog::optionAll() const {
    return liveHost(m_host) && liveHost(m_host)->optionAll();
}
void CommandHostDialog::setOptionAll(bool on) {
    if (auto *h = liveHost(m_host)) h->setOptionAll(on);
}
bool CommandHostDialog::optionStat() const {
    return liveHost(m_host) && liveHost(m_host)->optionStat();
}
void CommandHostDialog::setOptionStat(bool on) {
    if (auto *h = liveHost(m_host)) h->setOptionStat(on);
}
void CommandHostDialog::ingestReport(const QString &command, const QString &output) {
    if (auto *h = liveHost(m_host)) h->ingestReport(command, output);
}
QStringList CommandHostDialog::applyLines() const {
    return liveHost(m_host) ? liveHost(m_host)->applyLines() : QStringList();
}

void CommandHostDialog::focusFirstField() {
    const auto want = [](QWidget *w) {
        return w && w->isVisible() && w->isEnabled() && w->focusPolicy() != Qt::NoFocus;
    };
    QWidget *candidates[] = { m_filter, m_text, m_list };
    for (QWidget *w : candidates) {
        if (want(w)) { w->setFocus(Qt::TabFocusReason); return; }
    }
    for (auto *w : findChildren<QComboBox *>()) {
        if (want(w)) { w->setFocus(Qt::TabFocusReason); return; }
    }
    for (auto *w : findChildren<QPlainTextEdit *>()) {
        if (want(w) && !w->isReadOnly()) { w->setFocus(Qt::TabFocusReason); return; }
    }
    for (auto *w : findChildren<QLineEdit *>()) {
        if (want(w)) { w->setFocus(Qt::TabFocusReason); return; }
    }
    if (m_save && m_save->isVisible() && m_save->isEnabled())
        m_save->setFocus(Qt::TabFocusReason);
}
