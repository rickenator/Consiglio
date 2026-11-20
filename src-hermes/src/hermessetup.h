#pragma once

#include <QMap>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

// First-run detection of an existing Hermes Agent, and the user's choice of
// how Consiglio should use it.
//
// Rules this module enforces:
//  - Detection is read-only. It looks for files and runs `<hermes> --version`;
//    it never creates, modifies or migrates anything in a Hermes data folder.
//  - "Use existing" stores no path and no version: the executable is still
//    resolved on every launch ($HERMES_BIN, PATH, standard folders), so
//    `hermes update` keeps working.
//  - A separate copy lives entirely under a folder the user picks: the
//    installer runs with HOME and HERMES_HOME pointed there, so it can't touch
//    the existing ~/.hermes, ~/.local/bin/hermes or shell profiles.
class HermesSetup {
public:
    // Inputs, so detection is testable without touching the process.
    struct Environment {
        QString home;
        QString path;          // PATH
        QString hermesBin;     // $HERMES_BIN
        QString hermesHome;    // $HERMES_HOME
        QString localAppData;  // %LOCALAPPDATA% (Windows)
        bool windows = false;
        static Environment current();
    };

    struct Detection {
        // Executable
        QString executable;    // empty = none found
        QString source;        // "HERMES_BIN", "PATH" or "standard location"
        QString version;       // first line of `--version`, may be empty
        QString hermesBinProblem;  // $HERMES_BIN set but not usable
        // Data folder (config, keys, sessions, skills)
        QString dataDir;       // the folder found, or the default if none
        bool dataExists = false;
        bool hasConfig = false;     // config.yaml
        bool hasKeys = false;       // .env (contents are never read)
        bool hasSessions = false;   // state.db
        qint64 sessionsBytes = 0;
        int skillCount = 0;         // entries in skills/
        bool hasCheckout = false;   // hermes-agent/ (the install itself)

        bool executableFound() const { return !executable.isEmpty(); }
        bool anythingFound() const { return executableFound() || dataExists; }
    };

    // versionTimeoutMs = 0 skips the version probe.
    static Detection detect(const Environment &env, int versionTimeoutMs = 5000);
    static QStringList standardExecutableDirs(const Environment &env);
    static QStringList dataDirCandidates(const Environment &env);

    enum class Choice { None, UseExisting, Custom, Separate, Installed, Skipped };
    static QString choiceName(Choice c);
    static Choice savedChoice();
    static bool needsSetup();
    // Stores the choice. Only Custom stores an executable path (one the user
    // picked); only Separate stores a folder (the one the user picked).
    static void saveChoice(Choice choice, const QString &customExecutable = QString(),
                           const QString &separateRoot = QString());
    static void reset();

    // Separate copy layout: everything under `root`.
    static QString separateRoot();
    static QString separateHermesHome(const QString &root);
    static QStringList separateExecutableCandidates(const QString &root, bool windows);
    static QString defaultSeparateRoot();
    static bool separateInstallSupported(const Environment &env);

    // What to launch: "" = automatic resolution (no pin). For a separate copy,
    // the copy's stable launcher, found again on every launch.
    static QString launchExecutable();
    // Environment for every Hermes process Consiglio starts.
    static QProcessEnvironment processEnvironment(QProcessEnvironment base = QProcessEnvironment::systemEnvironment());

    // The official installer, as a shell script (POSIX) or an argv
    // (Windows), for display and for running in a terminal.
    struct InstallCommand {
        QString display;          // what the user sees
        QString posixScript;      // run with `sh -c` in a terminal (POSIX)
        QStringList windowsArgv;  // program + args (Windows)
        QString hermesHome;       // where the install will write
    };
    static constexpr const char *kInstallShUrl = "https://hermes-agent.nousresearch.com/install.sh";
    static constexpr const char *kInstallPs1Url = "https://hermes-agent.nousresearch.com/install.ps1";
    // Default location install (only offered when nothing exists, or with an
    // explicit "use my existing data" confirmation).
    static InstallCommand officialInstall(const Environment &env);
    // Isolated install under `root`.
    static InstallCommand separateInstall(const Environment &env, const QString &root);

    static QString shellQuote(const QString &s);
};
