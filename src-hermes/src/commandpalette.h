#pragma once

#include <QWidget>
#include <QList>
#include <QLineEdit>
#include <QListWidget>

struct PaletteAction {
    QString id;        // stable identifier, e.g. "new-session" or "/model"
    QString label;     // display text, e.g. "Model" or "New Session"
    QString shortcut;  // hint, e.g. "Ctrl+N"
};

// A compact, searchable action palette (Ctrl+K). Lists app actions plus the
// Hermes slash commands the session exposes (/model, /context, /tools, …).
// Type to filter; Enter runs the highlighted action; Esc closes.
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

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

    QWidget *m_overlay = nullptr;
    QLineEdit *m_filter = nullptr;
    QListWidget *m_list = nullptr;
    QList<PaletteAction> m_actions;
};
