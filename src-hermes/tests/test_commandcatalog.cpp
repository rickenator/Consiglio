// Unit tests for command discovery (CommandCatalog), `hermes config show` parsing and secret masking. Page fixtures are real
// `/commands N` replies captured from Hermes Agent v0.23.3.
#include "commandcatalog.h"
#include "commanddialog.h"
#include "hermescli.h"
#include "uiutil.h"

#include <QApplication>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#ifndef FIXTURE_DIR
#define FIXTURE_DIR "."
#endif

class CommandCatalogTest : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_config;
    static QString fixture(int page) {
        QFile f(QStringLiteral(FIXTURE_DIR "/hermes-0.23.3/commands-page-%1.txt").arg(page));
        if (!f.open(QIODevice::ReadOnly)) return QString();
        return QString::fromUtf8(f.readAll());
    }
    static CommandCatalog::Command cmd(const QString &name, const QString &args) {
        CommandCatalog::Command c;
        c.name = name;
        c.argsHint = args;
        return c;
    }
    static CommandCatalog::Page page(int n, int count, const QList<CommandCatalog::Command> &cmds) {
        CommandCatalog::Page p;
        p.page = n;
        p.pageCount = count;
        p.total = cmds.size();
        p.commands = cmds;
        p.valid = true;
        return p;
    }

