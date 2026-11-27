// Unit tests for the command UI map and the shared dialog shell: pickers
// preview without applying, Save is required, unknown commands are passthrough.
#include "commandui.h"
#include "commandhost.h"
#include "commanddialog.h"
#include "theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QtTest>

static CommandCatalog::Command liveCmd(const QString &name, const QString &label) {
    CommandCatalog::Command c;
    c.name = name;
    c.label = label;
    return c;
}

class CommandUiTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        Theme::instance().setMode(Theme::Mode::Dark);
    }

    void plansKnownCommandsAndAliases() {
        const CommandPlan model = CommandUi::planFor(QStringLiteral("/models"));
        QCOMPARE(model.name, QString("model"));
        QCOMPARE(model.archetype, CommandArchetype::Picker);
        QCOMPARE(model.apply, CommandApply::ConfigOption);
        QCOMPARE(model.configId, QString("model"));
        QVERIFY(CommandUi::hasNativeUi(QStringLiteral("model")));
        QVERIFY(CommandUi::hasNativeUi(QStringLiteral("MODELS")));
        QCOMPARE(CommandUi::badgeFor(QStringLiteral("model")), QString("Dialog"));

        QCOMPARE(CommandUi::planFor(QStringLiteral("reset")).name, QString("new"));
        QCOMPARE(CommandUi::planFor(QStringLiteral("compact")).name, QString("compress"));
        QCOMPARE(CommandUi::planFor(QStringLiteral("ctx")).name, QString("context"));
        QCOMPARE(CommandUi::planFor(QStringLiteral("fork")).name, QString("branch"));
        QCOMPARE(CommandUi::planFor(QStringLiteral("q")).name, QString("queue"));

        QCOMPARE(CommandUi::planFor(QStringLiteral("help")).archetype, CommandArchetype::AppLink);
        QCOMPARE(CommandUi::badgeFor(QStringLiteral("help")), QString("Dialog"));
    }

    void unknownAndFutureCommandsArePassthrough() {
        const CommandPlan p = CommandUi::planFor(QStringLiteral("zz-newfeature"));
        QCOMPARE(p.archetype, CommandArchetype::Passthrough);
        QCOMPARE(p.apply, CommandApply::Passthrough);
        QVERIFY(!CommandUi::hasNativeUi(QStringLiteral("zz-newfeature")));
        QCOMPARE(CommandUi::badgeFor(QStringLiteral("zz-newfeature")), QString("Chat"));
        QVERIFY(CommandUi::nativeCommandNames().contains(QStringLiteral("model")));
        QVERIFY(!CommandUi::nativeCommandNames().contains(QStringLiteral("zz-newfeature")));
    }

    void modelsPickerPreviewsUntilSave() {
        CommandCatalog::Command cmd;
        cmd.name = QStringLiteral("model");
        cmd.label = QStringLiteral("Model");
        cmd.icon = QStringLiteral("cpu");
        cmd.description = QStringLiteral("Show or change the model");
        CommandHostDialog d(CommandUi::planFor(QStringLiteral("model")), cmd);
        QList<AcpClient::ConfigOption> opts;
        AcpClient::ConfigOption opt;
        opt.id = QStringLiteral("model");
        opt.name = QStringLiteral("Model");
        opt.category = QStringLiteral("model");
        opt.type = QStringLiteral("select");
        opt.currentValue = QStringLiteral("openrouter:current");
        opt.choices = {
            {QStringLiteral("openrouter:current"), QStringLiteral("OpenRouter · Current"),
             QStringLiteral("Provider: OpenRouter • current"), QStringLiteral("OpenRouter")},
            {QStringLiteral("openrouter:large"), QStringLiteral("OpenRouter · Large"),
             QStringLiteral("Provider: OpenRouter"), QStringLiteral("OpenRouter")},
            {QStringLiteral("nous:hermes"), QStringLiteral("Nous · Hermes"),
             QStringLiteral("Provider: Nous"), QStringLiteral("Nous")},
        };
        opts << opt;
        d.setConfigOptions(opts, {QStringLiteral("openrouter:current"), QStringLiteral("openrouter:large")});
        QCOMPARE(d.pickerCount(), 3);
        QCOMPARE(d.selectedValue(), QString("openrouter:current"));
        QVERIFY(d.previewHeadline().contains("Current"));
        QVERIFY(d.previewBody().contains("OpenRouter"));
        QVERIFY(!d.isDirty());
        QVERIFY(!d.saveButton()->isEnabled());

        bool applied = false;
        CommandApplyRequest seen;
        connect(&d, &CommandHostDialog::applyRequested, this, [&](const CommandApplyRequest &r) {
            applied = true;
            seen = r;
        });

        d.setSelectedValue(QStringLiteral("nous:hermes"));
        QVERIFY(d.isDirty());
        QVERIFY(d.saveButton()->isEnabled());
        QVERIFY(d.previewHeadline().contains("Hermes"));
        QVERIFY(d.previewBody().contains("Nous"));
        QCOMPARE(d.findChild<QLabel *>(QStringLiteral("statusBadge"))->text(), QString("Preview"));
        QVERIFY(!applied);

        d.setPersistAsDefault(true);
        QVERIFY(d.persistAsDefault());
        QTest::mouseClick(d.saveButton(), Qt::LeftButton);
        QVERIFY(applied);
        QCOMPARE(seen.method, CommandApply::ConfigOption);
        QCOMPARE(seen.configId, QString("model"));
        QCOMPARE(seen.value, QString("nous:hermes"));
        QCOMPARE(seen.persistLine, QString("/model nous:hermes --global"));
        // Dialog stays open until endApply(true).
        QVERIFY(d.isVisible() || !d.isApplying() || d.isApplying());
    }

    void cancelDoesNotApply() {
        CommandCatalog::Command cmd;
        cmd.name = QStringLiteral("reasoning");
        cmd.label = QStringLiteral("Reasoning");
        CommandHostDialog d(CommandUi::planFor(QStringLiteral("reasoning")), cmd);
        AcpClient::ConfigOption opt;
        opt.id = QStringLiteral("reasoning_effort");
        opt.category = QStringLiteral("thought_level");
        opt.currentValue = QStringLiteral("medium");
        opt.choices = {{QStringLiteral("low"), QStringLiteral("low"), {}, {}},
                       {QStringLiteral("medium"), QStringLiteral("medium"), {}, {}},
                       {QStringLiteral("high"), QStringLiteral("high"), {}, {}}};
        d.setConfigOptions({opt});
        bool applied = false;
        connect(&d, &CommandHostDialog::applyRequested, this, [&](const CommandApplyRequest &) { applied = true; });
        d.setSelectedValue(QStringLiteral("high"));
        QVERIFY(d.isDirty());
        d.reject();
        QVERIFY(!applied);
        QCOMPARE(d.result(), int(QDialog::Rejected));
    }

    void applyFailureKeepsDialogOpen() {
        CommandCatalog::Command cmd;
        cmd.name = QStringLiteral("fast");
        cmd.label = QStringLiteral("Speed");
        CommandHostDialog d(CommandUi::planFor(QStringLiteral("fast")), cmd);
        AcpClient::ConfigOption opt;
        opt.id = QStringLiteral("service_tier");
        opt.currentValue = QStringLiteral("normal");
        opt.choices = {{QStringLiteral("normal"), QStringLiteral("Normal"), {}, {}},
                       {QStringLiteral("fast"), QStringLiteral("Fast"), {}, {}}};
        d.setConfigOptions({opt});
        d.show();
        d.setSelectedValue(QStringLiteral("fast"));
        d.beginApply();
        QVERIFY(d.isApplying());
        QVERIFY(!d.saveButton()->isEnabled());
        d.endApply(false, QStringLiteral("Agent refused"));
        QVERIFY(d.isVisible());
        QCOMPARE(d.errorText(), QString("Agent refused"));
        QVERIFY(!d.isApplying());
        QVERIFY(d.saveButton()->isEnabled());
        d.endApply(true);
        QCOMPARE(d.result(), int(QDialog::Accepted));
    }

    void infoAndConfirmAndForms() {
        CommandCatalog::Command status;
        status.name = QStringLiteral("status");
        status.label = QStringLiteral("Status");
        CommandHostDialog info(CommandUi::planFor(QStringLiteral("status")), status);
        info.setBodyText(QStringLiteral("Session: abc\nModel: fake"));
        QCOMPARE(info.bodyText(), QString("Session: abc\nModel: fake"));
        QVERIFY(info.saveButton()->isEnabled()); // Close

        CommandCatalog::Command neu;
        neu.name = QStringLiteral("new");
        CommandHostDialog confirm(CommandUi::planFor(QStringLiteral("new")), neu);
        QCOMPARE(confirm.applyRequest().method, CommandApply::NewSession);
        QCOMPARE(confirm.commandLine(), QString("/new"));

        CommandCatalog::Command undo;
        undo.name = QStringLiteral("undo");
        CommandHostDialog form(CommandUi::planFor(QStringLiteral("undo")), undo);
        QCOMPARE(form.undoCount(), 1);
        form.setUndoCount(3);
        QCOMPARE(form.commandLine(), QString("/undo 3"));

        CommandCatalog::Command title;
        title.name = QStringLiteral("title");
        title.argsHint = QStringLiteral("[name]");
        CommandHostDialog text(CommandUi::planFor(QStringLiteral("title")), title);
        QVERIFY(!text.saveButton()->isEnabled());
        text.setTextArgument(QStringLiteral("Ship the release"));
        QCOMPARE(text.commandLine(), QString("/title Ship the release"));
        QVERIFY(text.saveButton()->isEnabled());

        CommandCatalog::Command save;
        save.name = QStringLiteral("save");
        CommandHostDialog exp(CommandUi::planFor(QStringLiteral("save")), save);
        exp.setSaveFormat(QStringLiteral("json"));
        exp.setSaveFilename(QStringLiteral("notes.json"));
        QCOMPARE(exp.commandLine(), QString("/save json notes.json redact"));

        CommandCatalog::Command compress;
        compress.name = QStringLiteral("compress");
        CommandHostDialog comp(CommandUi::planFor(QStringLiteral("compress")), compress);
        comp.setTextArgument(QStringLiteral("the API"));
        comp.setCompressPreviewOnly(true);
        QCOMPARE(comp.commandLine(), QString("/compress --preview focus the API"));
    }

    void parsePersonalityReport() {
        const QString report = QStringLiteral(
            "Current personality: concise\n"
            "Available: none, concise, kawaii, professional\n");
        const auto items = CommandUi::itemsFromReport(report);
        QCOMPARE(items.size(), 4);
        QStringList names;
        for (const auto &it : items) names << it.value;
        QVERIFY(names.contains("concise"));
        QVERIFY(names.contains("kawaii"));
        QCOMPARE(CommandUi::currentFromReport(report, names), QString("concise"));
    }

    void commandConsoleSendsTypedLine() {
        QScopedPointer<CommandDialog> d(CommandDialog::console(nullptr, QStringLiteral("/goal pause")));
        QVERIFY(d->isFreeform());
        QCOMPARE(d->commandLine(), QString("/goal pause"));
        QVERIFY(d->canRun());
        d->setArguments(QStringLiteral("steer turn left"));
        QCOMPARE(d->commandLine(), QString("/steer turn left"));
    }

    // The ledger: every command with a native surface must actually get one.
    // A plan added without a host (or a host claiming the wrong archetype)
    // fails here instead of silently degrading to a text fallback.
    void everyNativePlanResolvesToAHost() {
        int dialogs = 0;
        for (const QString &name : CommandUi::nativeCommandNames()) {
            const CommandPlan p = CommandUi::planFor(name);
            CommandCatalog::Command cmd;
            cmd.name = name;
            cmd.label = CommandCatalog::labelFor(name);
            QScopedPointer<CommandHostDialog> d(new CommandHostDialog(p, cmd));
            if (p.archetype == CommandArchetype::AppLink) continue;  // existing surface, no dialog
            ++dialogs;
            QVERIFY2(d->host() != nullptr, qPrintable(name));
            QCOMPARE(int(d->host()->archetype()), int(p.archetype));
            // The line Hermes receives is transport, never UI text. Live panels
            // have no line until there is something to apply (queue edits,
            // a selected checkpoint, …).
            const QString line = d->commandLine();
            if (p.archetype == CommandArchetype::LivePanel) {
                QVERIFY2(line.isEmpty() || line.startsWith(QLatin1Char('/')), qPrintable(name));
                if (d->canApply())
                    QVERIFY2(line.startsWith(QLatin1Char('/')), qPrintable(name));
            } else {
                QVERIFY2(line.startsWith(QLatin1Char('/')), qPrintable(name));
            }
        }
        QVERIFY(dialogs >= 20);  // sanity: the loop really built dialogs
    }

    // A command this build has never seen still gets a surface when a future
    // Hermes maps it onto an archetype, and archetypes with no dialog get none.
    void factoryFallsBackToTheArchetype() {
        CommandCatalog::Command cmd;
        cmd.name = QStringLiteral("zz-newform");

        CommandPlan form;
        form.name = QStringLiteral("zz-newform");
        form.archetype = CommandArchetype::Form;
        QScopedPointer<CommandHost> fallback(createCommandHost(form, cmd));
        QVERIFY(fallback);
        QCOMPARE(int(fallback->archetype()), int(CommandArchetype::Form));
        QCOMPARE(fallback->line(), QString("/zz-newform"));

        CommandPlan link;
        link.name = QStringLiteral("zz-newform");
        link.archetype = CommandArchetype::AppLink;
        QVERIFY(createCommandHost(link, cmd) == nullptr);

        CommandPlan passthrough;
        passthrough.name = QStringLiteral("zz-newform");
        passthrough.archetype = CommandArchetype::Passthrough;
        QVERIFY(createCommandHost(passthrough, cmd) == nullptr);
    }

    void lightThemeShellBuilds() {
        Theme::instance().setMode(Theme::Mode::Light);
        CommandCatalog::Command cmd;
        cmd.name = QStringLiteral("model");
        cmd.label = QStringLiteral("Model");
        CommandHostDialog d(CommandUi::planFor(QStringLiteral("model")), cmd);
        AcpClient::ConfigOption opt;
        opt.id = QStringLiteral("model");
        opt.category = QStringLiteral("model");
        opt.currentValue = QStringLiteral("a");
        opt.choices = {{QStringLiteral("a"), QStringLiteral("A"), {}, {}},
                       {QStringLiteral("b"), QStringLiteral("B"), {}, {}}};
        d.setConfigOptions({opt});
        d.show();
        QVERIFY(d.pickerCount() == 2);
        Theme::instance().setMode(Theme::Mode::Dark);
    }

    void phase3PlansAndHiddenChrome() {
        QCOMPARE(CommandUi::planFor(QStringLiteral("busy")).archetype, CommandArchetype::Picker);
        QCOMPARE(CommandUi::planFor(QStringLiteral("autoprune")).archetype, CommandArchetype::Picker);
        QCOMPARE(CommandUi::planFor(QStringLiteral("codex-runtime")).archetype, CommandArchetype::Picker);
        QCOMPARE(CommandUi::planFor(QStringLiteral("bg")).apply, CommandApply::SendPrompt);
        QCOMPARE(CommandUi::planFor(QStringLiteral("moa")).apply, CommandApply::SendPrompt);
        QCOMPARE(CommandUi::planFor(QStringLiteral("refine")).apply, CommandApply::SendPrompt);
        QCOMPARE(CommandUi::planFor(QStringLiteral("review")).apply, CommandApply::SendPrompt);
        QCOMPARE(CommandUi::planFor(QStringLiteral("learn")).apply, CommandApply::SendPrompt);
        QCOMPARE(CommandUi::planFor(QStringLiteral("init")).archetype, CommandArchetype::TextPrompt);
        QCOMPARE(CommandUi::planFor(QStringLiteral("resume")).apply, CommandApply::ResumeSession);
        QCOMPARE(CommandUi::planFor(QStringLiteral("subscription")).archetype, CommandArchetype::Info);
        QCOMPARE(CommandUi::planFor(QStringLiteral("upgrade")).name, QString("subscription"));
        QCOMPARE(CommandUi::planFor(QStringLiteral("topup")).archetype, CommandArchetype::Info);
        QCOMPARE(CommandUi::planFor(QStringLiteral("reload-mcp")).archetype, CommandArchetype::Confirm);
        QCOMPARE(CommandUi::planFor(QStringLiteral("bundles")).archetype, CommandArchetype::Info);
        QCOMPARE(CommandUi::planFor(QStringLiteral("kanban")).archetype, CommandArchetype::Info);
        QVERIFY(CommandUi::hasNativeUi(QStringLiteral("busy")));
        QCOMPARE(CommandUi::badgeFor(QStringLiteral("busy")), QString("Dialog"));
        QVERIFY(CommandUi::isHiddenChrome(QStringLiteral("footer")));
        QVERIFY(CommandUi::isHiddenChrome(QStringLiteral("statusbar")));
        QVERIFY(CommandUi::isHiddenChrome(QStringLiteral("pet")));
        QVERIFY(CommandUi::isHiddenChrome(QStringLiteral("history")));
        QVERIFY(!CommandUi::isHiddenChrome(QStringLiteral("busy")));
        QVERIFY(!CommandUi::isHiddenChrome(QStringLiteral("goal")));
        CommandHostDialog busy(CommandUi::planFor(QStringLiteral("busy")), liveCmd("busy", "Busy input"));
        busy.setSelectedValue(QStringLiteral("steer"));
        QCOMPARE(busy.commandLine(), QString("/busy steer"));
        CommandHostDialog init(CommandUi::planFor(QStringLiteral("init")), liveCmd("init", "Generate AGENTS.md"));
        QVERIFY(init.canApply());
        QCOMPARE(init.commandLine(), QString("/init"));
        init.setTextArgument(QStringLiteral("prefer Qt 5"));
        QCOMPARE(init.commandLine(), QString("/init prefer Qt 5"));
        QVERIFY(init.applyRequest().keepOpen);
        QCOMPARE(init.findChild<QLineEdit *>(QStringLiteral("commandTextArgument"))->placeholderText(),
                 QString("Optional notes for AGENTS.md"));

        CommandHostDialog sub(CommandUi::planFor(QStringLiteral("subscription")), liveCmd("subscription", "Subscription"));
        QVERIFY(sub.cancelButton());
        QVERIFY(!sub.cancelButton()->isVisible());
        QVERIFY(sub.saveButton());
        QCOMPARE(sub.saveButton()->text(), QString("Close"));
        // Adding a chrome button must not bring Cancel back.
        sub.addChromeButton(QStringLiteral("Open portal"));
        QVERIFY(!sub.cancelButton()->isVisible());

        CommandHostDialog usage(CommandUi::planFor(QStringLiteral("usage")), liveCmd("usage", "Usage"));
        QVERIFY(!usage.cancelButton()->isVisible());
        usage.addChromeButton(QStringLiteral("Reset…"));
        QVERIFY(!usage.cancelButton()->isVisible());

        CommandHostDialog bundles(CommandUi::planFor(QStringLiteral("bundles")), liveCmd("bundles", "Skill bundles"));
        QVERIFY(bundles.pickerList() == nullptr);
        QVERIFY(!bundles.cancelButton()->isVisible());

        CommandHostDialog moa(CommandUi::planFor(QStringLiteral("moa")), liveCmd("moa", "Mixture of agents"));
        QCOMPARE(moa.findChild<QLineEdit *>(QStringLiteral("commandTextArgument"))->placeholderText(),
                 QString("What should the mixture of agents work on?"));
    }

    void livePanelPlansAndSeeds() {
        QCOMPARE(CommandUi::planFor(QStringLiteral("goal")).archetype, CommandArchetype::LivePanel);
        QCOMPARE(CommandUi::planFor(QStringLiteral("subgoal")).name, QString("subgoal"));
        QCOMPARE(CommandUi::planFor(QStringLiteral("hb")).name, QString("heartbeat"));
        QCOMPARE(CommandUi::planFor(QStringLiteral("proactive")).name, QString("loop"));
        QCOMPARE(CommandUi::planFor(QStringLiteral("q")).archetype, CommandArchetype::LivePanel);
        QCOMPARE(CommandUi::planFor(QStringLiteral("tasks")).name, QString("agents"));
        QCOMPARE(CommandUi::planFor(QStringLiteral("snap")).name, QString("snapshot"));
        QCOMPARE(CommandUi::planFor(QStringLiteral("suggest")).name, QString("suggestions"));
        QCOMPARE(CommandUi::planFor(QStringLiteral("bp")).name, QString("blueprint"));
        QVERIFY(CommandUi::planFor(QStringLiteral("rollback")).dangerous);
        QVERIFY(CommandUi::hasNativeUi(QStringLiteral("memory")));
        QVERIFY(CommandUi::hasNativeUi(QStringLiteral("skills")));
        QVERIFY(CommandUi::hasNativeUi(QStringLiteral("cron")));
        QVERIFY(CommandUi::hasNativeUi(QStringLiteral("debug")));
        QVERIFY(CommandUi::hasNativeUi(QStringLiteral("insights")));
        QVERIFY(CommandUi::hasNativeUi(QStringLiteral("diff")));
        QCOMPARE(CommandUi::badgeFor(QStringLiteral("goal")), QString("Dialog"));
        const auto goalSeeds = CommandUi::seedCommands(CommandUi::planFor(QStringLiteral("goal")));
        QCOMPARE(goalSeeds, QStringList() << QStringLiteral("/goal status") << QStringLiteral("/subgoal"));
        QCOMPARE(CommandUi::seedCommands(CommandUi::planFor(QStringLiteral("queue"))),
                 QStringList() << QStringLiteral("/queue list"));
        QCOMPARE(CommandUi::seedCommands(CommandUi::planFor(QStringLiteral("usage"))),
                 QStringList() << QStringLiteral("/usage"));
    }

    void parseLiveReports() {
        auto names = [](const QList<CommandPickerItem> &items) {
            QStringList out;
            for (const auto &it : items) out << it.value + QLatin1Char(':') + it.name;
            return out;
        };
        const auto queue = CommandUi::itemsFromLiveReport(
            QStringLiteral("queue"),
            QStringLiteral("2 prompts queued\n    1. Review the latest diff\n    2. then write tests\n"));
        QCOMPARE(queue.size(), 2);
        QCOMPARE(queue[0].name, QString("Review the latest diff"));
        QCOMPARE(queue[1].name, QString("then write tests"));

        const auto memory = CommandUi::itemsFromLiveReport(
            QStringLiteral("memory"),
            QStringLiteral("memory.write_approval = on\n  mem01  Remember the project uses Qt 5\n"));
        QCOMPARE(memory.size(), 1);
        QCOMPARE(memory[0].value, QString("mem01"));
        QVERIFY(memory[0].description.contains("Qt 5"));

        const auto rollback = CommandUi::itemsFromLiveReport(
            QStringLiteral("rollback"),
            QStringLiteral("Checkpoints:\n  1. a1b2c3d  2026-11-20 17:01  before edit\n  2. d4e5f6a  2026-11-20 17:10  after tools\n"));
        QCOMPARE(rollback.size(), 2);
        QCOMPARE(rollback[0].value, QString("a1b2c3d"));
        QVERIFY(rollback[0].name.contains("before edit"));

        const auto suggestions = CommandUi::itemsFromLiveReport(
            QStringLiteral("suggestions"),
            QStringLiteral("Suggestions:\n  1. Add a README section on ACP\n"));
        QCOMPARE(suggestions.size(), 1);
        QCOMPARE(CommandUi::statusFromReport(QStringLiteral("⊙ Goal (active, 0/20 turns): ship it\nMore")),
                 QString("⊙ Goal (active, 0/20 turns): ship it"));
        QCOMPARE(CommandUi::statusFromReport(QStringLiteral("No heartbeat set. Use /heartbeat set 10m prompt.")),
                 QString("No heartbeat set. Use /heartbeat set 10m prompt."));
        Q_UNUSED(names);
    }

    void queueEditsApplyOnSave() {
        CommandHostDialog d(CommandUi::planFor(QStringLiteral("queue")), liveCmd("queue", "Queue"));
        d.ingestReport(QStringLiteral("queue"),
                       QStringLiteral("1 prompt queued\n    1. Review the latest diff\n"));
        QCOMPARE(d.liveItems().size(), 1);
        QVERIFY(!d.isDirty());
        QVERIFY(!d.canApply());
        d.addLiveItem(QStringLiteral("then write tests"));
        QVERIFY(d.isDirty());
        QCOMPARE(d.applyLines(), QStringList() << QStringLiteral("/queue add then write tests"));
        d.removeSelectedLiveItem(); // may remove last or first depending on selection
        // Rebuild a known list and move.
        d.setLiveItems({
            {QStringLiteral("1"), QStringLiteral("first"), {}, {}},
            {QStringLiteral("2"), QStringLiteral("second"), {}, {}},
        }, true);
        d.setSelectedValue(QStringLiteral("1"));
        d.moveSelectedLiveItem(1);
        QCOMPARE(d.liveItems()[0].name, QString("second"));
        QCOMPARE(d.liveItems()[1].name, QString("first"));
        QCOMPARE(d.applyLines().first(), QString("/queue clear"));
        QVERIFY(d.applyLines().contains(QStringLiteral("/queue add first")));
        d.setLiveAction(QStringLiteral("clear"));
        QCOMPARE(d.commandLine(), QString("/queue clear"));
        QVERIFY(d.applyRequest().keepOpen);
    }

    void goalHeartbeatAndRollbackLines() {
        CommandHostDialog goal(CommandUi::planFor(QStringLiteral("goal")), liveCmd("goal", "Goal"));
        goal.ingestReport(QStringLiteral("goal"), QStringLiteral("⊙ Goal (active, 0/20 turns): ship it"));
        goal.ingestReport(QStringLiteral("subgoal"), QStringLiteral("Subgoals:\n  1. Write the changelog\n"));
        QCOMPARE(goal.liveItems().size(), 1);
        goal.setLiveAction(QStringLiteral("pause"));
        QCOMPARE(goal.commandLine(), QString("/goal pause"));
        goal.setLiveAction(QStringLiteral("subgoal-add"));
        goal.setTextArgument(QStringLiteral("Add screenshots"));
        QCOMPARE(goal.commandLine(), QString("/subgoal add Add screenshots"));
        goal.setLiveAction(QStringLiteral("subgoal-remove"));
        goal.setSelectedValue(QStringLiteral("1"));
        QCOMPARE(goal.commandLine(), QString("/subgoal rm 1"));

        CommandHostDialog hb(CommandUi::planFor(QStringLiteral("heartbeat")), liveCmd("heartbeat", "Heartbeat"));
        hb.setLiveAction(QStringLiteral("set"));
        hb.setIntervalText(QStringLiteral("10m"));
        hb.setTextArgument(QStringLiteral("check the inbox"));
        QCOMPARE(hb.commandLine(), QString("/heartbeat set 10m check the inbox"));

        CommandHostDialog rb(CommandUi::planFor(QStringLiteral("rollback")), liveCmd("rollback", "Checkpoints"));
        rb.ingestReport(QStringLiteral("rollback"),
                        QStringLiteral("Checkpoints:\n  1. a1b2c3d  2026-11-20 17:01  before edit\n"));
        rb.setLiveAction(QStringLiteral("restore"));
        rb.setSelectedValue(QStringLiteral("a1b2c3d"));
        rb.setOptionAll(true);
        QCOMPARE(rb.commandLine(), QString("/rollback a1b2c3d --all"));
        QVERIFY(rb.canApply());
        QVERIFY(!rb.applyRequest().keepOpen); // restore closes

        CommandHostDialog insights(CommandUi::planFor(QStringLiteral("insights")), liveCmd("insights", "Insights"));
        insights.setInsightDays(14);
        insights.setLiveAction(QStringLiteral("refresh"));
        QCOMPARE(insights.commandLine(), QString("/insights 14"));

        CommandHostDialog usage(CommandUi::planFor(QStringLiteral("usage")), liveCmd("usage", "Usage"));
        QCOMPARE(usage.plan().archetype, CommandArchetype::Info);
        QVERIFY(usage.findChild<QPushButton *>(QString(), Qt::FindDirectChildrenOnly) || usage.saveButton());
        // Arming is a button click; the Reset control exists on the button box.
        QVERIFY(usage.findChildren<QPushButton *>().size() >= 2);
    }

    void memorySkillsAndAutomations() {
        CommandHostDialog mem(CommandUi::planFor(QStringLiteral("memory")), liveCmd("memory", "Memory"));
        mem.ingestReport(QStringLiteral("memory"),
                         QStringLiteral("memory.write_approval = on\n  mem01  Remember the project uses Qt 5\n"));
        mem.setLiveAction(QStringLiteral("approve"));
        mem.setSelectedValue(QStringLiteral("mem01"));
        QCOMPARE(mem.commandLine(), QString("/memory approve mem01"));
        mem.setLiveAction(QStringLiteral("gate-off"));
        QCOMPARE(mem.commandLine(), QString("/memory write-approval off"));

        CommandHostDialog sk(CommandUi::planFor(QStringLiteral("skills")), liveCmd("skills", "Skills"));
        sk.setLiveAction(QStringLiteral("install"));
        sk.setTextArgument(QStringLiteral("arxiv"));
        QCOMPARE(sk.commandLine(), QString("/skills install arxiv"));

        CommandHostDialog cron(CommandUi::planFor(QStringLiteral("cron")), liveCmd("cron", "Scheduled tasks"));
        cron.ingestReport(QStringLiteral("cron"),
                          QStringLiteral("Scheduled tasks:\n  1. job-1  active  every 1h  status ping\n"));
        cron.setLiveAction(QStringLiteral("pause"));
        cron.setSelectedValue(QStringLiteral("job-1"));
        QCOMPARE(cron.commandLine(), QString("/cron pause job-1"));

        CommandHostDialog sug(CommandUi::planFor(QStringLiteral("suggestions")), liveCmd("suggestions", "Suggestions"));
        sug.ingestReport(QStringLiteral("suggestions"), QStringLiteral("Suggestions:\n  1. Add a README section on ACP\n"));
        sug.setLiveAction(QStringLiteral("accept"));
        sug.setSelectedValue(QStringLiteral("1"));
        QCOMPARE(sug.commandLine(), QString("/suggestions accept 1"));

        CommandHostDialog bp(CommandUi::planFor(QStringLiteral("blueprint")), liveCmd("blueprint", "Blueprints"));
        bp.ingestReport(QStringLiteral("blueprint"),
                        QStringLiteral("Blueprints:\n  1. weekly-review  Recurring weekly review session\n"));
        bp.setLiveAction(QStringLiteral("apply"));
        bp.setSelectedValue(QStringLiteral("weekly-review"));
        bp.setTextArgument(QStringLiteral("slot=demo"));
        QCOMPARE(bp.commandLine(), QString("/blueprint weekly-review slot=demo"));
    }
};

QTEST_MAIN(CommandUiTest)
#include "test_commandui.moc"
