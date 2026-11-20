#include <QApplication>
#include "mainwindow.h"

namespace {
// Global dark theme. The chat pane, composer, and palette carry their own
// per-widget styles; this stylesheet covers the window chrome (menu bar,
// status bar, central area, session rail) so nothing falls back to Qt's
// default light theme.
const char *kDarkTheme = R"(
    QMainWindow, QWidget {
        background: #0d1117;
        color: #c9d1d9;
    }
    QMenuBar {
        background: #161b22;
        color: #c9d1d9;
        border-bottom: 1px solid #30363d;
    }
    QMenuBar::item {
        background: transparent;
        padding: 4px 8px;
    }
    QMenuBar::item:selected {
        background: #21262d;
        border-radius: 6px;
    }
    QMenu {
        background: #161b22;
        color: #c9d1d9;
        border: 1px solid #30363d;
        border-radius: 8px;
        padding: 4px;
    }
    QMenu::item {
        padding: 6px 24px 6px 12px;
        border-radius: 6px;
    }
    QMenu::item:selected {
        background: rgba(88, 166, 255, 0.18);
        color: #f0f6fc;
    }
    QMenu::item:disabled {
        color: #484f58;
    }
    QStatusBar {
        background: #161b22;
        color: #8b949e;
        border-top: 1px solid #30363d;
        font-size: 23px;
    }
    QStatusBar::item {
        border: none;
    }
    QLabel {
        color: #c9d1d9;
    }
    QPushButton {
        background: #21262d;
        color: #c9d1d9;
        border: 1px solid #30363d;
        border-radius: 8px;
        padding: 6px 12px;
    }
    QPushButton:hover {
        background: #30363d;
    }
    QPushButton:disabled {
        background: #21262d;
        color: #484f58;
    }
    QLineEdit, QTextEdit {
        background: #0d1117;
        color: #c9d1d9;
        border: 1px solid #30363d;
        border-radius: 8px;
        padding: 6px 10px;
        selection-background-color: #1f6feb;
        selection-color: #ffffff;
    }
    QProgressBar {
        background: #21262d;
        border: 1px solid #30363d;
        border-radius: 4px;
    }
    QProgressBar::chunk {
        background: #58a6ff;
        border-radius: 3px;
    }
)";
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Consiglio");
    app.setOrganizationName("Aniviza");
    app.setApplicationVersion("0.1.0");
    app.setStyleSheet(kDarkTheme);

    MainWindow window;
    window.show();
    return app.exec();
}
