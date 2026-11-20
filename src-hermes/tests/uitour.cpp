// Offscreen screenshot tour of Consiglio · Hermes.
//
//   QT_QPA_PLATFORM=offscreen ./uitour --out DIR [--theme light|dark] [--real]
//
// Default: drives the scripted fake agent (tests/fake_hermes_acp.py) through
// every UI state. --real uses the `hermes` on PATH (a real model turn needs a
// configured provider). Screenshots are written as DIR/<theme>-NN-name.png.
#include "chatpane.h"
#include "commanddialog.h"
#include "mainwindow.h"
#include "settingsdialog.h"
#include "theme.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>
#include <QToolBar>
#include <QToolButton>
#include <functional>

static QString g_out;
static QString g_prefix;
static int g_n = 0;
static QTextStream g_log(stdout);

static bool waitFor(const std::function<bool()> &pred, int ms) {
    QElapsedTimer t;
    t.start();
    while (!pred()) {
        if (t.elapsed() > ms) return false;
        QTest::qWait(30);
    }
    return true;
}

static void settle(int ms = 250) { QTest::qWait(ms); }

static QString path(const QString &name) {
    return QStringLiteral("%1/%2-%3-%4.png").arg(g_out, g_prefix).arg(++g_n, 2, 10, QLatin1Char('0')).arg(name);
}

static void shot(QWidget *w, const QString &name) {
    settle(120);
    const QString p = path(name);
    w->grab().save(p);
    g_log << "  " << p << "\n";
    g_log.flush();
}

// The window with popup menus painted where they are on screen.
static void shotWithPopups(QWidget *window, const QList<QWidget *> &popups, const QString &name) {
    settle(200);
    QImage img = window->grab().toImage();
    QPainter p(&img);
    for (QWidget *popup : popups) {
        if (!popup || !popup->isVisible()) continue;
        const QPoint at = window->mapFromGlobal(popup->mapToGlobal(QPoint(0, 0)));
        p.drawPixmap(at, popup->grab());
    }
    p.end();
    const QString file = path(name);
    img.save(file);
    g_log << "  " << file << "\n";
    g_log.flush();
}

static QDialog *findDialog(const QString &objectName) {
    for (QWidget *w : QApplication::topLevelWidgets())
        if (w->isVisible() && w->objectName() == objectName) return qobject_cast<QDialog *>(w);
    return nullptr;
}

static bool idle(MainWindow &w) {
    return !w.client()->isBusy() && !w.client()->hasQuietWork();
}

static void runAndWait(MainWindow &w, const QString &line, int ms) {
    g_log << "> " << line << "\n";
    g_log.flush();
    w.runCommand(line);
    settle(100);
    if (!waitFor([&]() { return idle(w); }, ms)) g_log << "  (timed out waiting for " << line << ")\n";
    settle(400);
}

static void tourMenus(MainWindow &w) {
    // Goal menu from the Goal button.
    QToolButton *goal = w.goalButton();
    w.goalMenu()->popup(goal->mapToGlobal(QPoint(0, goal->height())));
    shotWithPopups(&w, {w.goalMenu()}, "goal-menu");
    w.goalMenu()->hide();

    // Hermes menu with one group open.
    QMenuBar *bar = w.menuBar();
    QAction *hermesAction = w.hermesMenu()->menuAction();
    const QRect r = bar->actionGeometry(hermesAction);
    w.hermesMenu()->popup(bar->mapToGlobal(r.bottomLeft()));
    settle(150);
    QMenu *sub = nullptr;
    QAction *subAction = nullptr;
    for (QAction *a : w.hermesMenu()->actions()) {
        if (a->menu() && a->property("catalogGroup").toString() == QLatin1String("model")) { sub = a->menu(); subAction = a; break; }
    }
    if (!sub)
        for (QAction *a : w.hermesMenu()->actions()) if (a->menu()) { sub = a->menu(); subAction = a; break; }
    if (sub) {
        w.hermesMenu()->setActiveAction(subAction);
        const QRect ar = w.hermesMenu()->actionGeometry(subAction);
        sub->popup(w.hermesMenu()->mapToGlobal(QPoint(w.hermesMenu()->width() - 4, ar.top() - 4)));
    }
    shotWithPopups(&w, {w.hermesMenu(), sub}, "hermes-menu");
    if (sub) sub->hide();
    w.hermesMenu()->hide();

    // Session menu (app actions + Hermes session commands).
    const QRect sr = bar->actionGeometry(w.sessionMenu()->menuAction());
    w.sessionMenu()->popup(bar->mapToGlobal(sr.bottomLeft()));
    shotWithPopups(&w, {w.sessionMenu()}, "session-menu");
    w.sessionMenu()->hide();
}

