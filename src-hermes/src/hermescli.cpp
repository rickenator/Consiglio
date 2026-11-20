#include "hermescli.h"

#include "acpclient.h"
#include "uiutil.h"

#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

QString HermesCli::executableOverride() {
    return QSettings().value("hermes/executable").toString().trimmed();
}

void HermesCli::setExecutableOverride(const QString &path) {
    QSettings settings;
    if (path.trimmed().isEmpty()) settings.remove("hermes/executable");
    else settings.setValue("hermes/executable", path.trimmed());
}

QString HermesCli::executable() {
    return AcpClient::resolveExecutable(executableOverride());
}

QString HermesCli::stripAnsi(const QString &text) {
    static const QRegularExpression ansi(QStringLiteral("\\x1B(?:\\[[0-?]*[ -/]*[@-~]|\\][^\\x07]*\\x07|[@-Z\\\\-_])"));
    QString out = text;
    out.remove(ansi);
    out.remove(QLatin1Char('\r'));
    return out;
}

QProcess *HermesCli::run(const QStringList &args, QObject *context,
                         std::function<void(int, const QString &)> done, int timeoutMs,
                         std::function<void(const QString &)> progress) {
    auto *proc = new QProcess(context);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("NO_COLOR", "1");
    env.insert("TERM", "dumb");
    env.insert("PYTHONIOENCODING", "utf-8");
    env.insert("HERMES_NO_PAGER", "1");
    proc->setProcessEnvironment(env);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    auto output = std::make_shared<QString>();
    auto finished = std::make_shared<bool>(false);
    auto finish = [proc, done, output, finished](int code) {
        if (*finished) return;
        *finished = true;
        if (done) done(code, stripAnsi(*output));
        proc->deleteLater();
    };
    QObject::connect(proc, &QProcess::readyRead, proc, [proc, output, progress]() {
        const QString chunk = QString::fromUtf8(proc->readAll());
        output->append(chunk);
        if (progress) progress(stripAnsi(chunk));
    });
    QObject::connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), proc,
                     [proc, output, finish](int code, QProcess::ExitStatus status) {
                         output->append(QString::fromUtf8(proc->readAll()));
                         finish(status == QProcess::NormalExit ? code : -1);
                     });
    QObject::connect(proc, &QProcess::errorOccurred, proc, [proc, output, finish](QProcess::ProcessError err) {
        if (err == QProcess::FailedToStart) {
            output->append(QObject::tr("Could not start “%1”: %2").arg(proc->program(), proc->errorString()));
            finish(-1);
        }
    });
    if (timeoutMs > 0) {
        QTimer::singleShot(timeoutMs, proc, [proc, output, finish]() {
            if (proc->state() != QProcess::NotRunning) {
                output->append(QObject::tr("\n(timed out)"));
                proc->kill();
                finish(-1);
            }
        });
    }
    proc->start(executable(), args);
    return proc;
}

static QString shellQuote(const QString &s) {
    QString q = s;
    q.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + q + QLatin1Char('\'');
}

QStringList HermesCli::terminalCommand(const QStringList &args) {
    QStringList parts{shellQuote(executable())};
    for (const QString &a : args) parts << shellQuote(a);
    const QString script = parts.join(' ')
        + QStringLiteral("; status=$?; echo; printf 'Press Enter to close… '; read _; exit $status");
#if defined(Q_OS_MACOS)
    QString escaped = script;
    escaped.replace('\\', "\\\\").replace('"', "\\\"");
    return {"osascript", "-e", QStringLiteral("tell application \"Terminal\" to do script \"%1\"").arg(escaped),
            "-e", "tell application \"Terminal\" to activate"};
#elif defined(Q_OS_WIN)
    QStringList cmd{"cmd.exe", "/c", "start", "Hermes", "cmd.exe", "/k", executable()};
    cmd += args;
    return cmd;
#else
    struct Term { const char *bin; QStringList prefix; };
    const QList<Term> terms = {
        {"x-terminal-emulator", {"-e"}}, {"gnome-terminal", {"--"}}, {"konsole", {"-e"}},
        {"xfce4-terminal", {"-x"}}, {"kitty", {}}, {"alacritty", {"-e"}}, {"wezterm", {"start", "--"}},
        {"foot", {}}, {"tilix", {"-e"}}, {"xterm", {"-e"}},
    };
    QStringList candidates;
    const QString preferred = qEnvironmentVariable("TERMINAL");
    if (!preferred.isEmpty()) {
        const QString path = QStandardPaths::findExecutable(preferred);
        if (!path.isEmpty()) return {path, "-e", "sh", "-c", script};
    }
    for (const auto &t : terms) {
        const QString path = QStandardPaths::findExecutable(QString::fromLatin1(t.bin));
        if (path.isEmpty()) continue;
        QStringList cmd{path};
        cmd += t.prefix;
        cmd << "sh" << "-c" << script;
        return cmd;
    }
    return {};
#endif
}

