// First-run Hermes detection and consent: an existing Hermes is found,
// shown, and used as-is by default; nothing in it is ever changed without an
// explicit choice.
#include <QCryptographicHash>
#include <QDirIterator>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include "hermessetup.h"
#include "hermessetupdialog.h"

namespace {
using Snapshot = QMap<QString, QString>;

Snapshot snapshot(const QString &root) {
    Snapshot out;
    QDirIterator it(root, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo fi = it.fileInfo();
        QString sig = fi.isDir() ? QStringLiteral("dir") : QString::number(fi.size());
        sig += QLatin1Char('|') + QString::number(fi.lastModified().toMSecsSinceEpoch());
        if (fi.isFile()) {
            QFile f(path);
            if (f.open(QIODevice::ReadOnly))
                sig += QLatin1Char('|') + QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256).toHex();
        }
        out.insert(QDir(root).relativeFilePath(path), sig);
    }
    return out;
}

bool writeFile(const QString &path, const QByteArray &data, bool exec = false) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(data);
    f.close();
    if (exec) f.setPermissions(f.permissions() | QFileDevice::ExeOwner | QFileDevice::ReadOwner);
    return true;
}

// A fake `hermes` that prints a version and nothing else.
QString writeFakeHermes(const QString &dir, const QString &version = QStringLiteral("Hermes Agent v0.23.3")) {
#ifdef Q_OS_WIN
    const QString path = QDir(dir).filePath("hermes.cmd");
    writeFile(path, QStringLiteral("@echo off\r\necho %1\r\n").arg(version).toUtf8(), true);
#else
    const QString path = QDir(dir).filePath("hermes");
    writeFile(path, QStringLiteral("#!/bin/sh\necho '%1'\n").arg(version).toUtf8(), true);
#endif
    return path;
}

bool isWindows() {
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}
} // namespace

class HermesSetupTest : public QObject {
    Q_OBJECT
    QTemporaryDir m_settingsDir;
    QScopedPointer<QTemporaryDir> m_root;
    QString m_home;

    HermesSetup::Environment env() const {
        HermesSetup::Environment e;
        e.home = m_home;
        e.windows = isWindows();
        if (e.windows) e.localAppData = QDir(m_root->path()).filePath("localappdata");
        return e;
    }
    // A realistic existing install: launcher, checkout, config, keys,
    // sessions and skills.
    QString makeExistingInstall() {
        const QString hermesDir = QDir(m_home).filePath(".hermes");
        writeFile(hermesDir + "/config.yaml", "model:\n  default: qwen2.5\n");
        writeFile(hermesDir + "/.env", "OPENROUTER_API_KEY=sk-or-secret\n");
        writeFile(hermesDir + "/state.db", QByteArray(4096, 'x'));
        writeFile(hermesDir + "/skills/one/SKILL.md", "# one\n");
        writeFile(hermesDir + "/skills/two/SKILL.md", "# two\n");
        writeFile(hermesDir + "/hermes-agent/README.md", "checkout\n");
        const QString bin = isWindows() ? QDir(env().localAppData).filePath("hermes/bin")
                                        : QDir(m_home).filePath(".local/bin");
        return writeFakeHermes(bin);
    }
    QPushButton *button(QDialog &d, const char *name) { return d.findChild<QPushButton *>(name); }
    bool dialogShows(QDialog &d, const QString &needle) {
        for (QLabel *l : d.findChildren<QLabel *>()) if (l->text().contains(needle)) return true;
        return false;
    }

private slots:
    void initTestCase() {
        QVERIFY(m_settingsDir.isValid());
        QCoreApplication::setOrganizationName("AnivizaTest");
        QCoreApplication::setApplicationName("ConsiglioHermesSetupTest");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
    }
    void init() {
        m_root.reset(new QTemporaryDir);
        QVERIFY(m_root->isValid());
        m_home = QDir(m_root->path()).filePath("home");
        QDir().mkpath(m_home);
        QSettings().clear();
    }

