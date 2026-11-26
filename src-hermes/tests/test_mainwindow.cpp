// Integration tests for the Hermes GUI: the main window driven against the
// scripted ACP agent (fake_hermes_acp.py), offscreen.
#include "mainwindow.h"
#include "commanddialog.h"

#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QTemporaryDir>
#include <QToolBar>
#include <QToolButton>
#include <QtTest>

#ifndef FAKE_ACP_SCRIPT
#error "FAKE_ACP_SCRIPT must point at tests/fake_hermes_acp.py"
#endif

class MainWindowTest : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_dir;
    QString m_wrapper;
    QString m_log;

    void writeWrapper(const QString &extraEnv = QString()) {
        m_wrapper = m_dir.filePath(QStringLiteral("hermes"));
        QFile f(m_wrapper);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(QStringLiteral("#!/bin/sh\nexport FAKE_ACP_VERSION=0.23.3 FAKE_ACP_FULL_COMMANDS=1 FAKE_ACP_LOG='%1' %2\n"
                               "exec python3 '%3' \"$@\"\n")
                    .arg(m_log, extraEnv, QStringLiteral(FAKE_ACP_SCRIPT)).toUtf8());
        f.close();
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }
    MainWindow *makeWindow() {
        MainWindow::Options o;
        o.executable = m_wrapper;
        o.centerOnScreen = false;
        auto *w = new MainWindow(o);
        w->resize(1280, 820);
        w->show();
        return w;
    }
    static bool waitDiscovered(MainWindow *w) {
        // Discovery needs a live session (Hermes answers /commands per session).
        if (!QTest::qWaitFor([w]() { return w->stateText().startsWith("Connected") || w->client()->isReady(); }, 15000))
            return false;
        if (!w->client()->isReady()) w->startNewSession();
        return QTest::qWaitFor([w]() { return w->catalog()->listingComplete() && !w->stateText().startsWith("Running"); }, 15000);
    }
    QList<QJsonObject> logged(const QString &method) const {
        QList<QJsonObject> out;
        QFile f(m_log);
        if (!f.open(QIODevice::ReadOnly)) return out;
        for (const QByteArray &line : f.readAll().split('\n')) {
            const QJsonObject o = QJsonDocument::fromJson(line).object();
            if (o.value("method").toString() == method) out.append(o);
        }
        return out;
    }
    static QList<QAction *> allActions(QMenu *menu) {
        QList<QAction *> out;
        for (QAction *a : menu->actions()) {
            out.append(a);
            if (a->menu()) out += allActions(a->menu());
        }
        return out;
    }
    static QAction *findCommand(QMenu *menu, const QString &name) {
        for (QAction *a : allActions(menu))
            if (a->property("catalogCommand").toString() == name) return a;
        return nullptr;
    }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
        qputenv("XDG_CONFIG_HOME", m_dir.filePath("config").toUtf8());
        qputenv("HERMES_HOME", m_dir.filePath("hermes-home").toUtf8());
        QCoreApplication::setOrganizationName("AnivizaTest");
        QCoreApplication::setApplicationName("ConsiglioMainWindowTest");
        m_log = m_dir.filePath("acp.log");
    }
    void init() {
        QFile::remove(m_log);
        writeWrapper();
        CommandCatalog::clearCache();
    }

    // Commands come from Hermes (ACP + /commands pages), quietly.
    void discoversEveryCommandQuietly() {
        QScopedPointer<MainWindow> w(makeWindow());
        QVERIFY(waitDiscovered(w.data()));
        QVERIFY2(w->catalog()->count() >= 30, qPrintable(QString::number(w->catalog()->count())));
        QTRY_VERIFY_WITH_TIMEOUT(w->client()->isReady() || !w->chat()->hasConversation(), 5000);
        QCOMPARE(w->chat()->blockCount(), 0);           // discovery never shows in the chat
        // Every command is in the palette and in some menu.
        const QList<PaletteAction> pal = w->palette()->actions();
        QSet<QString> ids;
        for (const auto &p : pal) ids.insert(p.id);
        QList<QAction *> menuActions;
        for (QAction *top : w->menuBar()->actions()) if (top->menu()) menuActions += allActions(top->menu());
        QSet<QString> inMenus;
        for (QAction *a : menuActions) inMenus.insert(a->property("catalogCommand").toString());
        for (const auto &c : w->catalog()->commands()) {
            QVERIFY2(ids.contains('/' + c.name), qPrintable("palette lacks /" + c.name));
            QVERIFY2(inMenus.contains(c.name), qPrintable("menus lack /" + c.name));
        }
    }

    // A command a Hermes update adds appears without a Consiglio release.
    void newCommandsFromUpdateAppear() {
        writeWrapper(QStringLiteral("FAKE_ACP_NEW_COMMAND=zz-newfeature"));
        QScopedPointer<MainWindow> w(makeWindow());
        QVERIFY(waitDiscovered(w.data()));
        QVERIFY(w->catalog()->contains("zz-newfeature"));
        QAction *a = findCommand(w->hermesMenu(), "zz-newfeature");
        QVERIFY(a);
        QVERIFY(!a->menu());                             // unknown commands open the console, no choice submenu
        bool inPalette = false;
        QString badge;
        for (const auto &p : w->palette()->actions()) {
            if (p.id == "/zz-newfeature") { inPalette = true; badge = p.badge; }
        }
        QVERIFY(inPalette);
        QCOMPARE(badge, QString("Chat"));
        QScopedPointer<QDialog> d(w->createCommandDialog("zz-newfeature"));
        QVERIFY(qobject_cast<CommandDialog *>(d.data()));
    }

    void everyControlIsLabelled() {
        QScopedPointer<MainWindow> w(makeWindow());
        QVERIFY(waitDiscovered(w.data()));
        for (const QString &id : w->actionIds()) {
            QAction *a = w->action(id);
            QVERIFY2(!a->toolTip().isEmpty(), qPrintable(id));
            QVERIFY2(!a->text().isEmpty(), qPrintable(id));
        }
        for (const char *id : {"app:new-session", "app:palette", "app:settings", "app:stop", "app:quit", "app:sidebar"}) {
            QVERIFY2(w->action(id), id);
            QVERIFY2(!w->action(id)->shortcut().isEmpty(), id);
        }
        QVERIFY(w->shortcutList().size() >= 10);
        for (QWidget *c : w->toolbar()->findChildren<QWidget *>()) {
            if (!qobject_cast<QToolButton *>(c) && !qobject_cast<QComboBox *>(c)) continue;
            if (c->objectName().startsWith("qt_")) continue;  // Qt's own overflow button
            if (qobject_cast<QComboBox *>(c->parentWidget())) continue;
            QVERIFY2(!c->toolTip().isEmpty(), qPrintable(c->metaObject()->className() + (" " + c->objectName())));
            QVERIFY2(!c->accessibleName().isEmpty() || (qobject_cast<QToolButton *>(c) && qobject_cast<QToolButton *>(c)->defaultAction()),
                     qPrintable(c->objectName()));
        }
    }

    // Goal lives with the other automation commands — no dedicated Goal menu —
    // and opens the live panel rather than echoing a /command into chat.
    void goalOpensLivePanel() {
        QScopedPointer<MainWindow> w(makeWindow());
        QVERIFY(waitDiscovered(w.data()));
        for (QAction *top : w->menuBar()->actions())
            QVERIFY2(top->text().remove('&') != "Goal", "no dedicated Goal menu");
        for (QWidget *c : w->toolbar()->findChildren<QWidget *>())
            QVERIFY2(!c->objectName().contains("goal", Qt::CaseInsensitive), qPrintable(c->objectName()));
        QAction *goal = findCommand(w->hermesMenu(), "goal");
        QVERIFY(goal);
        QVERIFY(!goal->menu());
        QAction *group = nullptr;
        for (QAction *a : w->hermesMenu()->actions())
            if (a->menu() && a->menu()->actions().contains(goal)) group = a;
        QVERIFY(group);
        QCOMPARE(group->property("catalogGroup").toString(), QString("automation"));
        // A typed /goal line in the chat still works.
        QVERIFY(w->runCommand("/goal ship the release notes"));
        QTRY_VERIFY_WITH_TIMEOUT(w->stateText().startsWith("Ready"), 10000);
        QScopedPointer<QDialog> d(w->createCommandDialog("goal"));
        auto *host = qobject_cast<CommandHostDialog *>(d.data());
        QVERIFY(host);
        QCOMPARE(host->plan().archetype, CommandArchetype::LivePanel);
        host->setLiveAction(QStringLiteral("set"));
        host->setTextArgument(QStringLiteral("write the changelog"));
        QCOMPARE(host->commandLine(), QString("/goal set write the changelog"));
        QCOMPARE(host->applyRequest().keepOpen, true);
        QCOMPARE(host->applyRequest().quietLines, QStringList() << QStringLiteral("/goal set write the changelog"));
        bool inPalette = false;
        QString badge;
        for (const auto &p : w->palette()->actions()) {
            if (p.id == "/goal") { inPalette = true; badge = p.badge; }
        }
        QVERIFY(inPalette);
        QCOMPARE(badge, QString("Dialog"));
        QCOMPARE(CommandUi::badgeFor(QStringLiteral("subgoal")), QString("Dialog"));
    }


    void modelsDialogAppliesOnlyOnSave() {
        QScopedPointer<MainWindow> w(makeWindow());
        QVERIFY(waitDiscovered(w.data()));
        QTRY_VERIFY_WITH_TIMEOUT(!w->client()->configOptions().isEmpty(), 10000);
        QScopedPointer<QDialog> d(w->createCommandDialog("models"));
        auto *host = qobject_cast<CommandHostDialog *>(d.data());
        QVERIFY(host);
        QVERIFY(host->pickerCount() >= 2);
        QCOMPARE(host->selectedValue(), QString("fake/model"));
        QVERIFY(host->previewBody().contains("fake"));
        QVERIFY(!host->saveButton()->isEnabled());          // nothing dirty yet
        QCOMPARE(logged("session/set_config_option").size(), 0);
        host->setSelectedValue("fake/large");
        QVERIFY(host->isDirty());
        QVERIFY(host->previewHeadline().contains("Large") || host->previewBody().contains("fake/large"));
        QVERIFY(host->saveButton()->isEnabled());
        QCOMPARE(logged("session/set_config_option").size(), 0);
        const auto req = host->applyRequest();
        QCOMPARE(req.method, CommandApply::ConfigOption);
        QCOMPARE(req.configId, QString("model"));
        QCOMPARE(req.value, QString("fake/large"));
        QString badge;
        for (const auto &p : w->palette()->actions()) if (p.id == "/model") badge = p.badge;
        QCOMPARE(badge, QString("Dialog"));
    }

    void helloWorldTurn() {
        QScopedPointer<MainWindow> w(makeWindow());
        QVERIFY(waitDiscovered(w.data()));
        QVERIFY(w->runCommand("hello world"));
        QTRY_VERIFY_WITH_TIMEOUT(w->chat()->blockCount() >= 2, 10000);
        QTRY_COMPARE_WITH_TIMEOUT(w->stateText().startsWith("Ready"), true, 10000);
        QCOMPARE(logged("session/prompt").size() >= 1, true);
    }

    void configComboSetsOption() {
        QScopedPointer<MainWindow> w(makeWindow());
        QVERIFY(waitDiscovered(w.data()));
        QVERIFY(w->runCommand("hello"));
        QTRY_VERIFY_WITH_TIMEOUT(w->configCombo("reasoning_effort"), 10000);
        QComboBox *c = w->configCombo("reasoning_effort");
        QVERIFY(!c->toolTip().isEmpty());
        QVERIFY(!c->accessibleName().isEmpty());
        const int high = c->findData("high");
        QVERIFY(high >= 0);
        c->setCurrentIndex(high);
        emit c->activated(high);                         // what a user pick emits
        QTRY_VERIFY_WITH_TIMEOUT(!logged("session/set_config_option").isEmpty(), 5000);
        const QJsonObject p = logged("session/set_config_option").last().value("params").toObject();
        QCOMPARE(p.value("configId").toString(), QString("reasoning_effort"));
        QCOMPARE(p.value("value").toString(), QString("high"));
    }

    void stopCancelsTurn() {
        QScopedPointer<MainWindow> w(makeWindow());
        QVERIFY(waitDiscovered(w.data()));
        QVERIFY(w->runCommand("slow"));
        QTRY_VERIFY_WITH_TIMEOUT(w->action("app:stop")->isEnabled(), 10000);
        // Wait until the prompt has actually reached the agent (it may be
        // queued behind a quiet discovery command first).
        QTRY_VERIFY_WITH_TIMEOUT(QString::fromUtf8(QJsonDocument(logged("session/prompt").isEmpty() ? QJsonObject()
                                     : logged("session/prompt").last()).toJson()).contains("\"slow\""), 10000);
        w->action("app:stop")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(!logged("session/cancel").isEmpty(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!w->action("app:stop")->isEnabled(), 5000);
    }

    void commandDialogForArgs() {
        QScopedPointer<MainWindow> w(makeWindow());
        QVERIFY(waitDiscovered(w.data()));
        QScopedPointer<QDialog> d(w->createCommandDialog("queue"));
        auto *host = qobject_cast<CommandHostDialog *>(d.data());
        QVERIFY(host);
        QVERIFY(!host->canApply());
        QCOMPARE(host->plan().archetype, CommandArchetype::LivePanel);
        host->addLiveItem(QStringLiteral("then write tests"));
        QCOMPARE(host->commandLine(), QString("/queue add then write tests"));
        QCOMPARE(host->applyLines(), QStringList() << QStringLiteral("/queue add then write tests"));
        QVERIFY(host->canApply());
        QVERIFY(host->applyRequest().keepOpen);
    }

    void badExecutableShowsDisconnected() {
        MainWindow::Options o;
        o.executable = m_dir.filePath("does-not-exist/hermes");
        o.centerOnScreen = false;
        MainWindow w(o);
        w.show();
        QTRY_VERIFY_WITH_TIMEOUT(w.chat()->isBannerVisible(), 10000);
        QCOMPARE(w.stateText(), QString("Not connected"));
    }

    void cachedCatalogUsedBeforeDiscovery() {
        {
            QScopedPointer<MainWindow> w(makeWindow());
            QVERIFY(waitDiscovered(w.data()));
        }
        MainWindow::Options o;
        o.executable = m_wrapper;
        o.autoLaunch = false;
        MainWindow w(o);
        QVERIFY2(w.catalog()->count() >= 30, "cached catalog not loaded at startup");
        QVERIFY(findCommand(w.hermesMenu(), "goal"));   // menus built from the cache
    }
};

QTEST_MAIN(MainWindowTest)
#include "test_mainwindow.moc"