bool HermesCli::openInTerminal(const QStringList &args, QString *error) {
    const QStringList cmd = terminalCommand(args);
    if (cmd.isEmpty()) {
        if (error) {
            *error = QObject::tr("No terminal emulator was found. Run this in a terminal instead:\n\n%1 %2")
                         .arg(executable(), args.join(' '));
        }
        return false;
    }
    if (!QProcess::startDetached(cmd.first(), cmd.mid(1))) {
        if (error) *error = QObject::tr("Could not open a terminal (%1).").arg(cmd.first());
        return false;
    }
    return true;
}

QList<HermesCli::ConfigSection> HermesCli::parseConfigShow(const QString &raw) {
    QList<ConfigSection> sections;
    const QString text = stripAnsi(raw);
    static const QRegularExpression colon(QStringLiteral("^\\s{1,4}([^:]{1,40}?):\\s+(.*)$"));
    static const QRegularExpression spaced(QStringLiteral("^\\s{1,4}(\\S.*?)\\s{2,}(\\S.*)$"));
    static const QRegularExpression notSet(QStringLiteral("^\\s{1,4}(\\S.*?)\\s+\\(not set\\)\\s*$"));
    bool inApiKeys = false;
    for (const QString &line : text.split('\n')) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;
        const QChar first = trimmed.at(0);
        if (first == QChar(0x250C) || first == QChar(0x2502) || first == QChar(0x2514)) continue;  // ┌ │ └
        if (first == QChar(0x2500)) break;  // ──── footer
        if (first == QChar(0x25C6)) {       // ◆ Section
            ConfigSection s;
            s.title = trimmed.mid(1).trimmed();
            inApiKeys = s.title.contains(QLatin1String("key"), Qt::CaseInsensitive)
                        || s.title.contains(QLatin1String("secret"), Qt::CaseInsensitive);
            sections.append(s);
            continue;
        }
        if (sections.isEmpty()) continue;
        ConfigRow row;
        if (const auto m = notSet.match(line); m.hasMatch()) {
            row.key = m.captured(1).trimmed();
            if (row.key.endsWith(':')) row.key.chop(1);
            row.value = QObject::tr("Not set");
            row.set = false;
        } else if (const auto c = colon.match(line); c.hasMatch()) {
            row.key = c.captured(1).trimmed();
            row.value = c.captured(2).trimmed();
        } else if (const auto s = spaced.match(line); s.hasMatch()) {
            row.key = s.captured(1).trimmed();
            row.value = s.captured(2).trimmed();
        } else {
            row.key = trimmed;
        }
        if (inApiKeys) {
            row.secret = true;
            if (row.set) row.value = QObject::tr("Configured");
        } else {
            row.value = ui::maskSecrets(row.value);
            if (row.value == QLatin1String("not configured")) row.set = false;
        }
        // Python dict values ("{'default': 'x', 'provider': 'custom'}") → one row each.
        if (row.value.startsWith('{') && row.value.endsWith('}')) {
            static const QRegularExpression pair(
                QStringLiteral("'([\\w.-]+)'\\s*:\\s*(?:'([^']*)'|\"([^\"]*)\"|([^,}]+))"));
            auto it = pair.globalMatch(row.value);
            bool any = false;
            while (it.hasNext()) {
                const auto m = it.next();
                ConfigRow sub;
                QString k = m.captured(1);
                k.replace('_', ' ');
                if (k == QLatin1String("base url")) k = QStringLiteral("base URL");
                if (!k.isEmpty()) k[0] = k.at(0).toUpper();
                sub.key = row.key + QStringLiteral(" · ") + k;
                QString v = m.captured(2);
                if (v.isEmpty()) v = m.captured(3);
                if (v.isEmpty()) v = m.captured(4).trimmed();
                sub.value = ui::maskSecrets(v);
                sections.last().rows.append(sub);
                any = true;
            }
            if (any) continue;
        }
        sections.last().rows.append(row);
    }
    return sections;
}
