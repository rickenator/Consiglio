// Screenshots of the first-run "Set up Hermes Agent" dialog.
//
//   QT_QPA_PLATFORM=offscreen ./setuptour --out DIR [--theme light|dark] [--real]
//
// --real detects the Hermes really installed for this user (read-only: file
// checks and `hermes --version`). The other states use a temporary fake home.
#include "hermessetupdialog.h"
#include "theme.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>

static void write(const QString &path, const QByteArray &data, bool exec = false) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    f.open(QIODevice::WriteOnly | QIODevice::Truncate);
    f.write(data);
    f.close();
    if (exec) f.setPermissions(f.permissions() | QFileDevice::ExeOwner);
}

static void shot(HermesSetupDialog &dlg, const QString &file) {
    dlg.show();
    dlg.adjustSize();
    QTest::qWait(200);
    dlg.grab().save(file);
    QTextStream(stdout) << "  " << file << "\n";
    dlg.hide();
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QString out = ".", themeName = "dark";
    bool real = false;
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args[i] == "--out" && i + 1 < args.size()) out = args[++i];
        else if (args[i] == "--theme" && i + 1 < args.size()) themeName = args[++i];
        else if (args[i] == "--real") real = true;
    }
    QTemporaryDir settingsDir;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    app.setOrganizationName("AnivizaTour");
    app.setApplicationName("ConsiglioSetupTour");
    Theme::instance().setMode(Theme::modeFromName(themeName));
    Theme::instance().apply(&app);
    QDir().mkpath(out);
    const QString pre = QStringLiteral("%1/setup-%2-").arg(out, themeName);

    if (real) {
        const auto env = HermesSetup::Environment::current();
        HermesSetupDialog dlg(env, HermesSetup::detect(env));
        shot(dlg, pre + "01-existing-real.png");
        dlg.selectOption(HermesSetupDialog::Option::InstallSeparate);
        shot(dlg, pre + "02-existing-real-separate.png");
    }

    QTemporaryDir root;
    const QString home = root.filePath("home");
    HermesSetup::Environment env;
    env.home = home;
    {   // Nothing installed.
        QDir().mkpath(home);
        HermesSetupDialog dlg(env, HermesSetup::detect(env));
        shot(dlg, pre + "03-none-found.png");
    }
    {   // Data but no executable: explicit consent needed.
        write(home + "/.hermes/config.yaml", "model: x\n");
        write(home + "/.hermes/.env", "KEY=x\n");
        HermesSetupDialog dlg(env, HermesSetup::detect(env));
        dlg.selectOption(HermesSetupDialog::Option::InstallIntoExistingData);
        shot(dlg, pre + "04-data-only-consent.png");
    }
    return 0;
}
