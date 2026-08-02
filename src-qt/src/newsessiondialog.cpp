#include "newsessiondialog.h"

#include "uimetrics.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

NewSessionDialog::NewSessionDialog(const QString &provider,
                                   const QString &initialDirectory,
                                   QWidget *parent)
    : QDialog(parent), m_provider(provider) {
    setWindowTitle(tr("New Session"));
    setModal(true);
    setMinimumWidth(UiMetrics::px(640));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(UiMetrics::panelMargin(), UiMetrics::panelMargin(),
                               UiMetrics::panelMargin(), UiMetrics::panelMargin());
    layout->setSpacing(UiMetrics::panelSpacing());

    auto *title = new QLabel(tr("Start an agent session"), this);
    title->setFont(UiMetrics::titleFont());
    layout->addWidget(title);

    auto *providerLabel = new QLabel(tr("Provider: %1").arg(provider), this);
    providerLabel->setFont(UiMetrics::secondaryFont());
    providerLabel->setStyleSheet("color: #8b949e;");
    layout->addWidget(providerLabel);

    auto *form = new QFormLayout();
    form->setSpacing(UiMetrics::panelSpacing());

    auto *workspaceRow = new QHBoxLayout();
    m_workspaceEdit = new QLineEdit(QDir::cleanPath(initialDirectory), this);
    auto *browseButton = new QPushButton(tr("Browse…"), this);
    workspaceRow->addWidget(m_workspaceEdit, 1);
    workspaceRow->addWidget(browseButton);
    form->addRow(tr("Workspace"), workspaceRow);

    m_permissionCombo = new QComboBox(this);
    m_permissionCombo->addItem(tr("Read only"), "read-only");
    m_permissionCombo->addItem(tr("Workspace access (Recommended)"), "workspace-write");
    m_permissionCombo->addItem(tr("Full machine access"), "danger-full-access");
    m_permissionCombo->setCurrentIndex(1);
    form->addRow(tr("Permissions"), m_permissionCombo);
    layout->addLayout(form);

    m_permissionDescription = new QLabel(this);
    m_permissionDescription->setWordWrap(true);
    m_permissionDescription->setFont(UiMetrics::secondaryFont());
    layout->addWidget(m_permissionDescription);

    const bool supportsCodexPermissions =
        provider == "codex" || provider == "remote_llamacpp";
    if (!supportsCodexPermissions) {
        m_permissionCombo->setEnabled(false);
        m_permissionDescription->setText(
            tr("This provider manages its own process permissions."));
        m_permissionDescription->setStyleSheet("color: #8b949e;");
    } else {
        updatePermissionDescription();
    }

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Cancel | QDialogButtonBox::Ok, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Start Session"));
    layout->addWidget(buttons);

    connect(browseButton, &QPushButton::clicked, this, [this]() {
        const QString directory = QFileDialog::getExistingDirectory(
            this, tr("Select Workspace Directory"), m_workspaceEdit->text(),
            QFileDialog::ShowDirsOnly);
        if (!directory.isEmpty()) m_workspaceEdit->setText(directory);
    });
    connect(m_permissionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this]() { updatePermissionDescription(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this,
            &NewSessionDialog::validateAndAccept);
}

QString NewSessionDialog::workspace() const {
    return QDir::cleanPath(m_workspaceEdit->text().trimmed());
}

QString NewSessionDialog::sandboxMode() const {
    if (!m_permissionCombo->isEnabled()) return "provider-managed";
    return m_permissionCombo->currentData().toString();
}

void NewSessionDialog::updatePermissionDescription() {
    const QString mode = sandboxMode();
    if (mode == "read-only") {
        m_permissionDescription->setText(
            tr("The agent can inspect the workspace but cannot edit files or run commands that require writes."));
        m_permissionDescription->setStyleSheet("color: #8b949e;");
    } else if (mode == "workspace-write") {
        m_permissionDescription->setText(
            tr("The agent can edit the selected workspace and run local commands inside that boundary."));
        m_permissionDescription->setStyleSheet("color: #3fb950;");
    } else {
        m_permissionDescription->setText(
            tr("Warning: the agent can access and modify files anywhere this user account can, with no Codex sandbox boundary."));
        m_permissionDescription->setStyleSheet("color: #f85149; font-weight: bold;");
    }
}

void NewSessionDialog::validateAndAccept() {
    if (!QDir(workspace()).exists()) {
        QMessageBox::warning(this, tr("Workspace not found"),
                             tr("Choose an existing workspace directory."));
        return;
    }
    if (sandboxMode() == "danger-full-access") {
        const auto result = QMessageBox::warning(
            this, tr("Grant full machine access?"),
            tr("This removes the Codex filesystem and network sandbox for this session. "
               "The agent can modify anything your user account can access.\n\n"
               "Only continue if you trust the task and workspace."),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (result != QMessageBox::Yes) return;
    }
    accept();
}
