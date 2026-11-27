#include "commandhost.h"

#include "commandui.h"
#include "commandlivehosts.h"
#include "theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

namespace {

QString commandName(const CommandPlan &plan, const CommandCatalog::Command &cmd) {
    return cmd.name.isEmpty() ? plan.name : cmd.name;
}

AcpClient::ConfigOption matchingOption(const CommandPlan &plan,
                                       const QList<AcpClient::ConfigOption> &options) {
    if (!plan.configId.isEmpty()) {
        for (const auto &o : options)
            if (o.id == plan.configId) return o;
    }
    if (!plan.configCategory.isEmpty()) {
        for (const auto &o : options)
            if (o.category == plan.configCategory || o.id == plan.configCategory) return o;
    }
    return {};
}

QList<CommandPickerItem> itemsFromOption(const AcpClient::ConfigOption &opt,
                                         const QStringList &availableModels) {
    QList<CommandPickerItem> items;
    QStringList seen;
    for (const auto &c : opt.choices) {
        items.push_back({c.value, c.name.isEmpty() ? c.value : c.name, c.description, c.group});
        seen << c.value;
    }
    for (const QString &id : availableModels) {
        if (id.isEmpty() || seen.contains(id)) continue;
        items.push_back({id, id, QString(), QString()});
        seen << id;
    }
    if (!opt.currentValue.isEmpty() && !seen.contains(opt.currentValue))
        items.push_front({opt.currentValue, opt.currentValue, QString(), QString()});
    return items;
}

}  // namespace

// ─── CommandHost defaults ─────────────────────────────────────────────────

QString CommandHost::name() const { return commandName(m_plan, m_command); }

QString CommandHost::slash() const { return QLatin1Char('/') + name(); }

CommandApplyRequest CommandHost::request() const {
    CommandApplyRequest req;
    req.method = m_plan.apply;
    req.configId = m_plan.configId;
    req.quietLine = line();
    if (m_plan.apply == CommandApply::ConfigOption && req.configId.isEmpty())
        req.configId = m_plan.configCategory;
    return req;
}

// ─── Generic hosts: one per archetype ─────────────────────────────────────

// Picker: the reference surface. Info panel on top, exclusive list below,
// preview on select, Save applies. Used by model, reasoning, fast, approvals
// and personality; the difference between an ACP-backed picker and a
// quiet-command picker is only the plan's apply channel.
class PickerHost : public CommandHost {
public:
    PickerHost(const CommandPlan &plan, const CommandCatalog::Command &cmd)
        : CommandHost(plan, cmd) {}

    void build(CommandHostDialog &shell) override {
        m_shell = &shell;
        shell.buildPickerBody();
        // personality can also be a free-form name, printed under the list.
        if (name() == QLatin1String("personality")) shell.buildPersonalityExtras();
    }

    void seedReport(const QString &report) override {
        if (!m_shell) return;
        const auto items = CommandUi::itemsFromReport(report);
        if (!items.isEmpty()) {
            QStringList known;
            for (const auto &it : items) known << it.value;
            m_shell->setPickerItems(items, CommandUi::currentFromReport(report, known));
        } else if (m_shell->pickerCount() == 0 && !report.trimmed().isEmpty()) {
            m_shell->setBodyText(report);
        }
    }

    void seedConfig(const QList<AcpClient::ConfigOption> &options,
                    const QStringList &models) override {
        if (!m_shell) return;
        const auto opt = matchingOption(plan(), options);
        if (opt.id.isEmpty() && models.isEmpty() && command().choices().isEmpty()) {
            m_shell->refreshPickerPreview();
            return;
        }
        QList<CommandPickerItem> items = itemsFromOption(opt, models);
        if (items.isEmpty()) {
            for (const QString &c : command().choices())
                items.push_back({c, c, QString(), QString()});
        }
        m_shell->setPickerItems(items, opt.currentValue);
    }

    bool dirty() const override {
        if (!m_shell) return false;
        const QString selected = m_shell->selectedValue();
        if (m_shell->persistAsDefault() && !selected.isEmpty()) return true;
        const QString current = m_shell->currentPickerValue();
        if (current.isEmpty()) return !selected.isEmpty();
        return selected != current;
    }

