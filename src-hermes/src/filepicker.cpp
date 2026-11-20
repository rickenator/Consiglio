#include "filepicker.h"

#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QVBoxLayout>

FilePicker::FilePicker(QWidget *parent) : QWidget(parent) {
    setupUI();
    // Like the command palette: a child of the main window that must stay hidden
    // until open() is called, otherwise it parks at (0,0) as a stray rectangle.
    hide();
}

void FilePicker::setupUI() {
    auto *overlayLayout = new QVBoxLayout(this);
    overlayLayout->setContentsMargins(0, 0, 0, 0);

    m_overlay = new QWidget(this);
    m_overlay->setObjectName("filePickerOverlay");
    m_overlay->setStyleSheet(R"(
        QWidget#filePickerOverlay { background: rgba(1, 4, 9, 0.55); }
    )");
    overlayLayout->addWidget(m_overlay); // fill the window so the dim + panel show

    auto *panel = new QWidget(m_overlay);
    panel->setObjectName("filePickerPanel");
    panel->setStyleSheet(R"(
        QWidget#filePickerPanel {
            background: #161b22; border: 1px solid #30363d; border-radius: 14px;
        }
    )");
    auto *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(16, 16, 16, 16);
    panelLayout->setSpacing(10);

    m_pathLabel = new QLabel(panel);
    m_pathLabel->setObjectName("filePickerPath");
    m_pathLabel->setStyleSheet(R"(
        QLabel#filePickerPath { color: #8b949e; font-family: 'JetBrains Mono', monospace; }
    )");
    panelLayout->addWidget(m_pathLabel);

    m_filter = new QLineEdit(panel);
    m_filter->setObjectName("filePickerFilter");
    m_filter->setPlaceholderText(tr("Type to filter… Enter inserts · double-click browses · Esc closes"));
    m_filter->setStyleSheet(R"(
        QLineEdit#filePickerFilter {
            background: #0d1117; border: 1px solid #30363d;
            border-radius: 10px; padding: 12px 14px; color: #c9d1d9;
            font-family: 'Segoe UI', sans-serif;
        }
        QLineEdit#filePickerFilter:focus { border-color: #58a6ff; }
    )");
    panelLayout->addWidget(m_filter);

    m_list = new QListWidget(panel);
    m_list->setObjectName("filePickerList");
    m_list->setStyleSheet(R"(
        QListWidget#filePickerList { background: transparent; border: none; }
        QListWidget::item { padding: 10px 12px; border-radius: 8px; color: #c9d1d9; }
        QListWidget::item:selected { background: rgba(88, 166, 255, 0.18); color: #f0f6fc; }
    )");
    panelLayout->addWidget(m_list, 1);

    auto *centering = new QVBoxLayout(m_overlay);
    centering->setContentsMargins(0, 120, 0, 0);
    centering->addStretch();
    auto *panelRow = new QHBoxLayout();
    panelRow->addStretch();
    panelRow->addWidget(panel, 0);
    panelRow->addStretch();
    centering->addLayout(panelRow);
    centering->addStretch();

    connect(m_filter, &QLineEdit::textChanged, this, &FilePicker::onFilterTextChanged);
    // Double-click a folder to BROWSE into it (drill down). Enter / single-click
    // insert the highlighted entry — files and directories alike (dirs get a
    // trailing "/"), matching the CLI's path completion.
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (!item || item->text() == "..") return;
        const QString path = item->data(Qt::UserRole).toString();
        if (!path.isEmpty() && QFileInfo(path).isDir()) navigateTo(path);
    });
    connect(m_filter, &QLineEdit::returnPressed, this, &FilePicker::onRunHighlighted);

    // Esc handling: key events are delivered to whichever child has focus
    // (the filter box or the list), never to the overlay widget itself — so
    // install the filter on those children, not on `this`. The dimmed overlay
    // is filtered too: a click on it (outside the panel) dismisses the picker.
    m_filter->installEventFilter(this);
    m_list->installEventFilter(this);
    m_overlay->installEventFilter(this);
}

void FilePicker::open(const QString &root) {
    m_root = root;
    navigateTo(root);
    if (parentWidget()) setGeometry(parentWidget()->rect());
    show();
    raise();
    m_filter->setFocus();
}

void FilePicker::closeOverlay() {
    hide();
    // Hand keyboard focus back to the composer so the user can keep typing
    // right after closing the picker (Esc) or inserting a path.
    if (auto *input = parentWidget()->findChild<QLineEdit *>("composerInput"))
        input->setFocus();
}

