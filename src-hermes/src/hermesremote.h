#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

// Finding Hermes on another machine.
//
// A remote agent is the same ACP session one hop away (see acptransport), but
// the launcher on the far side has to be found the same way the local one is:
// through a login shell, every time, without pinning a path. That is what this
// probe does — `ssh … sh -lc 'command -v hermes; hermes --version'` — and the
// answer (absolute path + version, or ssh's own error verbatim) is what the
// client then launches and what the UI shows.
//
// Anything user-supplied (destination, extra ssh options) is argv, never a
// local shell; the remote command is a single quoted string, which is exactly
// what ssh expects.
struct RemoteProbe {
    bool ok = false;
    QString executable;   // absolute path on the far side
    QString version;      // first line of `hermes --version`
    QString error;        // ssh stderr / timeout text, verbatim
    QString text() const; // one-line summary for the UI
};

class HermesRemote {
public:
    // argv for `ssh` that runs the probe. Exposed for tests and diagnostics.
    static QStringList probeArgv(const QString &destination, const QStringList &options);
    // The remote command string sent to ssh (single argv token).
    static QString probeCommand();
    // Parse the probe's stdout/stderr. Pure; the tests drive it directly.
    static RemoteProbe parse(const QByteArray &stdoutBytes, const QByteArray &stderrBytes, int exitCode);

    // Blocking probe with a timeout (tests, screenshot tour). The UI uses
    // probeAsync so a slow or unreachable host never freezes the window.
    static RemoteProbe probe(const QString &destination, const QStringList &options = QStringList(),
                             int timeoutMs = 20000);
    static void probeAsync(QObject *context, const QString &destination, const QStringList &options,
                           std::function<void(const RemoteProbe &)> done, int timeoutMs = 20000);

    // ── Preferences (QSettings: "remote/" keys) ──────────────────────────
    // Off by default: a local `hermes acp` works with no setup at all.
    static bool enabled();
    static void setEnabled(bool on);
    static QString destination();
    static void setDestination(const QString &destination);
    // Space-separated ssh options ("-p 2222 -i ~/.ssh/id_ed25519"); quoted
    // tokens are honoured.
    static QStringList options();
    static QString optionsLine();
    static void setOptions(const QString &line);
    static QString launcher();   // fallback name if the probe finds nothing
    static void setLauncher(const QString &launcher);
    static void clear();
};
