#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>

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

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Native desktop client for the Hermes Agent"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption hermesOpt(QStringLiteral("hermes"),
                                 QStringLiteral("Path to the hermes executable (default: settings, $HERMES_BIN, PATH)."),
                                 QStringLiteral("path"));
    parser.addOption(hermesOpt);
    parser.process(app);

    // Style, palette, stylesheet and font all come from the theme tokens.
    Theme::instance().apply(&app);

    // First run: find an existing Hermes and ask before anything is installed
    // or changed. Skipped when --hermes names an executable explicitly.
    if (!parser.isSet(hermesOpt)) HermesSetupDialog::runIfNeeded();

    MainWindow::Options options;
    options.executable = parser.value(hermesOpt);
    MainWindow window(options);
    window.show();
    return app.exec();
}