    void detectsAnExistingInstall() {
        const QString exe = makeExistingInstall();
        const auto d = HermesSetup::detect(env());
        QCOMPARE(QDir::fromNativeSeparators(d.executable), QDir::fromNativeSeparators(exe));
        QCOMPARE(d.source, QStringLiteral("standard location"));
        QCOMPARE(d.version, QStringLiteral("Hermes Agent v0.23.3"));
        QVERIFY(d.dataExists);
        QVERIFY(d.hasConfig);
        QVERIFY(d.hasKeys);
        QVERIFY(d.hasSessions);
        QCOMPARE(d.sessionsBytes, qint64(4096));
        QCOMPARE(d.skillCount, 2);
        QVERIFY(d.hasCheckout);
    }

    void hermesBinThenPathThenStandardFolders() {
        makeExistingInstall();
        const QString other = writeFakeHermes(QDir(m_root->path()).filePath("other"), "Hermes Agent v9.9.9");
        auto e = env();
        e.hermesBin = other;
        auto d = HermesSetup::detect(e);
        QCOMPARE(d.source, QStringLiteral("HERMES_BIN"));
        QCOMPARE(d.version, QStringLiteral("Hermes Agent v9.9.9"));

        e.hermesBin.clear();
        e.path = QDir::toNativeSeparators(QDir(m_root->path()).filePath("other"));
        d = HermesSetup::detect(e);
        QCOMPARE(d.source, QStringLiteral("PATH"));

        e.path.clear();
        e.hermesBin = QDir(m_root->path()).filePath("missing/hermes");
        d = HermesSetup::detect(e);
        QVERIFY(!d.hermesBinProblem.isEmpty());
        QCOMPARE(d.source, QStringLiteral("standard location"));
    }

    void detectionIsReadOnly() {
        makeExistingInstall();
        const Snapshot before = snapshot(m_root->path());
        HermesSetup::detect(env());
        QCOMPARE(snapshot(m_root->path()), before);
    }

    void existingInstallPromptsAndDefaultsToUseExisting() {
        makeExistingInstall();
        QVERIFY(HermesSetup::needsSetup());
        HermesSetupDialog dlg(env(), HermesSetup::detect(env()));
        QVERIFY(dlg.isOptionAvailable(HermesSetupDialog::Option::UseExisting));
        QVERIFY(dlg.isOptionAvailable(HermesSetupDialog::Option::ChooseExecutable));
        QVERIFY(dlg.isOptionAvailable(HermesSetupDialog::Option::InstallSeparate));
        QVERIFY(!dlg.isOptionAvailable(HermesSetupDialog::Option::InstallOfficial));
        QCOMPARE(dlg.selectedOption(), HermesSetupDialog::Option::UseExisting);
        QVERIFY(dlg.commandPreview().contains("Nothing is installed"));
        QVERIFY(dialogShows(dlg, "Hermes Agent v0.23.3"));
        QVERIFY(dialogShows(dlg, QDir::toNativeSeparators(QDir(m_home).filePath(".hermes"))));
        QCOMPARE(button(dlg, "setupContinue")->text(), QStringLiteral("Continue"));
    }

    void useExistingChangesNoFiles() {
        makeExistingInstall();
        const Snapshot before = snapshot(m_root->path());
        int launches = 0;
        HermesSetupDialog dlg(env(), HermesSetup::detect(env()));
        dlg.setInstallerLauncher([&](const HermesSetup::InstallCommand &, QString *) { ++launches; return true; });
        QTest::mouseClick(button(dlg, "setupContinue"), Qt::LeftButton);
        QCOMPARE(dlg.result(), int(QDialog::Accepted));
        QCOMPARE(HermesSetup::savedChoice(), HermesSetup::Choice::UseExisting);
        QCOMPARE(launches, 0);
        // Nothing pinned: no executable path, no version, no HERMES_HOME.
        QVERIFY(!QSettings().contains("hermes/executable"));
        QCOMPARE(HermesSetup::launchExecutable(), QString());
        QVERIFY(!HermesSetup::processEnvironment(QProcessEnvironment()).contains("HERMES_HOME"));
        QVERIFY(!HermesSetup::needsSetup());
        QCOMPARE(snapshot(m_root->path()), before);
    }

