#pragma once

#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QStandardItemModel>
#include <QWidget>

// One entry in the command palette.
struct PaletteAction {
    QString id;           // what actionSelected() reports ("/model", "app:settings", …)
    QString label;        // "Switch Model"
    QString shortcut;     // "Ctrl+N" (display only)
    QString description;  // one line under/after the label
    QString group;        // section title ("Session", "Hermes · Info & Diagnostics")
    QString icon;         // icon name in :/icons
    QString keywords;     // extra search terms (aliases, "/name")
    QString detail;       // right-aligned hint, e.g. "/model"
    bool enabled = true;
};

// A Ctrl+K command palette: a dimmed overlay over the window with a search
// field and a grouped, keyboard-driven list. Matches on label, command name,
// aliases and description.
class CommandPalette : public QWidget {
    Q_OBJECT

public:
    explicit CommandPalette(QWidget *parent = nullptr);

    void setActions(const QList<PaletteAction> &actions);
    QList<PaletteAction> actions() const { return m_actions; }
    void open(const QString &query = QString());
    void closeOverlay();
    bool isOpen() const { return isVisible(); }

    // For tests: ids currently listed (in order) for the current query.
    QStringList visibleIds() const;
    void setQuery(const QString &query);
    void triggerCurrent();

signals:
    void actionSelected(const QString &id);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void setupUI();
    void rebuild();
    void layoutSheet();
    void moveSelection(int delta);
    void selectFirst();

    QWidget *m_sheet = nullptr;
    QLabel *m_searchIcon = nullptr;
    QLineEdit *m_filter = nullptr;
    QListView *m_list = nullptr;
    QStandardItemModel *m_model = nullptr;
    QLabel *m_footer = nullptr;
    QLabel *m_count = nullptr;
    QList<PaletteAction> m_actions;
};