    QString line() const override {
        QString l = slash();
        const QString value = m_shell ? m_shell->selectedValue() : QString();
        if (!value.isEmpty()) l += QLatin1Char(' ') + value;
        if (name() == QLatin1String("model")) {
            const bool persist = m_shell && m_shell->persistAsDefault();
            l += persist ? QStringLiteral(" --global") : QStringLiteral(" --session");
        }
        return l;
    }

    CommandApplyRequest request() const override {
        CommandApplyRequest req = CommandHost::request();
        if (m_shell) req.value = m_shell->selectedValue();
        if (name() == QLatin1String("model") && m_shell && m_shell->persistAsDefault()
            && !req.value.isEmpty()) {
            req.persistLine = QStringLiteral("/model %1 --global").arg(req.value);
        }
        return req;
    }
};

// Info: a read-only report. Body text arrives from the quiet command (or, in
// time, from a typed renderer); Close is always available.
class InfoHost : public CommandHost {
public:
    InfoHost(const CommandPlan &plan, const CommandCatalog::Command &cmd)
        : CommandHost(plan, cmd) {}
    void build(CommandHostDialog &shell) override {
        m_shell = &shell;
        shell.buildInfoBody();
    }
    void seedReport(const QString &report) override {
        if (m_shell) m_shell->setBodyText(report);
    }
    bool dirty() const override { return true; }  // Close is always available
    QString line() const override { return slash(); }
};

// Confirm: one sentence about the effect, Confirm/Cancel, applies on Confirm.
class ConfirmHost : public CommandHost {
public:
    ConfirmHost(const CommandPlan &plan, const CommandCatalog::Command &cmd)
        : CommandHost(plan, cmd) {}
    void build(CommandHostDialog &shell) override {
        m_shell = &shell;
        shell.buildConfirmBody();
    }
    bool dirty() const override { return true; }
    QString line() const override { return slash(); }
};

// Text prompt: one field (plus the syntax hint) and Save.
// Also the generic fallback for a Form command with no host of its own.
class TextPromptHost : public CommandHost {
public:
    TextPromptHost(const CommandPlan &plan, const CommandCatalog::Command &cmd)
        : CommandHost(plan, cmd) {}
    void build(CommandHostDialog &shell) override {
        m_shell = &shell;
        shell.buildTextPromptBody();
    }
    void seedSessionTitle(const QString &title) override {
        // /title opens with the session's current title already in the field.
        if (!m_shell || name() != QLatin1String("title")) return;
        if (m_shell->textArgument().isEmpty()) m_shell->setTextArgument(title);
    }
    bool dirty() const override { return m_shell && !m_shell->textArgument().isEmpty(); }
    QString line() const override {
        const QString value = m_shell ? m_shell->textArgument() : QString();
        return value.isEmpty() ? slash() : slash() + QLatin1Char(' ') + value;
    }
    CommandApplyRequest request() const override {
        CommandApplyRequest req = CommandHost::request();
        if (m_shell) req.value = m_shell->textArgument();
        return req;
    }
};

// ─── Commands with a form of their own ────────────────────────────────────
//
// Each is a class, not a branch: adding the next one is a class plus a row in
// createCommandHost(), and it cannot accidentally inherit another command's
// assumptions about arguments.

// /undo [N] — how many of your turns to drop.
class UndoHost : public CommandHost {
public:
    UndoHost(const CommandPlan &plan, const CommandCatalog::Command &cmd)
        : CommandHost(plan, cmd) {}
    void build(CommandHostDialog &shell) override {
        m_shell = &shell;
        shell.buildUndoForm();
    }
    bool dirty() const override { return true; }
    QString line() const override {
        const int n = m_shell ? m_shell->undoCount() : 1;
        return n <= 1 ? slash() : QStringLiteral("%1 %2").arg(slash()).arg(n);
    }
};

// /compress [--preview] [focus <topic>] — shrink the context window.
class CompressHost : public CommandHost {
public:
    CompressHost(const CommandPlan &plan, const CommandCatalog::Command &cmd)
        : CommandHost(plan, cmd) {}
    void build(CommandHostDialog &shell) override {
        m_shell = &shell;
        shell.buildCompressForm();
    }
    bool dirty() const override { return true; }
    QString line() const override {
        if (!m_shell) return slash();
        QString l = slash();
        if (m_shell->compressPreviewOnly()) l += QStringLiteral(" --preview");
        const QString focus = m_shell->textArgument();
        if (!focus.isEmpty()) l += QStringLiteral(" focus ") + focus;
        return l;
    }
};

