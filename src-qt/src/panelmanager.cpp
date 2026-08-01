#include "panelmanager.h"
#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QCoreApplication>

PanelManager::PanelManager(QStackedWidget *stack, QObject *parent)
    : QObject(parent), m_stack(stack)
{
}

PanelManager::~PanelManager() = default;

void PanelManager::createPanels(QWidget *mainWindow) {
    // Welcome panel
    m_stack->addWidget(createWelcomePanel());

    // Sessions panel
    m_stack->addWidget(createSessionsPanel());

    // Timeline panel
    m_stack->addWidget(createTimelinePanel());

    // Files panel
    m_stack->addWidget(createFilesPanel());

    // Discussions panel
    m_stack->addWidget(createDiscussionsPanel());

    // Secrets panel
    m_stack->addWidget(createSecretsPanel());

    // Mobile panel
    m_stack->addWidget(createMobilePanel());
}

QWidget *PanelManager::createWelcomePanel() {
    auto *widget = new QWidget;
    auto *layout = new QVBoxLayout(widget);
    layout->setAlignment(Qt::AlignCenter);

    auto *title = new QLabel("Welcome to Consiglio", widget);
    title->setStyleSheet("font-size: 28px; font-weight: bold; color: #f0f6fc;");
    layout->addWidget(title, 0, Qt::AlignCenter);

    auto *subtitle = new QLabel("A native C++ desktop control plane for AI agents.\nSelect a panel from the sidebar to get started.", widget);
    subtitle->setStyleSheet("font-size: 14px; color: #8b949e; text-align: center;");
    subtitle->setWordWrap(true);
    layout->addWidget(subtitle, 0, Qt::AlignCenter);

    return widget;
}

QWidget *PanelManager::createSessionsPanel() {
    auto *widget = new QWidget;
    auto *layout = new QVBoxLayout(widget);

    auto *label = new QLabel("Sessions Panel", widget);
    label->setStyleSheet("font-size: 18px; font-weight: bold; color: #f0f6fc;");
    layout->addWidget(label);

    auto *info = new QLabel("Active and past agent sessions will appear here.", widget);
    info->setStyleSheet("color: #8b949e;");
    layout->addWidget(info);

    return widget;
}

QWidget *PanelManager::createTimelinePanel() {
    auto *widget = new QWidget;
    auto *layout = new QVBoxLayout(widget);

    auto *label = new QLabel("Event Timeline", widget);
    label->setStyleSheet("font-size: 18px; font-weight: bold; color: #f0f6fc;");
    layout->addWidget(label);

    auto *info = new QLabel("Real-time event stream from the active agent session.", widget);
    info->setStyleSheet("color: #8b949e;");
    layout->addWidget(info);

    return widget;
}

QWidget *PanelManager::createFilesPanel() {
    auto *widget = new QWidget;
    auto *layout = new QVBoxLayout(widget);

    auto *label = new QLabel("File Browser", widget);
    label->setStyleSheet("font-size: 18px; font-weight: bold; color: #f0f6fc;");
    layout->addWidget(label);

    auto *info = new QLabel("Browse and manage files in the agent's working directory.", widget);
    info->setStyleSheet("color: #8b949e;");
    layout->addWidget(info);

    return widget;
}

QWidget *PanelManager::createDiscussionsPanel() {
    auto *widget = new QWidget;
    auto *layout = new QVBoxLayout(widget);

    auto *label = new QLabel("Discussions", widget);
    label->setStyleSheet("font-size: 18px; font-weight: bold; color: #f0f6fc;");
    layout->addWidget(label);

    auto *info = new QLabel("Threaded discussions and notes.", widget);
    info->setStyleSheet("color: #8b949e;");
    layout->addWidget(info);

    return widget;
}

QWidget *PanelManager::createSecretsPanel() {
    auto *widget = new QWidget;
    auto *layout = new QVBoxLayout(widget);

    auto *label = new QLabel("Secrets Manager", widget);
    label->setStyleSheet("font-size: 18px; font-weight: bold; color: #f0f6fc;");
    layout->addWidget(label);

    auto *info = new QLabel("Manage API keys and credentials securely.", widget);
    info->setStyleSheet("color: #8b949e;");
    layout->addWidget(info);

    return widget;
}

QWidget *PanelManager::createMobilePanel() {
    auto *widget = new QWidget;
    auto *layout = new QVBoxLayout(widget);

    auto *label = new QLabel("Mobile Pairing", widget);
    label->setStyleSheet("font-size: 18px; font-weight: bold; color: #f0f6fc;");
    layout->addWidget(label);

    auto *info = new QLabel("Pair mobile devices for remote control.", widget);
    info->setStyleSheet("color: #8b949e;");
    layout->addWidget(info);

    return widget;
}
