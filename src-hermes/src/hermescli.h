#pragma once

#include <QObject>
#include <QPair>
#include <QPointer>
#include <QProcess>
#include <QStringList>
#include <functional>

#include "hermessetup.h"

// Runs the `hermes` CLI for the parts of setup/config that ACP doesn't cover
// (config show/set, doctor, update, provider setup). The executable is
// resolved on every call — the user's override if set, else $HERMES_BIN /
// PATH — so `hermes update` swapping the install underneath never strands us.
class HermesCli {
public:
    // The user's executable override ("" = automatic).
    static QString executableOverride();
    static void setExecutableOverride(const QString &path);
    static QString executable();

    // Run `hermes <args>` asynchronously. `done(exitCode, output)` gets
    // stdout+stderr with ANSI codes stripped; exitCode is -1 when the process
    // couldn't start or timed out. `progress` (optional) streams output.
    static QProcess *run(const QStringList &args, QObject *context,
                         std::function<void(int, const QString &)> done,
                         int timeoutMs = 60000,
                         std::function<void(const QString &)> progress = {});

    // Open `hermes <args>` in a terminal window (interactive wizards).
    // Returns false (with `error`) when no terminal could be started.
    static bool openInTerminal(const QStringList &args, QString *error = nullptr);
    // The terminal command line that openInTerminal would use (for tests/UI).
    static QStringList terminalCommand(const QStringList &args);
    // A terminal running `sh -c <command>` (POSIX), left open at the end.
    static QStringList terminalCommandForScript(const QString &command);
    // Run the official Hermes installer where the user can see and answer it.
    static bool openInstallerInTerminal(const HermesSetup::InstallCommand &command, QString *error = nullptr);

    static QString stripAnsi(const QString &text);

    // `hermes config show` → sections of label/value rows.
    struct ConfigRow { QString key; QString value; bool secret = false; bool set = true; };
    struct ConfigSection { QString title; QList<ConfigRow> rows; };
    static QList<ConfigSection> parseConfigShow(const QString &text);
};
