#include <QApplication>
#include <QStyleFactory>
#include <QFontDatabase>
#include <QPalette>
#include "mainwindow.h"

static void setDarkPalette(QApplication *app) {
    QPalette dark;
    // GitHub-dark inspired colors
    QColor base = QColor("#0d1117");       // main background
    QColor surface = QColor("#161b22");     // panels, inputs
    QColor border = QColor("#30363d");      // borders
    QColor text = QColor("#c9d1d9");        // primary text
    QColor muted = QColor("#8b949e");       // secondary text
    QColor accent = QColor("#58a6ff");      // links, highlights
    QColor green = QColor("#3fb950");       // success
    QColor red = QColor("#f85149");         // error

    dark.setColor(QPalette::Window, base);
    dark.setColor(QPalette::WindowText, text);
    dark.setColor(QPalette::Base, surface);
    dark.setColor(QPalette::AlternateBase, surface);
    dark.setColor(QPalette::ToolTipBase, text);
    dark.setColor(QPalette::ToolTipText, text);
    dark.setColor(QPalette::Text, text);
    dark.setColor(QPalette::Button, surface);
    dark.setColor(QPalette::ButtonText, text);
    dark.setColor(QPalette::BrightText, accent);
    dark.setColor(QPalette::Link, accent);
    dark.setColor(QPalette::Highlight, accent);
    dark.setColor(QPalette::HighlightedText, QColor("#0d1117"));

    app->setPalette(dark);
    app->setStyleSheet(R"(
        QMenuBar {
            background: #161b22;
            color: #c9d1d9;
            border-bottom: 1px solid #30363d;
        }
        QMenuBar::item:selected {
            background: #30363d;
        }
        QMenu {
            background: #161b22;
            color: #c9d1d9;
            border: 1px solid #30363d;
        }
        QMenu::item:selected {
            background: #30363d;
        }
        QPushButton {
            background: #21262d;
            color: #c9d1d9;
            border: 1px solid #30363d;
            border-radius: 4px;
            padding: 6px 12px;
        }
        QPushButton:hover {
            background: #30363d;
            border-color: #58a6ff;
        }
        QLineEdit {
            background: #0d1117;
            color: #c9d1d9;
            border: 1px solid #30363d;
            border-radius: 4px;
            padding: 6px 8px;
        }
        QLineEdit:focus {
            border-color: #58a6ff;
        }
        QTreeView, QListView, QTableView {
            background: #0d1117;
            color: #c9d1d9;
            border: 1px solid #30363d;
            border-radius: 4px;
            padding: 4px;
        }
        QTreeView::item:hover {
            background: rgba(88, 166, 255, 0.08);
        }
        QTreeView::item:selected {
            background: rgba(88, 166, 255, 0.15);
        }
        QTabWidget::pane {
            border: 1px solid #30363d;
            border-radius: 4px;
            background: #0d1117;
        }
        QTabBar::tab {
            background: #161b22;
            color: #8b949e;
            padding: 8px 16px;
            border-top-left-radius: 4px;
            border-top-right-radius: 4px;
        }
        QTabBar::tab:selected {
            background: #0d1117;
            color: #c9d1d9;
        }
        QTabBar::tab:hover {
            background: #21262d;
        }
        QLabel {
            color: #c9d1d9;
        }
        QTextEdit {
            background: #0d1117;
            color: #c9d1d9;
            border: 1px solid #30363d;
            border-radius: 4px;
        }
        QStatusBar {
            background: #161b22;
            color: #8b949e;
            border-top: 1px solid #30363d;
        }
        QComboBox {
            background: #0d1117;
            color: #c9d1d9;
            border: 1px solid #30363d;
            border-radius: 4px;
            padding: 4px 8px;
        }
        QScrollBar:vertical {
            background: #0d1117;
            width: 10px;
            border-radius: 5px;
        }
        QScrollBar::handle:vertical {
            background: #30363d;
            border-radius: 4px;
            min-height: 20px;
        }
        QScrollBar::handle:vertical:hover {
            background: #484f58;
        }
        QScrollBar::add-line, QScrollBar::sub-line {
            height: 0px;
        }
    )");
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Consiglio");
    app.setApplicationVersion("0.2.0");
    app.setOrganizationName("Aniviza");
    app.setOrganizationDomain("aniviza.com");

    // Dark Fusion style + custom palette
    app.setStyle(QStyleFactory::create("Fusion"));
    setDarkPalette(&app);

    QFont font("Segoe UI", 10);
    font.setStyleHint(QFont::SansSerif);
    app.setFont(font);

    MainWindow window;
    window.show();
    return app.exec();
}