static void tourDialogs(MainWindow &w, bool real) {
    // Command palette.
    w.palette()->open();
    shot(&w, "palette");
    w.palette()->setQuery(QStringLiteral("goal"));
    shot(&w, "palette-search");
    w.palette()->closeOverlay();

    // A command with one-click choices, and one with a required argument.
    QString withChoices, required;
    for (const auto &c : w.catalog()->commands()) {
        if (withChoices.isEmpty() && c.choices().size() >= 3 && !c.requiresArgs() && c.name != QLatin1String("goal")) withChoices = c.name;
        if (required.isEmpty() && c.requiresArgs()) required = c.name;
    }
    for (const QString &name : {withChoices, required}) {
        if (name.isEmpty()) continue;
        QDialog *d = w.createCommandDialog(name);
        d->show();
        shot(d, "command-dialog-" + name);
        d->deleteLater();
    }

    // Settings pages.
    SettingsDialog *s = w.openSettings(QStringLiteral("appearance"));
    s->resize(980, 680);
    for (const char *page : {"appearance", "session", "agent", "config", "shortcuts"}) {
        s->showPage(QString::fromLatin1(page));
        if (QLatin1String(page) == QLatin1String("config")) {
            s->loadConfiguration();
            waitFor([&]() { return s->configurationLoaded(); }, real ? 30000 : 8000);
            settle(real ? 1500 : 400);
        }
        if (QLatin1String(page) == QLatin1String("agent")) settle(real ? 1500 : 400);
        shot(s, QStringLiteral("settings-%1").arg(QLatin1String(page)));
    }
    s->close();
    settle();
}

