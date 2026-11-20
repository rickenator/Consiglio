#pragma once

#include <QWidget>
#include <QList>
#include <QLineEdit>
#include <QListWidget>

struct PaletteAction {
    QString id;        // stable identifier, e.g. "new-session"
    QString label;     // display text, e.g. "New Session"
    QString shortcut;  // hint, e.g. "Ctrl+N"
};

// A compact, searchable action palette. Type to filter; Enter runs the
// highlighted action; Esc closes. Modeled on the Hermes / IDE command
// palettes so common session controls are reachable without hunting menus.
class CommandPalette : public QWidget {
    Q_OBJECT

public:
    explicit CommandPalette(QWidget *parent = nullptr);

    void setActions(const QList<PaletteAction> &actions);
    void open();
    void closeOverlay();

signals:
    void actionSelected(const QString &id);

public slots:
    void onFilterTextChanged(const QString &text);
    void onRunHighlighted();

private:
    void setupUI();
    void refreshList();

    QWidget *m_overlay = nullptr;
    QLineEdit *m_filter = nullptr;
    QListWidget *m_list = nullptr;
    QList<PaletteAction> m_actions;
};
