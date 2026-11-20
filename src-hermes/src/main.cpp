#include <QApplication>
#include <QCoreApplication>
#include <QCommandLineParser>
#include <cstdio>
#include <QIcon>

#include "appsandbox.h"
#include "hermessetupdialog.h"
#include "mainwindow.h"
#include "theme.h"

int main(int argc, char *argv[]) {
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Consiglio"));
    app.setApplicationDisplayName(QStringLiteral("Consiglio · Hermes"));
    app.setOrganizationName(QStringLiteral("Aniviza"));
    app.setApplicationVersion(QStringLiteral(CONSIGLIO_HERMES_VERSION));
    app.setWindowIcon(QIcon(QStringLiteral(":/logo.svg")));

    QString sandboxDir;
    QStringList remaining;
    const bool sandbox = AppSandbox::parse(QCoreApplication::arguments(), &sandboxDir, &remaining);
    if (sandbox) {
        QString err;
        if (!AppSandbox::activate(sandboxDir, &err)) {
            std::fprintf(stderr, "consiglio: sandbox: %s\n", qPrintable(err));
            return 1;
        }
        app.setApplicationDisplayName(QStringLiteral("Consiglio · Hermes — Sandbox"));
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Native desktop client for the Hermes Agent"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption hermesOpt(QStringLiteral("hermes"),
                                 QStringLiteral("Path to the hermes executable (default: settings, $HERMES_BIN, PATH)."),
                                 QStringLiteral("path"));
    parser.addOption(hermesOpt);
    parser.addOption(QCommandLineOption(QStringLiteral("sandbox"),
        QStringLiteral("Isolated test mode: Consiglio settings and HERMES_HOME go under a throwaway folder "
                       "(default: the user cache). Does not read or write ~/.hermes. Optional directory: "
                       "--sandbox=DIR or --sandbox DIR.")));
    parser.process(remaining);

    // Style, palette, stylesheet and font all come from the theme tokens.
    Theme::instance().apply(&app);

    // First run: find an existing Hermes and ask before anything is installed
    // or changed. Skipped when --hermes names an executable explicitly, and in
    // sandbox mode (the official installer must not touch the real home).
    if (!sandbox && !parser.isSet(hermesOpt)) HermesSetupDialog::runIfNeeded();

    MainWindow::Options options;
    options.executable = parser.value(hermesOpt);
    MainWindow window(options);
    window.show();
    return app.exec();
}