private slots:
    void initTestCase() {
        qputenv("XDG_CONFIG_HOME", m_config.path().toUtf8());
        QCoreApplication::setOrganizationName(QStringLiteral("AnivizaTest"));
        QCoreApplication::setApplicationName(QStringLiteral("ConsiglioCatalogTest"));
    }

    void parsesRealRegistryPage() {
        const QString text = fixture(2);
        QVERIFY2(!text.isEmpty(), "fixture missing");
        const auto p = CommandCatalog::parsePage(text);
        QVERIFY(p.valid);
        QCOMPARE(p.page, 2);
        QCOMPARE(p.pageCount, 6);
        QCOMPARE(p.total, 110);
        QVERIFY(p.commands.size() >= 15);
        QHash<QString, CommandCatalog::Command> by;
        for (const auto &c : p.commands) by.insert(c.name, c);
        QVERIFY(by.contains("goal"));
        QVERIFY(by["goal"].argsHint.startsWith("[text"));
        QVERIFY(by["goal"].description.contains("standing goal"));
        QCOMPARE(by["heartbeat"].aliases, QStringList{"hb"});
        QVERIFY(!by["heartbeat"].description.contains("alias"));
        QCOMPARE(by["context"].aliases, QStringList{"ctx"});
        QCOMPARE(by["loop"].aliases, QStringList{"proactive"});
        QVERIFY(by["moa"].requiresArgs());
        QVERIFY(!by["status"].takesArgs());
        QVERIFY(!by["loop"].requiresArgs());   // "[interval] <prompt> …" starts optional
    }

    void parsesSkillSectionAndLastPage() {
        const auto p4 = CommandCatalog::parsePage(fixture(4));
        QVERIFY(p4.valid);
        int skills = 0;
        for (const auto &c : p4.commands) if (c.isSkill()) ++skills;
        QVERIFY2(skills > 0, "skill commands (— separator) not parsed");
        const auto p6 = CommandCatalog::parsePage(fixture(6));
        QCOMPARE(p6.page, 6);
        QCOMPARE(p6.pageCount, 6);
        for (const auto &c : p6.commands) QVERIFY2(!c.name.isEmpty() && !c.description.isEmpty(), qPrintable(c.name));
    }

    void ignoresNonCatalogText() {
        QVERIFY(!CommandCatalog::parsePage("Hello there, how can I help?").valid);
        QVERIFY(!CommandCatalog::parsePage("").valid);
    }

    void choicesFromSyntax() {
        QCOMPARE(cmd("x", "[on|off|status]").choices(), (QStringList{"on", "off", "status"}));
        const QStringList hb = cmd("heartbeat", "[every <interval> <prompt> | status | pause | resume | clear]").choices();
        QCOMPARE(hb, (QStringList{"status", "pause", "resume", "clear"}));
        const QStringList goal = cmd("goal", "[text | -- <text> | draft <text> | show | gate add <cmd> | pause | resume | clear | status | wait <pid> | unwait]").choices();
        QVERIFY(goal.contains("pause") && goal.contains("resume") && goal.contains("clear") && goal.contains("status"));
        QVERIFY(!goal.contains("text"));
        QVERIFY(cmd("queue", "<prompt>").choices().isEmpty());
        QVERIFY(cmd("queue", "<prompt>").requiresArgs());
        QVERIFY(cmd("title", "[name]").choices().isEmpty());
        QVERIFY(!cmd("title", "[name]").requiresArgs());
    }

    void mergesAdvertisedAndListed() {
        CommandCatalog cat;
        QSignalSpy changed(&cat, &CommandCatalog::changed);
        cat.setAdvertised({{"model", "Show model", "model name to switch to"}, {"acponly", "Only via ACP", ""}});
        QCOMPARE(cat.count(), 2);
        cat.beginListing();
        auto m = cmd("model", "[model|reset] [--provider name]");
        m.description = "Switch model";
        auto h = cmd("heartbeat", "[status]");
        h.aliases = QStringList{QStringLiteral("hb")};
        QCOMPARE(cat.addListedPage(page(1, 2, {m})), 2);
        QVERIFY(!cat.listingComplete());
        QCOMPARE(cat.addListedPage(page(2, 2, {h})), 0);
        QVERIFY(cat.listingComplete());
        QCOMPARE(cat.count(), 3);
        QVERIFY(cat.contains("acponly"));                    // still advertised over ACP
        QCOMPARE(cat.command("model").inputHint, QString("model name to switch to"));
        QVERIFY(cat.command("model").argsHint.startsWith("[model|reset]"));
        QCOMPARE(cat.command("hb").name, QString("heartbeat"));   // aliases resolve
        QVERIFY(changed.count() >= 3);
        for (const auto &c : cat.commands()) {
            QVERIFY2(!c.group.isEmpty() && !c.icon.isEmpty() && !c.label.isEmpty(), qPrintable(c.name));
        }
    }

    void dropsCommandsAnUpdateRemoved() {
        CommandCatalog cat;
        cat.beginListing();
        cat.addListedPage(page(1, 1, {cmd("old", ""), cmd("keep", "")}));
        QVERIFY(cat.contains("old"));
        // A later Hermes no longer lists /old but adds /brandnew.
        cat.beginListing();
        cat.addListedPage(page(1, 1, {cmd("keep", ""), cmd("brandnew", "[on|off]")}));
        QVERIFY(!cat.contains("old"));
        QVERIFY(cat.contains("brandnew"));
        // Unknown commands still get a group, icon and label (never hidden).
        const auto b = cat.command("brandnew");
        QCOMPARE(b.group, QString("more"));
        QVERIFY(!b.icon.isEmpty());
        QCOMPARE(b.choices(), (QStringList{"on", "off"}));
    }

    void cachesPerVersion() {
        CommandCatalog::clearCache();
        {
            CommandCatalog cat;
            cat.beginListing();
            cat.addListedPage(page(1, 1, {cmd("goal", "[text]"), cmd("status", "")}));
            cat.saveCache("0.23.3");
        }
        CommandCatalog fresh;
        QString version;
        QVERIFY(fresh.loadCache(&version));
        QCOMPARE(version, QString("0.23.3"));
        QVERIFY(fresh.contains("goal"));
        QVERIFY(fresh.command("goal").sources & CommandCatalog::Cached);
        CommandCatalog::clearCache();
        CommandCatalog none;
        QVERIFY(!none.loadCache());
        QCOMPARE(none.count(), 0);
    }

    void staticPresentation() {
        QCOMPARE(CommandCatalog::groupFor("goal", false), QString("automation"));   // one command among the others
        QCOMPARE(CommandCatalog::groupFor("model", false), QString("model"));
        QCOMPARE(CommandCatalog::groupFor("some-skill", true), QString("skills"));
        QCOMPARE(CommandCatalog::groupFor("never-heard-of-it", false), QString("more"));
        QVERIFY(!CommandCatalog::labelFor("undo").isEmpty());
        QCOMPARE(CommandCatalog::groups().first().id, QString("session"));
    }

    void commandDialogPreview() {
        auto c = cmd("heartbeat", "[every <interval> <prompt> | status | pause | resume | clear]");
        c.label = "Heartbeat";
        CommandDialog d(c);
        QVERIFY(d.canRun());                    // optional arguments
        QCOMPARE(d.commandLine(), QString("/heartbeat"));
        d.setArguments("status");
        QCOMPARE(d.commandLine(), QString("/heartbeat status"));
        auto q = cmd("queue", "<prompt>");
        q.label = "Queue";
        CommandDialog dq(q);
        QVERIFY(!dq.canRun());                  // required argument missing
        dq.setArguments("next do X");
        QVERIFY(dq.canRun());
    }

    void parsesConfigShow() {
        const QString text = QStringLiteral(
            "\n◆ Model\n  Model:        {'default': 'qwen', 'provider': 'custom', 'base_url': 'http://127.0.0.1:8091/v1'}\n"
            "  Max turns:    90\n\n◆ API Keys\n  OpenRouter:   (not set)\n  Anthropic:    sk-ant-...abcd\n\n"
            "◆ Display\n  Personality:  kawaii\n────────────────\n  Run hermes config edit to change\n");
        const auto sections = HermesCli::parseConfigShow(text);
        QVERIFY(sections.size() >= 3);
        QCOMPARE(sections[0].title, QString("Model"));
        bool sawDefault = false, sawSecretSet = false, sawSecretUnset = false;
        for (const auto &s : sections) {
            for (const auto &r : s.rows) {
                if (r.key.contains("Default")) { sawDefault = true; QCOMPARE(r.value, QString("qwen")); }
                if (r.key == "Anthropic") { sawSecretSet = r.secret && r.set; QVERIFY(!r.value.contains("abcd")); }
                if (r.key == "OpenRouter") sawSecretUnset = r.secret && !r.set;
                QVERIFY2(!r.key.contains("Run hermes"), "footer leaked into rows");
            }
        }
        QVERIFY(sawDefault);
        QVERIFY(sawSecretSet);
        QVERIFY(sawSecretUnset);
    }

    void masksSecrets() {
        const QString masked = ui::maskSecrets("key sk-ant-api03-ABCDEFGHIJKLMNOPQRSTUV and Bearer abcdefghijklmnop1234 api_key=hunter2hunter2");
        QVERIFY(!masked.contains("ABCDEFGHIJKLMNOP"));
        QVERIFY(!masked.contains("abcdefghijklmnop1234"));
        QVERIFY(!masked.contains("hunter2hunter2"));
        QCOMPARE(ui::maskSecrets("nothing secret here"), QString("nothing secret here"));
    }

    void terminalCommandWrapsArgs() {
        const QStringList cmdline = HermesCli::terminalCommand({"--setup"});
        QVERIFY(!cmdline.isEmpty());
        QVERIFY(cmdline.join(' ').contains("--setup"));
    }
};

QTEST_MAIN(CommandCatalogTest)
#include "test_commandcatalog.moc"
