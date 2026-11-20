#pragma once

#include <QString>
#include <QStringList>

// Isolated test mode: Consiglio's settings and Hermes's data dir are redirected
// into a throwaway folder. ~/.hermes, the user's QSettings and shell profile
// are never read or written. Activate after QApplication exists and before
// any QSettings is constructed.
class AppSandbox {
public:
    // --sandbox, --sandbox=DIR, --sandbox DIR, or CONSIGLIO_SANDBOX (1/true/yes
    // or a path). The flag wins over the environment. Remaining args are
    // returned with the sandbox options removed, for QCommandLineParser.
    static bool parse(const QStringList &argv, QString *dir, QStringList *remaining);

    // Default folder when no path is given: <cache>/consiglio-sandbox.
    static QString defaultRoot();

    // Create the folders, point QSettings at <root>/consiglio and set
    // HERMES_HOME to <root>/.hermes. Returns false if the root couldn't be
    // created. Never copies from the user's existing Hermes or settings.
    static bool activate(const QString &root, QString *error = nullptr);

    static bool isActive();
    static QString root();
    static QString hermesHome();
    static QString consiglioDir();

    // Drop any previous activation (tests). Does not delete files.
    static void reset();
};