// /branch [--here] [name] — fork this session.
class BranchHost : public CommandHost {
public:
    BranchHost(const CommandPlan &plan, const CommandCatalog::Command &cmd)
        : CommandHost(plan, cmd) {}
    void build(CommandHostDialog &shell) override {
        m_shell = &shell;
        shell.buildBranchForm();
    }
    bool dirty() const override { return true; }
    QString line() const override {
        if (!m_shell) return slash();
        QString l = slash();
        if (m_shell->branchStayHere()) l += QStringLiteral(" --here");
        const QString name = m_shell->textArgument();
        if (!name.isEmpty()) l += QLatin1Char(' ') + name;
        return l;
    }
};

// /save <fmt> [file] [redact] — export the conversation.
class SaveHost : public CommandHost {
public:
    SaveHost(const CommandPlan &plan, const CommandCatalog::Command &cmd)
        : CommandHost(plan, cmd) {}
    void build(CommandHostDialog &shell) override {
        m_shell = &shell;
        shell.buildSaveForm();
    }
    bool dirty() const override { return true; }
    QString line() const override {
        if (!m_shell) return slash();
        QString l = slash() + QLatin1Char(' ') + m_shell->saveFormat();
        if (!m_shell->saveFilename().isEmpty()) l += QLatin1Char(' ') + m_shell->saveFilename();
        if (m_shell->saveRedact()) l += QStringLiteral(" redact");
        return l;
    }
};

// /tools [enable|disable <names…>] — report, with an optional change.
class ToolsHost : public CommandHost {
public:
    ToolsHost(const CommandPlan &plan, const CommandCatalog::Command &cmd)
        : CommandHost(plan, cmd) {}
    void build(CommandHostDialog &shell) override {
        m_shell = &shell;
        shell.buildToolsForm();
    }
    void seedReport(const QString &report) override {
        if (m_shell) m_shell->setBodyText(report);
    }
    bool dirty() const override {
        return m_shell && !m_shell->toolsAction().isEmpty() && !m_shell->toolsNames().isEmpty();
    }
    QString line() const override {
        if (!m_shell || m_shell->toolsAction().isEmpty()) return slash();
        return QStringLiteral("%1 %2 %3").arg(slash(), m_shell->toolsAction(), m_shell->toolsNames()).trimmed();
    }
};


// /export [profile] [-o path] — share a profile as a .tar.gz archive.
class ExportHost : public CommandHost {
public:
    ExportHost(const CommandPlan &plan, const CommandCatalog::Command &cmd)
        : CommandHost(plan, cmd) {}
    void build(CommandHostDialog &shell) override {
        m_shell = &shell;
        shell.buildExportForm();
    }
    bool dirty() const override { return true; }
    QString line() const override {
        if (!m_shell) return slash();
        QString l = slash();
        if (!m_shell->profileName().isEmpty()) l += QLatin1Char(' ') + m_shell->profileName();
        if (!m_shell->archivePath().isEmpty())
            l += QStringLiteral(" -o ") + m_shell->archivePath();
        return l;
    }
};

// /import <archive.tar.gz> [--name <name>] — import a shared profile archive.
class ImportHost : public CommandHost {
public:
    ImportHost(const CommandPlan &plan, const CommandCatalog::Command &cmd)
        : CommandHost(plan, cmd) {}
    void build(CommandHostDialog &shell) override {
        m_shell = &shell;
        shell.buildImportForm();
    }
    bool dirty() const override { return m_shell && !m_shell->archivePath().isEmpty(); }
    QString line() const override {
        if (!m_shell || m_shell->archivePath().isEmpty()) return slash();
        QString l = slash() + QLatin1Char(' ') + m_shell->archivePath();
        if (!m_shell->profileName().isEmpty())
            l += QStringLiteral(" --name ") + m_shell->profileName();
        return l;
    }
};

// ─── Factory ──────────────────────────────────────────────────────────────

