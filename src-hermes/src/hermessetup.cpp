#include "hermessetup.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

namespace {
const char *kChoiceKey = "hermes/setupChoice";
const char *kSeparateKey = "hermes/separateRoot";
const char *kExecutableKey = "hermes/executable";   // shared with HermesCli's override

bool isExecutableFile(const QString &path) {
    const QFileInfo fi(path);
    return fi.exists() && fi.isFile() && fi.isExecutable();
}

QString findIn(const QStringList &dirs, bool windows) {
    if (dirs.isEmpty()) return {};
    // QStandardPaths honours PATHEXT on Windows.
    const QString found = QStandardPaths::findExecutable(QStringLiteral("hermes"), dirs);
    if (!found.isEmpty()) return found;
    if (windows) {
        for (const QString &d : dirs) {
            for (const char *ext : {".exe", ".cmd", ".bat"}) {
                const QString p = QDir(d).filePath(QStringLiteral("hermes") + QLatin1String(ext));
                if (QFileInfo::exists(p)) return QDir::toNativeSeparators(p);
            }
        }
    }
    return {};
}

QString join(const QString &a, const QString &b) { return QDir(a).filePath(b); }
} // namespace

HermesSetup::Environment HermesSetup::Environment::current() {
    Environment e;
    e.home = QDir::homePath();
    e.path = qEnvironmentVariable("PATH");
    e.hermesBin = qEnvironmentVariable("HERMES_BIN").trimmed();
    e.hermesHome = qEnvironmentVariable("HERMES_HOME").trimmed();
    e.localAppData = qEnvironmentVariable("LOCALAPPDATA").trimmed();
#ifdef Q_OS_WIN
    e.windows = true;
#endif
    return e;
}

QStringList HermesSetup::standardExecutableDirs(const Environment &env) {
    QStringList dirs;
    if (env.windows) {
        if (!env.hermesHome.isEmpty()) dirs << join(env.hermesHome, "bin");
        if (!env.localAppData.isEmpty()) {
            dirs << join(env.localAppData, "hermes/bin")
                 << join(env.localAppData, "hermes/hermes-agent/venv/Scripts");
        }
        dirs << join(env.home, ".local/bin");
    } else {
        dirs << join(env.home, ".local/bin");
        if (!env.hermesHome.isEmpty()) dirs << join(env.hermesHome, "hermes-agent/venv/bin");
        dirs << join(env.home, ".hermes/hermes-agent/venv/bin")
             << QStringLiteral("/usr/local/bin") << QStringLiteral("/opt/homebrew/bin")
             << join(env.home, "bin");
    }
    dirs.removeDuplicates();
    return dirs;
}

QStringList HermesSetup::dataDirCandidates(const Environment &env) {
    QStringList dirs;
    if (!env.hermesHome.isEmpty()) dirs << env.hermesHome;
    if (env.windows && !env.localAppData.isEmpty()) dirs << join(env.localAppData, "hermes");
    dirs << join(env.home, ".hermes");
    dirs.removeDuplicates();
    return dirs;
}

