#include "providerselectiondialog.h"
#include "uimetrics.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>
#include <algorithm>

namespace {

bool isSelectable(const AgentInfo &provider) {
    if (!provider.installed) return false;
    if (provider.id == "ollama" || provider.id == "llama-cpp") {
        return provider.authenticated;
    }
    return true;
}

} // namespace

ProviderSelectionDialog::ProviderSelectionDialog(const QList<AgentInfo> &providers,
                                                 const QString &preferredProvider,
                                                 QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Choose an AI provider"));
    setModal(true);

    const QSize available = QApplication::primaryScreen()->availableGeometry().size();
    resize(qMin(UiMetrics::px(700), available.width()),
           qMin(UiMetrics::px(560), available.height()));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(UiMetrics::panelMargin(), UiMetrics::panelMargin(),
                               UiMetrics::panelMargin(), UiMetrics::panelMargin());
    layout->setSpacing(UiMetrics::panelSpacing());

    auto *title = new QLabel(tr("Choose how this session should run"), this);
    title->setFont(UiMetrics::titleFont());
    title->setStyleSheet("color: #f0f6fc;");
    layout->addWidget(title);

    const int networkEndpointCount = std::count_if(providers.cbegin(), providers.cend(),
        [](const AgentInfo &provider) { return !provider.endpoint.isEmpty(); });
    auto *description = new QLabel(
        tr("Scanned this device and its active local networks. Found %1 LAN model endpoint(s). "
           "Unavailable local tools remain visible for diagnostics.").arg(networkEndpointCount), this);
    description->setFont(UiMetrics::secondaryFont());
    description->setWordWrap(true);
    description->setStyleSheet("color: #8b949e;");
    layout->addWidget(description);

    m_providerList = new QListWidget(this);
    m_providerList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_providerList->setSpacing(UiMetrics::px(6));
    layout->addWidget(m_providerList, 1);

    QListWidgetItem *firstSelectable = nullptr;
    QListWidgetItem *preferredItem = nullptr;
    QList<AgentInfo> orderedProviders = providers;
    std::stable_sort(orderedProviders.begin(), orderedProviders.end(),
        [](const AgentInfo &left, const AgentInfo &right) {
            const int leftRank = !left.endpoint.isEmpty() ? 0 : (isSelectable(left) ? 1 : 2);
            const int rightRank = !right.endpoint.isEmpty() ? 0 : (isSelectable(right) ? 1 : 2);
            return leftRank < rightRank;
        });
    for (const auto &provider : orderedProviders) {
        const bool selectable = isSelectable(provider);
        const QString status = selectable ? tr("Ready") : tr("Unavailable");
        const QString version = provider.version.isEmpty() ? QString() : QString(" · %1").arg(provider.version);
        auto *item = new QListWidgetItem(
            QString("%1\n%2 — %3%4").arg(provider.name, status, provider.diagnostic, version),
            m_providerList);
        item->setData(Qt::UserRole, provider.id);
        item->setData(Qt::UserRole + 1, provider.provider.isEmpty() ? provider.id : provider.provider);
        item->setData(Qt::UserRole + 2, provider.endpoint);
        item->setData(Qt::UserRole + 3, provider.model);
        item->setSizeHint(QSize(0, UiMetrics::px(78)));
        if (!selectable) {
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled & ~Qt::ItemIsSelectable);
            item->setForeground(QColor("#6e7681"));
        } else {
            if (!firstSelectable) firstSelectable = item;
            if ((provider.provider.isEmpty() ? provider.id : provider.provider) == preferredProvider)
                preferredItem = item;
        }
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_continueButton = buttons->addButton(tr("Start session"), QDialogButtonBox::AcceptRole);
    m_continueButton->setEnabled(firstSelectable != nullptr);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_providerList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (item && item->flags().testFlag(Qt::ItemIsEnabled)) accept();
    });
    layout->addWidget(buttons);

    m_providerList->setCurrentItem(preferredItem ? preferredItem : firstSelectable);
}

QString ProviderSelectionDialog::selectedProvider() const {
    const auto *item = m_providerList->currentItem();
    return item ? item->data(Qt::UserRole + 1).toString() : QString();
}

QString ProviderSelectionDialog::selectedEndpoint() const {
    const auto *item = m_providerList->currentItem();
    return item ? item->data(Qt::UserRole + 2).toString() : QString();
}

QString ProviderSelectionDialog::selectedModel() const {
    const auto *item = m_providerList->currentItem();
    return item ? item->data(Qt::UserRole + 3).toString() : QString();
}
