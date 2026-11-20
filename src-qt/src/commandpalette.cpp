#include "commandpalette.h"
#include "uimetrics.h"

#include <QEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

CommandPalette::CommandPalette(QWidget *parent) : QWidget(parent) {
    setupUI();
}

void CommandPalette::setupUI() {
    // The palette is a child overlay that fills its parent (the main window)
    // when shown. A translucent backdrop sits behind a centered panel.
    auto *overlayLayout = new QVBoxLayout(this);
    overlayLayout->setContentsMargins(0, 0, 0, 0);

    m_overlay = new QWidget(this);
    m_overlay->setObjectName("paletteOverlay");
    m_overlay->setStyleSheet(R"(
        QWidget#paletteOverlay { background: rgba(1, 4, 9, 0.55); }
    )");

    auto *panel = new QWidget(m_overlay);
    panel->setObjectName("palettePanel");
    panel->setStyleSheet(R"(
        QWidget#palettePanel {
            background: #161b22; border: 1px solid #30363d;
            border-radius: 14px;
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
        QListWidget#paletteList {
            background: transparent; border: none;
        }
        QListWidget::item {
            padding: 10px 12px; border-radius: 8px; color: #c9d1d9;
        }
        QListWidget::item:selected {
            background: rgba(88, 166, 255, 0.18); color: #f0f6fc;
        }
    )");
    panelLayout->addWidget(m_list, 1);

    // Center the panel with a fixed width inside the overlay.
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
}

void CommandPalette::setActions(const QList<PaletteAction> &actions) {
    m_actions = actions;
}

void CommandPalette::open() {
    if (parentWidget()) {
        setGeometry(parentWidget()->rect());
    }
    show();
    raise();
    m_filter->clear();
    refreshList();
    m_filter->setFocus();
}

void CommandPalette::closeOverlay() {
    hide();
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