    void nothingFoundOffersInstallAndSkip() {
        HermesSetupDialog dlg(env(), HermesSetup::detect(env()));
        QVERIFY(dlg.isOptionAvailable(HermesSetupDialog::Option::InstallOfficial));
        QVERIFY(dlg.isOptionAvailable(HermesSetupDialog::Option::ChooseExecutable));
        QVERIFY(!dlg.isOptionAvailable(HermesSetupDialog::Option::UseExisting));
        QVERIFY(!dlg.isOptionAvailable(HermesSetupDialog::Option::InstallSeparate));
        QCOMPARE(dlg.selectedOption(), HermesSetupDialog::Option::InstallOfficial);
        QVERIFY(dlg.commandPreview().contains(isWindows() ? "install.ps1" : "install.sh"));
        QTest::mouseClick(button(dlg, "setupSkip"), Qt::LeftButton);
        QCOMPARE(HermesSetup::savedChoice(), HermesSetup::Choice::Skipped);
        QVERIFY(QDir(m_home).isEmpty(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot));
    }

    void officialInstallRunsOnlyWhenChosen() {
        QList<HermesSetup::InstallCommand> launched;
        HermesSetupDialog dlg(env(), HermesSetup::detect(env()));
        dlg.setInstallerLauncher([&](const HermesSetup::InstallCommand &c, QString *) { launched << c; return true; });
        QCOMPARE(launched.size(), 0);
        QTest::mouseClick(button(dlg, "setupContinue"), Qt::LeftButton);
        QCOMPARE(launched.size(), 1);
        QVERIFY(dlg.installerStarted());
        QCOMPARE(button(dlg, "setupContinue")->text(), QStringLiteral("Check again"));
        QVERIFY(!dlg.isVisible() || dlg.result() != int(QDialog::Accepted));
        QCOMPARE(HermesSetup::savedChoice(), HermesSetup::Choice::None);
        // The installer "finishes": the next check picks it up, nothing pinned.
        makeExistingInstall();
        QTest::mouseClick(button(dlg, "setupContinue"), Qt::LeftButton);
        QCOMPARE(HermesSetup::savedChoice(), HermesSetup::Choice::Installed);
        QCOMPARE(HermesSetup::launchExecutable(), QString());
    }

    void existingDataWithoutExecutableNeedsExplicitConsent() {
        writeFile(QDir(m_home).filePath(".hermes/config.yaml"), "model: x\n");
        const Snapshot before = snapshot(m_root->path());
        int launches = 0;
        HermesSetupDialog dlg(env(), HermesSetup::detect(env()));
        dlg.setInstallerLauncher([&](const HermesSetup::InstallCommand &, QString *) { ++launches; return true; });
        QVERIFY(!dlg.isOptionAvailable(HermesSetupDialog::Option::UseExisting));
        QVERIFY(!dlg.isOptionAvailable(HermesSetupDialog::Option::InstallOfficial));
        QCOMPARE(dlg.selectedOption(), HermesSetupDialog::Option::ChooseExecutable);
        dlg.selectOption(HermesSetupDialog::Option::InstallIntoExistingData);
        QVERIFY(!button(dlg, "setupContinue")->isEnabled());
        QTest::mouseClick(button(dlg, "setupContinue"), Qt::LeftButton);
        QCOMPARE(launches, 0);
        QCOMPARE(snapshot(m_root->path()), before);
        dlg.findChild<QCheckBox *>("setupConsent")->setChecked(true);
        QVERIFY(button(dlg, "setupContinue")->isEnabled());
        QTest::mouseClick(button(dlg, "setupContinue"), Qt::LeftButton);
        QCOMPARE(launches, 1);
    }