HermesSetup::Detection HermesSetup::detect(const Environment &env, int versionTimeoutMs) {
    Detection d;

    // 1. $HERMES_BIN
    if (!env.hermesBin.isEmpty()) {
        if (isExecutableFile(env.hermesBin)) {
            d.executable = env.hermesBin;
            d.source = QStringLiteral("HERMES_BIN");
        } else {
            d.hermesBinProblem = QObject::tr("HERMES_BIN is set to “%1”, which isn't an executable file.")
                                     .arg(env.hermesBin);
        }
    }
    // 2. PATH
    if (d.executable.isEmpty()) {
        const QStringList pathDirs = env.path.split(env.windows ? QLatin1Char(';') : QLatin1Char(':'),
                                                    Qt::SkipEmptyParts);
        const QString p = findIn(pathDirs, env.windows);
        if (!p.isEmpty()) { d.executable = p; d.source = QStringLiteral("PATH"); }
    }
    // 3. Standard install folders
    if (d.executable.isEmpty()) {
        const QString p = findIn(standardExecutableDirs(env), env.windows);
        if (!p.isEmpty()) { d.executable = p; d.source = QStringLiteral("standard location"); }
    }

    // Data folder: config, keys, sessions, skills. Presence only.
    const QStringList candidates = dataDirCandidates(env);
    for (const QString &dir : candidates) {
        const QDir qd(dir);
        if (!qd.exists()) continue;
        d.dataDir = QDir::toNativeSeparators(qd.absolutePath());
        d.dataExists = true;
        d.hasConfig = QFileInfo::exists(qd.filePath("config.yaml"));
        d.hasKeys = QFileInfo::exists(qd.filePath(".env"));
        const QFileInfo db(qd.filePath("state.db"));
        d.hasSessions = db.exists();
        d.sessionsBytes = db.exists() ? db.size() : 0;
        const QDir skills(qd.filePath("skills"));
        d.skillCount = skills.exists()
            ? skills.entryList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot).size() : 0;
        d.hasCheckout = QDir(qd.filePath("hermes-agent")).exists();
        break;
    }
    if (!d.dataExists && !candidates.isEmpty())
        d.dataDir = QDir::toNativeSeparators(candidates.first());

    // Version: ask the executable itself (read-only for Consiglio).
    if (!d.executable.isEmpty() && versionTimeoutMs > 0) {
        QProcess p;
        p.setProcessChannelMode(QProcess::MergedChannels);
        QProcessEnvironment pe = QProcessEnvironment::systemEnvironment();
        pe.insert("NO_COLOR", "1");
        p.setProcessEnvironment(pe);
        p.start(d.executable, {QStringLiteral("--version")});
        if (p.waitForFinished(versionTimeoutMs) && p.exitStatus() == QProcess::NormalExit) {
            static const QRegularExpression ansi(QStringLiteral("\\x1B\\[[0-?]*[ -/]*[@-~]"));
            QString out = QString::fromUtf8(p.readAll());
            out.remove(ansi);
            const QStringList lines = out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            if (!lines.isEmpty()) d.version = lines.first().trimmed();
        } else {
            p.kill();
            p.waitForFinished(1000);
        }
    }
    return d;
}

QString HermesSetup::choiceName(Choice c) {
    switch (c) {
    case Choice::UseExisting: return QStringLiteral("existing");
    case Choice::Custom: return QStringLiteral("custom");
    case Choice::Separate: return QStringLiteral("separate");
    case Choice::Installed: return QStringLiteral("installed");
    case Choice::Skipped: return QStringLiteral("skipped");
    case Choice::None: break;
    }
    return {};
}

HermesSetup::Choice HermesSetup::savedChoice() {
    const QString v = QSettings().value(kChoiceKey).toString();
    for (Choice c : {Choice::UseExisting, Choice::Custom, Choice::Separate, Choice::Installed, Choice::Skipped})
        if (v == choiceName(c)) return c;
    return Choice::None;
}

bool HermesSetup::needsSetup() { return savedChoice() == Choice::None; }

void HermesSetup::saveChoice(Choice choice, const QString &customExecutable, const QString &separateRoot) {
    QSettings s;
    s.setValue(kChoiceKey, choiceName(choice));
    if (choice == Choice::Custom) s.setValue(kExecutableKey, customExecutable.trimmed());
    else if (choice == Choice::UseExisting || choice == Choice::Installed || choice == Choice::Separate)
        s.remove(kExecutableKey);   // automatic resolution: nothing pinned
    if (choice == Choice::Separate) s.setValue(kSeparateKey, separateRoot);
    else s.remove(kSeparateKey);
}

void HermesSetup::reset() {
    QSettings s;
    s.remove(kChoiceKey);
    s.remove(kSeparateKey);
}

QString HermesSetup::separateRoot() {
    if (savedChoice() != Choice::Separate) return {};
    return QSettings().value(kSeparateKey).toString();
}

