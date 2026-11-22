#include "hermesremote.h"

#include <QProcess>
#include <memory>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>

namespace {

const char *kEnabled = "remote/enabled";
const char *kDestination = "remote/destination";
const char *kOptions = "remote/options";
const char *kLauncher = "remote/launcher";

QString shellQuote(const QString &s) {
    QString q = s;
    q.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + q + QLatin1Char('\'');
}

// Common ssh flags for anything Consiglio runs against a remote Hermes:
//   -T              no tty: stdout is the protocol, nothing else
//   BatchMode       fail fast instead of prompting (there is no terminal to prompt on)
//   ConnectTimeout  a dead host is an error in seconds, not a hung window
//   accept-new      pin the key on first use; a *changed* key still fails
QStringList baseSshOptions() {
    return {QStringLiteral("-T"),
            QStringLiteral("-o"), QStringLiteral("BatchMode=yes"),
            QStringLiteral("-o"), QStringLiteral("ConnectTimeout=10"),
            QStringLiteral("-o"), QStringLiteral("StrictHostKeyChecking=accept-new")};
}

}  // namespace

QString HermesRemote::probeCommand() {
    // printf with markers so the answer survives anything else the shell prints
    // on login (motd, version-manager noise). Both lookups are silent on failure.
    return QStringLiteral("sh -lc %1").arg(shellQuote(
        QStringLiteral("printf 'PATH=%s\\n' \"$(command -v hermes 2>/dev/null)\"; "
                       "printf 'VER=%s\\n' \"$(hermes --version 2>/dev/null | head -n 1)\"")));
}

QStringList HermesRemote::probeArgv(const QString &destination, const QStringList &options) {
    QStringList argv = baseSshOptions();
    argv += options;
    argv << QStringLiteral("--") << destination << probeCommand();
    return argv;
}

RemoteProbe HermesRemote::parse(const QByteArray &stdout, const QByteArray &stderr, int exitCode) {
    RemoteProbe p;
    QString path;
    QString version;
    const QStringList lines = QString::fromUtf8(stdout).split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line.startsWith(QLatin1String("PATH="))) path = line.mid(5).trimmed();
        else if (line.startsWith(QLatin1String("VER="))) version = line.mid(4).trimmed();
    }
    if (!path.isEmpty()) {
        p.ok = true;
        p.executable = path;
        p.version = version;
        return p;
    }
    // No path: either the login shell has no hermes, or ssh itself failed.
    const QString err = QString::fromUtf8(stderr).trimmed();
    if (!err.isEmpty()) {
        p.error = err;
    } else if (exitCode != 0) {
        p.error = QObject::tr("ssh exited with code %1 and no message.").arg(exitCode);
    } else {
        p.error = QObject::tr("No hermes launcher was found in the remote login shell's PATH.");
    }
    return p;
}

RemoteProbe HermesRemote::probe(const QString &destination, const QStringList &options, int timeoutMs) {
    RemoteProbe p;
    if (destination.trimmed().isEmpty()) {
        p.error = QObject::tr("No SSH destination was given.");
        return p;
    }
    QProcess proc;
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("NO_COLOR"), QStringLiteral("1"));
    proc.setProcessEnvironment(env);
    proc.start(QStringLiteral("ssh"), probeArgv(destination, options));
    if (!proc.waitForStarted(8000)) {
        p.error = QObject::tr("Couldn't start ssh: %1").arg(proc.errorString());
        return p;
    }
    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        proc.waitForFinished(1000);
        p.error = QObject::tr("ssh timed out after %1 s.").arg(timeoutMs / 1000);
        return p;
    }
    return parse(proc.readAllStandardOutput(), proc.readAllStandardError(), proc.exitCode());
}

void HermesRemote::probeAsync(QObject *context, const QString &destination, const QStringList &options,
                              std::function<void(const RemoteProbe &)> done, int timeoutMs) {
    auto *proc = new QProcess(context);
    proc->setProcessChannelMode(QProcess::SeparateChannels);
    auto finished = std::make_shared<bool>(false);
    auto deliver = [proc, done, finished](const RemoteProbe &p) {
        if (*finished) return;
        *finished = true;
        proc->deleteLater();
        if (done) done(p);
    };
    QObject::connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), proc,
                     [proc, deliver](int code, QProcess::ExitStatus status) {
                         deliver(HermesRemote::parse(proc->readAllStandardOutput(),
                                                     proc->readAllStandardError(),
                                                     status == QProcess::NormalExit ? code : -1));
                     });
    QObject::connect(proc, &QProcess::errorOccurred, proc, [proc, deliver](QProcess::ProcessError err) {
        if (err != QProcess::FailedToStart) return;
        RemoteProbe p;
        p.error = QObject::tr("Couldn't start ssh: %1").arg(proc->errorString());
        deliver(p);
    });
    if (timeoutMs > 0) {
        QTimer::singleShot(timeoutMs, proc, [proc, deliver, timeoutMs]() {
            if (proc->state() == QProcess::NotRunning) return;
            proc->kill();
            RemoteProbe p;
            p.error = QObject::tr("ssh timed out after %1 s.").arg(timeoutMs / 1000);
            deliver(p);
        });
    }
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("NO_COLOR"), QStringLiteral("1"));
    proc->setProcessEnvironment(env);
    proc->start(QStringLiteral("ssh"), probeArgv(destination, options));
}

QString RemoteProbe::text() const {
    if (ok) {
        return version.isEmpty() ? executable : QStringLiteral("%1 — %2").arg(executable, version);
    }
    return error;
}

// ── Preferences ──────────────────────────────────────────────────────────

bool HermesRemote::enabled() { return QSettings().value(QLatin1String(kEnabled), false).toBool(); }
void HermesRemote::setEnabled(bool on) { QSettings().setValue(QLatin1String(kEnabled), on); }

QString HermesRemote::destination() {
    return QSettings().value(QLatin1String(kDestination)).toString().trimmed();
}
void HermesRemote::setDestination(const QString &destination) {
    QSettings settings;
    const QString trimmed = destination.trimmed();
    if (trimmed.isEmpty()) settings.remove(QLatin1String(kDestination));
    else settings.setValue(QLatin1String(kDestination), trimmed);
}

QString HermesRemote::optionsLine() {
    return QSettings().value(QLatin1String(kOptions)).toString().trimmed();
}
QStringList HermesRemote::options() { return QProcess::splitCommand(optionsLine()); }
void HermesRemote::setOptions(const QString &line) {
    QSettings settings;
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty()) settings.remove(QLatin1String(kOptions));
    else settings.setValue(QLatin1String(kOptions), trimmed);
}

QString HermesRemote::launcher() {
    const QString v = QSettings().value(QLatin1String(kLauncher)).toString().trimmed();
    return v.isEmpty() ? QStringLiteral("hermes") : v;
}
void HermesRemote::setLauncher(const QString &launcher) {
    QSettings settings;
    const QString trimmed = launcher.trimmed();
    if (trimmed.isEmpty() || trimmed == QLatin1String("hermes")) settings.remove(QLatin1String(kLauncher));
    else settings.setValue(QLatin1String(kLauncher), trimmed);
}

void HermesRemote::clear() {
    QSettings settings;
    settings.remove(QLatin1String(kEnabled));
    settings.remove(QLatin1String(kDestination));
    settings.remove(QLatin1String(kOptions));
    settings.remove(QLatin1String(kLauncher));
}
