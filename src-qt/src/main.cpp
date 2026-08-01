#include <QApplication>
#include <QStyleFactory>
#include <QFontDatabase>
#include "mainwindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Consiglio");
    app.setApplicationVersion("0.2.0");
    app.setOrganizationName("Aniviza");
    app.setOrganizationDomain("aniviza.com");

    // Dark style — native Qt dark palette
    app.setStyle(QStyleFactory::create("Fusion"));
    
    QFont font("Segoe UI", 10);
    font.setStyleHint(QFont::SansSerif);
    app.setFont(font);

    MainWindow window;
    window.show();
    return app.exec();
}