void FilePicker::navigateTo(const QString &dir) {
    m_current = dir;
    m_pathLabel->setText(dir);
    refreshList();
}

void FilePicker::refreshList() {
    const QString filter = m_filter->text().trimmed().toLower();
    QDir dir(m_current);
    if (!dir.exists()) {
        m_list->clear();
        return;
    }
    // Directories first, then files, each alphabetical. Hidden entries are
    // skipped to match the Hermes CLI's default file picker.
    const auto entries = dir.entryInfoList(
        QDir::Dirs | QDir::Files, QDir::Name);

    m_list->clear();
    // A ".." row at the top lets the user climb out of the cwd (e.g. to reach
    // /tmp when rooted at a workspace). Omitted at the filesystem root.
    if (m_current != "/") {
        auto *up = new QListWidgetItem("..", m_list);
        QDir parent(m_current);
        parent.cdUp();
        up->setData(Qt::UserRole, parent.absolutePath());
        up->setToolTip("Go to parent directory");
    }
    for (const QFileInfo &info : entries) {
        if (info.isHidden()) continue;
        const QString name = info.fileName();
        if (!filter.isEmpty() && !name.toLower().contains(filter)) continue;
        const bool isDir = info.isDir();
        auto *item = new QListWidgetItem(
            QString("%1%2").arg(name, isDir ? "/" : ""), m_list);
        item->setData(Qt::UserRole, info.absoluteFilePath());
        // Directories get a subtle folder glyph so they read as navigable.
        if (isDir) item->setToolTip(info.absoluteFilePath());
    }
    if (m_list->currentRow() < 0 && m_list->count() > 0) {
        m_list->setCurrentRow(0);
    }
}

void FilePicker::onFilterTextChanged(const QString &text) {
    Q_UNUSED(text);
    refreshList();
}

void FilePicker::selectItem(QListWidgetItem *item) {
    // The ".." row is a navigation control, not a real entry — Enter on it
    // climbs to the parent directory.
    if (item->text() == "..") {
        navigateTo(item->data(Qt::UserRole).toString());
        return;
    }
    const QString path = item->data(Qt::UserRole).toString();
    // Insert the entry: files as-is, directories with a trailing "/" so the
    // reference reads as a folder — matching the CLI's path completion, which
    // is how you reference a directory like /tmp in your message.
    const QString text = QFileInfo(path).isDir() ? path + "/" : path;
    emit pathSelected(text);
    closeOverlay();
}

void FilePicker::onRunHighlighted() {
    auto *item = m_list->currentItem();
    if (item) {
        selectItem(item);
        return;
    }
    // No highlighted entry: if the filter text names an existing subdirectory,
    // drill into it directly (fast navigation without clicking).
    const QString candidate = m_current + "/" + m_filter->text().trimmed();
    if (!m_filter->text().trimmed().isEmpty() && QFileInfo(candidate).isDir()) {
        navigateTo(candidate);
    }
}

bool FilePicker::eventFilter(QObject *obj, QEvent *event) {
    // Key events reach the focused child (filter box or list), not the overlay
    // widget itself — so catch Esc on any of them and close the picker.
    if ((obj == m_filter || obj == m_list) && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape) {
            closeOverlay();
            return true;
        }
        // Up/down arrows move the highlighted entry (and scroll the list) while
        // the filter box has focus, like a dropdown — browse without clicking.
        // (When the list itself has focus, QListWidget handles arrows natively.)
        if (obj == m_filter && (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down)) {
            if (m_list->count() > 0) {
                int row = m_list->currentRow();
                if (row < 0) row = 0;
                int next = (key->key() == Qt::Key_Up) ? row - 1 : row + 1;
                if (next < 0) next = 0;
                if (next >= m_list->count()) next = m_list->count() - 1;
                m_list->setCurrentRow(next);
            }
            return true;
        }
    }
    // A click on the dimmed area (outside the panel) dismisses the picker. The
    // panel and its children swallow their own clicks, so this only fires for
    // outside clicks. Commits the highlighted entry — a file, or a directory
    // with a trailing "/" (the ".." row is navigation, never committed).
    if (obj == m_overlay && event->type() == QEvent::MouseButtonPress) {
        auto *item = m_list->currentItem();
        if (item && item->text() != "..") {
            const QString path = item->data(Qt::UserRole).toString();
            if (!path.isEmpty()) {
                emit pathSelected(QFileInfo(path).isDir() ? path + "/" : path);
                closeOverlay();
                return true;
            }
        }
        closeOverlay();
        return true;
    }
    return QWidget::eventFilter(obj, event);
}