QString HermesSetup::separateHermesHome(const QString &root) { return QDir(root).filePath(".hermes"); }

QStringList HermesSetup::separateExecutableCandidates(const QString &root, bool windows) {
    const QString home = separateHermesHome(root);
    if (windows) return {join(home, "bin/hermes.exe"), join(home, "bin/hermes.cmd")};
    return {join(root, ".local/bin/hermes"), join(home, "hermes-agent/.hermes/bin/hermes"),
            join(home, "hermes-agent/venv/bin/hermes")};
}

QString HermesSetup::defaultSeparateRoot() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("hermes-separate"));
}

bool HermesSetup::separateInstallSupported(const Environment &env) {
    // The Windows installer always prepends its bin folder to the user PATH,
    // which would change which `hermes` existing terminals run.
    return !env.windows;
}

QString HermesSetup::launchExecutable() {
    const Choice c = savedChoice();
    if (c == Choice::Separate) {
        const QString root = separateRoot();
#ifdef Q_OS_WIN
        const bool win = true;
#else
        const bool win = false;
#endif
        for (const QString &p : separateExecutableCandidates(root, win))
            if (QFileInfo::exists(p)) return p;
        return separateExecutableCandidates(root, win).first();
    }
    if (c == Choice::Custom) return QSettings().value(kExecutableKey).toString().trimmed();
    return QSettings().value(kExecutableKey).toString().trimmed();   // "" unless the user set an override
}

QProcessEnvironment HermesSetup::processEnvironment(QProcessEnvironment base) {
    const QString root = separateRoot();
    if (!root.isEmpty()) base.insert(QStringLiteral("HERMES_HOME"), separateHermesHome(root));
    return base;
}

QString HermesSetup::shellQuote(const QString &s) {
    QString q = s;
    q.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + q + QLatin1Char('\'');
}

HermesSetup::InstallCommand HermesSetup::officialInstall(const Environment &env) {
    InstallCommand c;
    c.hermesHome = dataDirCandidates(env).isEmpty() ? QString() : dataDirCandidates(env).first();
    if (env.windows) {
        const QString cmd = QStringLiteral("iex (irm %1)").arg(QLatin1String(kInstallPs1Url));
        c.windowsArgv = QStringList{QStringLiteral("powershell.exe"), QStringLiteral("-NoProfile"), QStringLiteral("-NoExit"),
                                    QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"), QStringLiteral("-Command"), cmd};
        c.display = cmd;
    } else {
        c.posixScript = QStringLiteral("curl -fsSL %1 | bash").arg(shellQuote(QLatin1String(kInstallShUrl)));
        c.display = QStringLiteral("curl -fsSL %1 | bash").arg(QLatin1String(kInstallShUrl));
    }
    return c;
}

HermesSetup::InstallCommand HermesSetup::separateInstall(const Environment &env, const QString &root) {
    InstallCommand c;
    const QString home = separateHermesHome(root);
    const QString dir = QDir(home).filePath("hermes-agent");
    c.hermesHome = home;
    if (env.windows) return c;   // not supported: see separateInstallSupported()
    // HOME is pointed at the separate root too, so the installer's launcher
    // (~/.local/bin/hermes), tool store and shell-profile edits land there
    // instead of in the user's real home.
    c.posixScript = QStringLiteral("mkdir -p %1 && export HOME=%1 HERMES_HOME=%2 && "
                                   "curl -fsSL %3 | bash -s -- --dir %4 --hermes-home %2")
                        .arg(shellQuote(root), shellQuote(home), shellQuote(QLatin1String(kInstallShUrl)),
                             shellQuote(dir));
    c.display = QStringLiteral("HOME=%1 HERMES_HOME=%2 \\\n  curl -fsSL %3 | bash -s -- --dir %4 --hermes-home %2")
                    .arg(shellQuote(root), shellQuote(home), QLatin1String(kInstallShUrl), shellQuote(dir));
    return c;
}