    void separateCopyIsIsolated() {
        if (isWindows()) QSKIP("Separate copies are not offered on Windows");
        makeExistingInstall();
        const QString realData = QDir(m_home).filePath(".hermes");
        const Snapshot before = snapshot(m_home);
        QList<HermesSetup::InstallCommand> launched;
        HermesSetupDialog dlg(env(), HermesSetup::detect(env()));
        dlg.setInstallerLauncher([&](const HermesSetup::InstallCommand &c, QString *) { launched << c; return true; });
        dlg.selectOption(HermesSetupDialog::Option::InstallSeparate);
        // Folders that would overlap the existing Hermes are refused.
        dlg.setSeparateRoot(m_home);
        QVERIFY(!button(dlg, "setupContinue")->isEnabled());
        dlg.setSeparateRoot(realData + "/copy");
        QVERIFY(!button(dlg, "setupContinue")->isEnabled());
        const QString root = QDir(m_root->path()).filePath("separate hermes");
        dlg.setSeparateRoot(root);
        QVERIFY(button(dlg, "setupContinue")->isEnabled());
        QTest::mouseClick(button(dlg, "setupContinue"), Qt::LeftButton);
        QCOMPARE(launched.size(), 1);
        const QString script = launched.first().posixScript;
        QVERIFY(script.contains(QStringLiteral("HOME=%1").arg(HermesSetup::shellQuote(root))));
        QVERIFY(script.contains(QStringLiteral("HERMES_HOME=%1").arg(HermesSetup::shellQuote(root + "/.hermes"))));
        QVERIFY(script.contains("--hermes-home"));
        QVERIFY2(!script.contains(realData), qPrintable(script));
        // The copy appears; it is used with its own HERMES_HOME.
        writeFakeHermes(root + "/.local/bin");
        QTest::mouseClick(button(dlg, "setupContinue"), Qt::LeftButton);
        QCOMPARE(HermesSetup::savedChoice(), HermesSetup::Choice::Separate);
        QCOMPARE(HermesSetup::launchExecutable(), root + "/.local/bin/hermes");
        QCOMPARE(HermesSetup::processEnvironment(QProcessEnvironment()).value("HERMES_HOME"), root + "/.hermes");
        QCOMPARE(snapshot(m_home), before);
    }

    void windowsDoesNotOfferSeparateCopies() {
        makeExistingInstall();
        auto e = env();
        e.windows = true;
        QVERIFY(!HermesSetup::separateInstallSupported(e));
        HermesSetupDialog dlg(e, HermesSetup::detect(env()));
        QVERIFY(dlg.isOptionAvailable(HermesSetupDialog::Option::InstallSeparate));
        QVERIFY(!dlg.isOptionEnabled(HermesSetupDialog::Option::InstallSeparate));
        QVERIFY(HermesSetup::officialInstall(e).windowsArgv.join(' ').contains("install.ps1"));
    }

    void chooseExecutableStoresOnlyThatPath() {
        makeExistingInstall();
        const QString other = writeFakeHermes(QDir(m_root->path()).filePath("other"));
        HermesSetupDialog dlg(env(), HermesSetup::detect(env()));
        dlg.selectOption(HermesSetupDialog::Option::ChooseExecutable);
        dlg.setExecutablePath(other);
        QTest::mouseClick(button(dlg, "setupContinue"), Qt::LeftButton);
        QCOMPARE(HermesSetup::savedChoice(), HermesSetup::Choice::Custom);
        QCOMPARE(HermesSetup::launchExecutable(), other);
    }
};

QTEST_MAIN(HermesSetupTest)
#include "test_hermessetup.moc"
