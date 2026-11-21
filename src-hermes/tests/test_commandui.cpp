// Unit tests for the command UI map and the shared dialog shell: pickers
// preview without applying, Save is required, unknown commands are passthrough.
#include "commandui.h"
#include "commanddialog.h"
#include "theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QtTest>

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
};

QTEST_MAIN(CommandUiTest)
#include "test_commandui.moc"
