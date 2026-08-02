#include "sidebar.h"
#include <QIcon>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QApplication>
#include <QStyle>
#include "uimetrics.h"

Sidebar::Sidebar(QWidget *parent)
    : QListWidget(parent)
{
    setupUI();
}

void Sidebar::setupUI() {
    setFixedWidth(UiMetrics::sidebarWidth());
    setStyleSheet(R"(
        QListWidget {
            background: #1a1a1a;
            border: none;
            color: #c9d1d9;
            outline: none;
        }
        QListWidget::item {
            padding: 30px 42px;
            border-radius: 6px;
            margin: 6px 24px;
        }
        QListWidget::item:hover {
            background: rgba(255, 255, 255, 0.06);
        }
        QListWidget::item:selected {
            background: rgba(88, 166, 255, 0.18);
            color: #58a6ff;
        }
    )");

    setSpacing(8);

    // Plain text navigation keeps the work areas visually calm and direct.
    auto addPanel = [this](const QString &label, PanelId id) {
        auto *item = new QListWidgetItem(label, this);
        item->setData(Qt::UserRole, static_cast<int>(id));
        item->setSizeHint(QSize(0, UiMetrics::px(54)));
        return item;
    };

    addPanel("Sessions", PanelId::Sessions);
    addPanel("Timeline", PanelId::Timeline);
    addPanel("Files", PanelId::Files);
    addPanel("Discussions", PanelId::Discussions);

    addPanel("Secrets", PanelId::Secrets);
    addPanel("Mobile", PanelId::Mobile);

    // New Session button
    auto *newSessionBtn = new QPushButton("+ New Session", this);
    newSessionBtn->setStyleSheet(R"(
        QPushButton {
            background: rgba(63, 185, 80, 0.15);
            border: 1px solid rgba(63, 185, 80, 0.3);
            color: #3fb950;
            padding: 8px 14px;
            border-radius: 6px;
            font-weight: 600;
        }
        QPushButton:hover {
            background: rgba(63, 185, 80, 0.25);
        }
    )");
    connect(newSessionBtn, &QPushButton::clicked, this, [this]() { emit newSessionRequested(); });
    auto *newSessionItem = new QListWidgetItem(this);
    newSessionItem->setFlags(Qt::NoItemFlags);
    setItemWidget(newSessionItem, newSessionBtn);

    // Settings button at bottom
    auto *settingsBtn = new QPushButton("⚙ Settings", this);
    settingsBtn->setStyleSheet(R"(
        QPushButton {
            background: transparent;
            border: none;
            color: #8b949e;
            padding: 8px 14px;
            text-align: left;
        }
        QPushButton:hover {
            color: #c9d1d9;
        }
    )");
    connect(settingsBtn, &QPushButton::clicked, this, [this]() { emit settingsRequested(); });
    auto *settingsItem = new QListWidgetItem(this);
    settingsItem->setFlags(Qt::NoItemFlags);
    setItemWidget(settingsItem, settingsBtn);

    setCurrentRow(0);
    connect(this, &QListWidget::itemClicked, this, &Sidebar::onItemClicked);
}

void Sidebar::onItemClicked(QListWidgetItem *item) {
    if (auto *btn = qobject_cast<QPushButton *>(itemWidget(item))) {
        return;
    }
    auto id = static_cast<PanelId>(item->data(Qt::UserRole).toInt());
    setCurrentItem(item);
    emit panelChanged(id);
}

Sidebar::PanelId Sidebar::currentPanelId() const {
    auto *item = currentItem();
    if (!item) return PanelId::Sessions;
    return static_cast<PanelId>(item->data(Qt::UserRole).toInt());
}

void Sidebar::selectPanel(PanelId id) {
    for (int i = 0; i < count(); ++i) {
        auto *listItem = item(i);
        if (auto *btn = qobject_cast<QPushButton *>(itemWidget(listItem))) continue;
        if (static_cast<PanelId>(listItem->data(Qt::UserRole).toInt()) == id) {
            setCurrentItem(listItem);
            emit panelChanged(id);
            return;
        }
    }
}