CommandHost *createCommandHost(const CommandPlan &plan, const CommandCatalog::Command &command) {
    const QString n = commandName(plan, command);

    // Commands whose body is their own shape.
    if (n == QLatin1String("undo")) return new UndoHost(plan, command);
    if (n == QLatin1String("compress")) return new CompressHost(plan, command);
    if (n == QLatin1String("branch")) return new BranchHost(plan, command);
    if (n == QLatin1String("save")) return new SaveHost(plan, command);
    if (n == QLatin1String("export")) return new ExportHost(plan, command);
    if (n == QLatin1String("import")) return new ImportHost(plan, command);
    if (n == QLatin1String("tools")) return new ToolsHost(plan, command);
    if (n == QLatin1String("usage")) return new UsageHost(plan, command);
    if (n == QLatin1String("goal") || n == QLatin1String("subgoal")) return new GoalHost(plan, command);
    if (n == QLatin1String("heartbeat")) return new HeartbeatHost(plan, command);
    if (n == QLatin1String("loop")) return new LoopHost(plan, command);
    if (n == QLatin1String("queue")) return new QueueHost(plan, command);
    if (n == QLatin1String("agents")) return new AgentsHost(plan, command);
    if (n == QLatin1String("memory")) return new MemoryHost(plan, command);
    if (n == QLatin1String("skills")) return new SkillsHost(plan, command);
    if (n == QLatin1String("rollback")) return new RollbackHost(plan, command);
    if (n == QLatin1String("snapshot")) return new SnapshotHost(plan, command);
    if (n == QLatin1String("debug")) return new DebugHost(plan, command);
    if (n == QLatin1String("insights")) return new InsightsHost(plan, command);
    if (n == QLatin1String("diff")) return new DiffHost(plan, command);
    if (n == QLatin1String("cron")) return new CronHost(plan, command);
    if (n == QLatin1String("suggestions")) return new SuggestionsHost(plan, command);
    if (n == QLatin1String("blueprint")) return new BlueprintHost(plan, command);
    if (n == QLatin1String("busy"))
        return new ChoicePickerHost(plan, command, {
            {QStringLiteral("queue"), CommandHostDialog::tr("Queue"),
             CommandHostDialog::tr("Hold new messages until this turn finishes")},
            {QStringLiteral("steer"), CommandHostDialog::tr("Steer"),
             CommandHostDialog::tr("Treat new messages as mid-turn guidance")},
            {QStringLiteral("interrupt"), CommandHostDialog::tr("Interrupt"),
             CommandHostDialog::tr("Stop the current turn when a new message arrives")},
        });
    if (n == QLatin1String("autoprune"))
        return new ChoicePickerHost(plan, command, {
            {QStringLiteral("on"), CommandHostDialog::tr("On"),
             CommandHostDialog::tr("Automatically prune old sessions")},
            {QStringLiteral("off"), CommandHostDialog::tr("Off"),
             CommandHostDialog::tr("Keep old sessions until you delete them")},
            {QStringLiteral("later"), CommandHostDialog::tr("Later"),
             CommandHostDialog::tr("Ask again later")},
        });
    if (n == QLatin1String("codex-runtime"))
        return new ChoicePickerHost(plan, command, {
            {QStringLiteral("auto"), CommandHostDialog::tr("Auto"),
             CommandHostDialog::tr("Let Hermes pick the runtime")},
            {QStringLiteral("codex_app_server"), CommandHostDialog::tr("Codex app server"),
             CommandHostDialog::tr("Use the Codex app-server runtime")},
        });
    if (n == QLatin1String("subscription") || n == QLatin1String("topup"))
        return new BillingHost(plan, command);
    if (n == QLatin1String("init")) return new InitHost(plan, command);
    if (n == QLatin1String("resume")) return new ResumeHost(plan, command);
    if (n == QLatin1String("bundles") || n == QLatin1String("plugins")
        || n == QLatin1String("toolsets") || n == QLatin1String("curator")
        || n == QLatin1String("kanban"))
        return new ViewReportHost(plan, command);

    // Everything else is handled by the archetype's generic host, so a command
    // this build has never heard of still gets a real surface.
    switch (plan.archetype) {
    case CommandArchetype::Picker: return new PickerHost(plan, command);
    case CommandArchetype::Info: return new InfoHost(plan, command);
    case CommandArchetype::Confirm: return new ConfirmHost(plan, command);
    case CommandArchetype::TextPrompt: return new TextPromptHost(plan, command);
    case CommandArchetype::Form: return new TextPromptHost(plan, command);  // unmodelled form
    case CommandArchetype::LivePanel:
        // Named hosts above cover every live-panel plan; an unexpected name
        // still gets a dialog rather than falling through to the console.
        return new AgentsHost(plan, command);
    case CommandArchetype::AppLink:
    case CommandArchetype::Passthrough:
        break;
    }
    return nullptr;  // no dialog: existing surface or Command console
}
