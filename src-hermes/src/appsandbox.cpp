#include "appsandbox.h"

#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QFile>

namespace {
bool g_active = false;
QString g_root;
QString g_hermesHome;
QString g_consiglioDir;

bool isTruthy(const QString &v) {
    const QString s = v.trimmed().toLower();
    return s.isEmpty() || s == QLatin1String("1") || s == QLatin1String("true")
        || s == QLatin1String("yes") || s == QLatin1String("on");
}
} // namespace

bool AppSandbox::parse(const QStringList &argv, QString *dir, QStringList *remaining) {
    QString fromArg;
    bool flag = false;
    QStringList rest;
    rest.reserve(argv.size());
    for (int i = 0; i < argv.size(); ++i) {
        const QString a = argv.at(i);
        if (a == QLatin1String("--sandbox")) {
            flag = true;
            if (i + 1 < argv.size() && !argv.at(i + 1).startsWith(QLatin1Char('-'))) {
                fromArg = argv.at(++i);
            }
            continue;
        }
        if (a.startsWith(QLatin1String("--sandbox="))) {
            flag = true;
            fromArg = a.mid(int(QStringLiteral("--sandbox=").size()));
            continue;
        }
        rest.append(a);
    }

    QString fromEnv;
    bool envSet = qEnvironmentVariableIsSet("CONSIGLIO_SANDBOX");
    if (envSet) fromEnv = qEnvironmentVariable("CONSIGLIO_SANDBOX");

    if (!flag && !envSet) {
        if (remaining) *remaining = argv;
        return false;
    }
    if (dir) {
        if (!fromArg.isEmpty()) *dir = fromArg;
        else if (!fromEnv.isEmpty() && !isTruthy(fromEnv)) *dir = fromEnv;
        else dir->clear();
    }
    if (remaining) *remaining = rest;
    return true;
}

QString AppSandbox::defaultRoot() {
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    return QDir(cache.isEmpty() ? QDir::tempPath() : cache).filePath(QStringLiteral("consiglio-sandbox"));
}

bool AppSandbox::activate(const QString &root, QString *error) {
    const QString resolved = QDir::cleanPath(root.isEmpty() ? defaultRoot() : root);
    QDir dir;
    const QString consiglio = QDir(resolved).filePath(QStringLiteral("consiglio"));
    const QString hermes = QDir(resolved).filePath(QStringLiteral(".hermes"));
    if (!dir.mkpath(consiglio) || !dir.mkpath(hermes)) {
        if (error) *error = QStringLiteral("couldn't create sandbox folders under %1").arg(resolved);
        return false;
    }

    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, consiglio);
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, consiglio);
    qputenv("HERMES_HOME", QFile::encodeName(hermes));
    qputenv("CONSIGLIO_SANDBOX", QFile::encodeName(resolved));

    g_root = resolved;
    g_consiglioDir = consiglio;
    g_hermesHome = hermes;
    g_active = true;
    return true;
}

bool AppSandbox::isActive() { return g_active; }
QString AppSandbox::root() { return g_root; }
QString AppSandbox::hermesHome() { return g_hermesHome; }
QString AppSandbox::consiglioDir() { return g_consiglioDir; }

void AppSandbox::reset() {
    g_active = false;
    g_root.clear();
    g_hermesHome.clear();
    g_consiglioDir.clear();
}
