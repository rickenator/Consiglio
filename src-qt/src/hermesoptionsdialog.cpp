#include "hermesoptionsdialog.h"
#include "uimetrics.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
const char *kReasoningLevels[] = {
    "default", "none", "minimal", "low", "medium", "high", "xhigh", "max", "ultra"
};
} // namespace

HermesOptionsDialog::HermesOptionsDialog(const QString &initialWorkspace,
                                         const QVariantMap &current,
                                         QWidget *parent)
    : QDialog(parent) {
    setupUI();

    // Seed from the current session options so re-opening preserves state.
    m_workspaceEdit->setText(QDir::cleanPath(initialWorkspace));
    m_modelEdit->setText(current.value("model").toString());
    m_providerCombo->setCurrentText(current.value("providerName").toString());
    const QString reasoning = current.value("reasoning").toString();
    if (!reasoning.isEmpty()) {
        int idx = m_reasoningCombo->findText(reasoning);
        if (idx >= 0) m_reasoningCombo->setCurrentIndex(idx);
    }
    m_toolsetsEdit->setText(current.value("toolsets").toString());
    m_skillsEdit->setText(current.value("skills").toString());
    m_profileEdit->setText(current.value("profile").toString());
    const int maxTurns = current.value("maxTurns").toInt();
    if (maxTurns > 0) m_maxTurnsEdit->setText(QString::number(maxTurns));
    m_yoloCheck->setChecked(current.value("yolo", false).toBool());
    m_worktreeCheck->setChecked(current.value("worktree", false).toBool());
}

void HermesOptionsDialog::setupUI() {
    setWindowTitle(tr("Hermes Session Options"));
    setModal(true);
    resize(UiMetrics::px(560), UiMetrics::px(520));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(UiMetrics::panelMargin(), UiMetrics::panelMargin(),
                               UiMetrics::panelMargin(), UiMetrics::panelMargin());
    layout->setSpacing(UiMetrics::panelSpacing());

    auto *title = new QLabel(tr("Hermes Session Options"), this);
    title->setFont(UiMetrics::titleFont());
    title->setStyleSheet("color: #f0f6fc;");
    layout->addWidget(title);

    auto *description = new QLabel(
        tr("These map directly to the flags passed to `hermes chat` for every "
           "turn of this session. Leave a field empty to use the Hermes default."),
        this);
    description->setWordWrap(true);
    description->setFont(UiMetrics::secondaryFont());
    description->setStyleSheet("color: #8b949e;");
    layout->addWidget(description);

    auto *form = new QFormLayout();
    form->setSpacing(UiMetrics::panelSpacing());

    // Workspace
    auto *workspaceRow = new QHBoxLayout();
    m_workspaceEdit = new QLineEdit(this);
    auto *browseButton = new QPushButton(tr("Browse…"), this);
    workspaceRow->addWidget(m_workspaceEdit, 1);
    workspaceRow->addWidget(browseButton);
    form->addRow(tr("Workspace"), workspaceRow);

    // Model
    m_modelEdit = new QLineEdit(this);
    m_modelEdit->setPlaceholderText(tr("e.g. anthropic/claude-sonnet-4"));
    form->addRow(tr("Model (-m)"), m_modelEdit);

    // Provider
    m_providerCombo = new QComboBox(this);
    m_providerCombo->setEditable(true);
    m_providerCombo->addItem(tr("(default)"));
    m_providerCombo->addItem("openai");
    m_providerCombo->addItem("anthropic");
    m_providerCombo->addItem("nous");
    m_providerCombo->addItem("deepseek");
    m_providerCombo->addItem("google");
    m_providerCombo->setInsertPolicy(QComboBox::InsertAtCurrent);
    form->addRow(tr("Provider (--provider)"), m_providerCombo);

    // Reasoning
    m_reasoningCombo = new QComboBox(this);
    for (const char *level : kReasoningLevels) m_reasoningCombo->addItem(level);
    m_reasoningCombo->setCurrentIndex(0);
    form->addRow(tr("Reasoning (--reasoning)"), m_reasoningCombo);

    // Toolsets
    m_toolsetsEdit = new QLineEdit(this);
    m_toolsetsEdit->setPlaceholderText(tr("e.g. web,terminal,file"));
    form->addRow(tr("Toolsets (-t)"), m_toolsetsEdit);

    // Skills
    m_skillsEdit = new QLineEdit(this);
    m_skillsEdit->setPlaceholderText(tr("comma-separated skill names"));
    form->addRow(tr("Skills (-s)"), m_skillsEdit);

    // Profile
    m_profileEdit = new QLineEdit(this);
    m_profileEdit->setPlaceholderText(tr("named profile (optional)"));
    form->addRow(tr("Profile (--profile)"), m_profileEdit);

    // Max turns
    m_maxTurnsEdit = new QLineEdit(this);
    m_maxTurnsEdit->setPlaceholderText(tr("0 = unlimited"));
    form->addRow(tr("Max turns (--max-turns)"), m_maxTurnsEdit);

    // Toggles
    m_yoloCheck = new QCheckBox(tr("Skip dangerous-command approval (--yolo)"), this);
    form->addRow(QString(), m_yoloCheck);
    m_worktreeCheck = new QCheckBox(tr("Isolated git worktree (--worktree)"), this);
    form->addRow(QString(), m_worktreeCheck);

    layout->addLayout(form);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(browseButton, &QPushButton::clicked, this, [this]() {
        const QString directory = QFileDialog::getExistingDirectory(
            this, tr("Select Workspace Directory"), m_workspaceEdit->text(),
            QFileDialog::ShowDirsOnly);
        if (!directory.isEmpty()) m_workspaceEdit->setText(directory);
    });
}

QVariantMap HermesOptionsDialog::options() const {
    QVariantMap result;
    const QString model = m_modelEdit->text().trimmed();
    if (!model.isEmpty()) result["model"] = model;

    const QString provider = m_providerCombo->currentText().trimmed();
    if (!provider.isEmpty() && provider != tr("(default)")) result["providerName"] = provider;

    const QString reasoning = m_reasoningCombo->currentText().trimmed();
    if (!reasoning.isEmpty() && reasoning != "default") result["reasoning"] = reasoning;

    const QString toolsets = m_toolsetsEdit->text().trimmed();
    if (!toolsets.isEmpty()) result["toolsets"] = toolsets;

    const QString skills = m_skillsEdit->text().trimmed();
    if (!skills.isEmpty()) result["skills"] = skills;

    const QString profile = m_profileEdit->text().trimmed();
    if (!profile.isEmpty()) result["profile"] = profile;

    int maxTurns = 0;
    bool ok = false;
    maxTurns = m_maxTurnsEdit->text().toInt(&ok);
    if (ok && maxTurns > 0) result["maxTurns"] = maxTurns;

    if (m_yoloCheck->isChecked()) result["yolo"] = true;
    if (m_worktreeCheck->isChecked()) result["worktree"] = true;
    return result;
}

QString HermesOptionsDialog::workspace() const {
    return QDir::cleanPath(m_workspaceEdit->text().trimmed());
}
