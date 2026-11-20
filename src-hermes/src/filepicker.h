#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QListWidget>
#include <QLabel>

// A file-system picker overlay, opened by typing "/" at a NON-first position in
// the composer (mirroring the Hermes CLI). It browses files/directories under a
// root directory: type to filter the current folder's entries. Enter INSERTS the
// highlighted entry — a file as-is, or a directory with a trailing "/" (so you can
// reference a folder like /tmp in your message, matching the CLI's path completion).
// Double-click a folder to BROWSE into it; ".." climbs to the parent. Esc closes.
// The chosen path is inserted back into the composer by the owner (MainWindow) at
// the cursor position recorded when "/" was typed.
class FilePicker : public QWidget {
    Q_OBJECT

public:
    explicit FilePicker(QWidget *parent = nullptr);

    // Show the picker rooted at `root` (an absolute path).
    void open(const QString &root);
    void closeOverlay();

signals:
    void pathSelected(const QString &absolutePath);

public slots:
    void onFilterTextChanged(const QString &text);
    void onRunHighlighted();

private:
    void setupUI();
    void refreshList();
    void navigateTo(const QString &dir);
    void selectItem(QListWidgetItem *item);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

    QWidget *m_overlay = nullptr;
    QLineEdit *m_filter = nullptr;
    QListWidget *m_list = nullptr;
    QLabel *m_pathLabel = nullptr;
    QString m_root;     // the directory we were opened at
    QString m_current;  // the folder currently being browsed
};