int main(int argc, char **argv) {
    QStringList args;
    for (int i = 1; i < argc; ++i) args << QString::fromLocal8Bit(argv[i]);
    auto arg = [&](const QString &flag, const QString &def) {
        const int i = args.indexOf(flag);
        return i >= 0 && i + 1 < args.size() ? args[i + 1] : def;
    };
    const bool real = args.contains(QStringLiteral("--real"));
    const QString themeName = arg(QStringLiteral("--theme"), QStringLiteral("dark"));
    g_out = QDir(arg(QStringLiteral("--out"), QStringLiteral("screenshots"))).absolutePath();
    g_prefix = (real ? QStringLiteral("real-") : QStringLiteral("mock-")) + themeName;
    QDir().mkpath(g_out);

    // Isolated Consiglio settings (never the user's).
    QTemporaryDir config;
    qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    QTemporaryDir fakeDir;
    if (!real) {
        const QString wrapper = fakeDir.filePath(QStringLiteral("hermes"));
        QFile f(wrapper);
        f.open(QIODevice::WriteOnly);
        f.write(QStringLiteral("#!/bin/sh\nexport FAKE_ACP_VERSION=0.23.3 FAKE_ACP_FULL_COMMANDS=1 FAKE_ACP_SESSIONS=many\n"
                               "exec python3 '%1' \"$@\"\n").arg(QStringLiteral(FAKE_ACP_SCRIPT)).toUtf8());
        f.close();
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        qputenv("PATH", fakeDir.path().toUtf8() + ":" + qgetenv("PATH"));
        qunsetenv("HERMES_BIN");
    }

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Consiglio"));
    app.setOrganizationName(QStringLiteral("Aniviza"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    QSettings().setValue(QStringLiteral("appearance/theme"), themeName);
    QSettings().setValue(QStringLiteral("session/workspace"), QDir::currentPath());
    Theme::instance().setMode(Theme::modeFromName(themeName));
    Theme::instance().apply(&app);

    g_log << "Tour: " << (real ? "real Hermes" : "fake agent") << ", theme " << themeName << " → " << g_out << "\n";

    MainWindow::Options opts;
    opts.centerOnScreen = false;
    MainWindow w(opts);
    w.resize(1280, 820);
    w.move(0, 0);
    w.show();
    QTest::qWait(30);
    shot(&w, "connecting");

    if (!waitFor([&]() { return w.client()->isConnected(); }, real ? 60000 : 10000)) {
        g_log << "Could not connect: " << w.stateText() << "\n";
        shot(&w, "connect-failed");
        return 1;
    }
    settle(600);
    shot(&w, "no-session");

    w.startNewSession();
    waitFor([&]() { return w.client()->isReady(); }, real ? 60000 : 10000);
    waitFor([&]() { return idle(w) && w.catalog()->listingComplete(); }, real ? 60000 : 10000);
    settle(500);
    g_log << "Catalog: " << w.catalog()->count() << " commands\n";
    shot(&w, "session-ready");

    runAndWait(w, QStringLiteral("hello world"), real ? 180000 : 10000);
    shot(&w, "hello-world");
    for (ChatBlock *b : w.chat()->blocks())
        if (b->kind() == ChatBlock::Agent) g_log << "  Hermes replied: " << b->plainText().left(300).replace('\n', ' ') << "\n";

    if (!real) {
        runAndWait(w, QStringLiteral("demo: what's in src and how do I build it?"), 10000);
        shot(&w, "rich-turn");
        w.runCommand(QStringLiteral("slow"));
        settle(1200);
        shot(&w, "busy");
        w.action(QStringLiteral("app:stop"))->trigger();
        waitFor([&]() { return idle(w); }, 10000);
        settle(300);
        shot(&w, "stopped");
    } else {
        runAndWait(w, QStringLiteral("/status"), 60000);
        shot(&w, "status-command");
        runAndWait(w, QStringLiteral("/tools"), 60000);
        shot(&w, "tools-command");
    }

    // Goal flow.
    {
        QDialog *d = w.createGoalDialog();
        if (auto *g = qobject_cast<GoalDialog *>(d)) g->setGoalText(QStringLiteral("Write release notes for 0.1 with a changelog"));
        d->show();
        shot(d, "goal-dialog");
        d->deleteLater();
    }
    runAndWait(w, QStringLiteral("/goal Write release notes for 0.1 with a changelog"), real ? 180000 : 10000);
    waitFor([&]() { return w.goalStatus().state == QLatin1String("active") && idle(w); }, real ? 60000 : 5000);
    settle(400);
    g_log << "Goal state: " << w.goalStatus().state << " — " << w.goalStatus().goal << "\n";
    shot(&w, "goal-active");

    tourMenus(w);
    tourDialogs(w, real);

    w.action(QStringLiteral("app:goal-pause"))->trigger();
    waitFor([&]() { return w.goalStatus().state == QLatin1String("paused") && idle(w); }, real ? 60000 : 5000);
    settle(400);
    shot(&w, "goal-paused");

    if (!real) {
        w.runCommand(QStringLiteral("permission"));
        waitFor([&]() { return findDialog(QStringLiteral("permissionDialog")) != nullptr; }, 5000);
        if (QDialog *p = findDialog(QStringLiteral("permissionDialog"))) {
            shot(p, "permission");
            p->reject();
        }
        waitFor([&]() { return idle(w); }, 5000);
    }

    w.resize(800, 720);
    settle(400);
    shot(&w, "narrow");
    w.resize(1280, 820);
    settle(300);

    if (!real) {
        w.runCommand(QStringLiteral("crash"));
        waitFor([&]() { return w.stateText().contains(QStringLiteral("Reconnect")); }, 5000);
        settle(100);
        shot(&w, "reconnecting");
        waitFor([&]() { return w.client()->isReady(); }, 15000);
        settle(600);
        shot(&w, "restored");

        MainWindow::Options bad;
        bad.executable = QStringLiteral("/nonexistent/hermes");
        bad.centerOnScreen = false;
        MainWindow failed(bad);
        failed.resize(1280, 820);
        failed.show();
        waitFor([&]() { return failed.stateText() == QObject::tr("Not connected"); }, 8000);
        settle(500);
        shot(&failed, "failed");
    }
    g_log << "Done: " << g_n << " screenshots\n";
    return 0;
}
