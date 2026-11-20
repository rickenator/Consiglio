// Ported from src-hermes/src/commandpalette.cpp (audit M4): hidden until
// open(), the overlay fills the window, Esc / outside-click close it.
#include "commandpalette.h"
#include "uimetrics.h"

#include <QEvent>
#include <QKeyEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>

CommandPalette::CommandPalette(QWidget *parent) : QWidget(parent) {
    setupUI();
    // The palette is a child of the main window but must NOT be visible until
    // open() is called — otherwise it parks itself at (0,0) as a stray rectangle
    // that overlaps the menu bar.
    hide();
}

void CommandPalette::setupUI() {
    // A child overlay that fills its parent (the main window) when shown.
    auto *overlayLayout = new QVBoxLayout(this);
    overlayLayout->setContentsMargins(0, 0, 0, 0);

    m_overlay = new QWidget(this);
    m_overlay->setObjectName("paletteOverlay");
    m_overlay->setStyleSheet(R"(
        QWidget#paletteOverlay { background: rgba(1, 4, 9, 0.55); }
    )");
    overlayLayout->addWidget(m_overlay); // fill the window so the dim + panel show

    auto *panel = new QWidget(m_overlay);
    panel->setObjectName("palettePanel");
    panel->setStyleSheet(R"(
        QWidget#palettePanel {
            background: #161b22; border: 1px solid #30363d; border-radius: 14px;
        }
    )");
    auto *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(UiMetrics::px(16), UiMetrics::px(16),
                                    UiMetrics::px(16), UiMetrics::px(16));
    panelLayout->setSpacing(UiMetrics::px(10));

    m_filter = new QLineEdit(panel);
    m_filter->setObjectName("paletteFilter");
    m_filter->setPlaceholderText(tr("Type a command…"));
    m_filter->setStyleSheet(R"(
        QLineEdit#paletteFilter {
            background: #0d1117; border: 1px solid #30363d;
            border-radius: 10px; padding: 12px 14px; color: #c9d1d9;
            font-family: 'Segoe UI', sans-serif;
        }
        QLineEdit#paletteFilter:focus { border-color: #58a6ff; }
    )");
    panelLayout->addWidget(m_filter);

    m_list = new QListWidget(panel);
    m_list->setObjectName("paletteList");
    m_list->setStyleSheet(R"(
        QListWidget#paletteList { background: transparent; border: none; }
        QListWidget::item { padding: 10px 12px; border-radius: 8px; color: #c9d1d9; }
        QListWidget::item:selected { background: rgba(88, 166, 255, 0.18); color: #f0f6fc; }
    )");
    panelLayout->addWidget(m_list, 1);

    auto *centering = new QVBoxLayout(m_overlay);
    centering->setContentsMargins(0, UiMetrics::px(120), 0, 0);
    centering->addStretch();
    auto *panelRow = new QHBoxLayout();
    panelRow->addStretch();
    panelRow->addWidget(panel, 0);
    panelRow->addStretch();
    centering->addLayout(panelRow);
    centering->addStretch();

    connect(m_filter, &QLineEdit::textChanged, this, &CommandPalette::onFilterTextChanged);
    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        if (item) emit actionSelected(item->data(Qt::UserRole).toString());
        closeOverlay();
    });
    connect(m_filter, &QLineEdit::returnPressed, this, &CommandPalette::onRunHighlighted);

    // Esc handling: key events are delivered to whichever child has focus
    // (the filter box or the list), never to the overlay widget itself — so
    // install the filter on those children, not on `this`. The dimmed overlay
    // is filtered too: a click on it (outside the panel) cancels the palette.
    m_filter->installEventFilter(this);
    m_list->installEventFilter(this);
    m_overlay->installEventFilter(this);
}

void CommandPalette::setActions(const QList<PaletteAction> &actions) {
    m_actions = actions;
}

void CommandPalette::open() {
    if (parentWidget()) setGeometry(parentWidget()->rect());
    show();
    raise();
    m_filter->clear();
    refreshList();
    m_filter->setFocus();
}

void CommandPalette::closeOverlay() {
    hide();
    // Hand keyboard focus back to the composer so the user can keep typing
    // right after closing the palette (Esc) or running an action.
    if (parentWidget()) {
        if (auto *input = parentWidget()->findChild<QLineEdit *>("composerInput"))
            input->setFocus();
    }
}

bool CommandPalette::eventFilter(QObject *obj, QEvent *event) {
    // Key events reach the focused child (filter box or list), not the overlay
    // widget itself — so catch Esc on any of them and close the palette.
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
    // A click on the dimmed area (outside the panel) cancels the palette. The
    // panel and its children swallow their own clicks, so this only fires for
    // outside clicks.
    if (obj == m_overlay && event->type() == QEvent::MouseButtonPress) {
        closeOverlay();
        return true;
    }
    return QWidget::eventFilter(obj, event);
}

void CommandPalette::onFilterTextChanged(const QString &text) {
    Q_UNUSED(text);
    refreshList();
}

void CommandPalette::refreshList() {
    const QString filter = m_filter->text().trimmed().toLower();
    m_list->clear();
    for (const auto &action : m_actions) {
        if (!filter.isEmpty() &&
            !action.label.toLower().contains(filter) &&
            !action.id.toLower().contains(filter)) {
            continue;
        }
        const QString hint = action.shortcut.isEmpty() ? QString() : QString("   %1").arg(action.shortcut);
        auto *item = new QListWidgetItem(QString("%1%2").arg(action.label, hint), m_list);
        item->setData(Qt::UserRole, action.id);
    }
    if (m_list->currentRow() < 0 && m_list->count() > 0) {
        m_list->setCurrentRow(0);
    }
}

void CommandPalette::onRunHighlighted() {
    auto *item = m_list->currentItem();
    if (!item) return;
    emit actionSelected(item->data(Qt::UserRole).toString());
    closeOverlay();
}
