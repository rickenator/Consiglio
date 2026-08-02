#include "settingsdialog.h"
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QGroupBox>
#include <QComboBox>
#include <QApplication>
#include <QScreen>
#include "uimetrics.h"

SettingsDialog::SettingsDialog(AppSettings *settings, AgentDetector *agentDetector, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Settings"));
    const QSize available = QApplication::primaryScreen()->availableGeometry().size();
    setMinimumSize(qMin(UiMetrics::px(600), available.width()),
                   qMin(UiMetrics::px(500), available.height()));

    auto *mainLayout = new QVBoxLayout(this);

    auto *tabs = new QTabWidget(this);

    // ─── General Tab ──────────────────────────────────────────────
    auto *generalPage = new QWidget();
    auto *generalLayout = new QFormLayout(generalPage);

    auto *providerCombo = new QComboBox(generalPage);
    providerCombo->addItems({"codex", "ollama", "remote_llamacpp", "lan"});
    int idx = providerCombo->findText(settings->defaultProvider);
    if (idx >= 0) providerCombo->setCurrentIndex(idx);
    generalLayout->addRow(tr("Default Provider:"), providerCombo);

    auto *modelEdit = new QLineEdit(settings->defaultModel, generalPage);
    generalLayout->addRow(tr("Default Model:"), modelEdit);

    tabs->addTab(generalPage, tr("General"));

    // ─── Ollama Tab ───────────────────────────────────────────────
    auto *ollamaPage = new QWidget();
    auto *ollamaLayout = new QFormLayout(ollamaPage);

    auto *ollamaUrl = new QLineEdit(settings->ollama.baseUrl, ollamaPage);
    ollamaLayout->addRow(tr("Base URL:"), ollamaUrl);

    auto *ollamaModel = new QLineEdit(settings->ollama.model, ollamaPage);
    ollamaLayout->addRow(tr("Model:"), ollamaModel);

    auto *ollamaKey = new QLineEdit(settings->ollama.apiKey, ollamaPage);
    ollamaKey->setEchoMode(QLineEdit::Password);
    ollamaLayout->addRow(tr("API Key:"), ollamaKey);

    tabs->addTab(ollamaPage, tr("Ollama"));

    // ─── Remote llama.cpp Tab ─────────────────────────────────────
    auto *llamaPage = new QWidget();
    auto *llamaLayout = new QFormLayout(llamaPage);

    auto *llamaUrl = new QLineEdit(settings->remoteLlamaCpp.baseUrl, llamaPage);
    llamaLayout->addRow(tr("Base URL:"), llamaUrl);

    auto *llamaModel = new QLineEdit(settings->remoteLlamaCpp.model, llamaPage);
    llamaLayout->addRow(tr("Model:"), llamaModel);

    auto *llamaKey = new QLineEdit(settings->remoteLlamaCpp.apiKey, llamaPage);
    llamaKey->setEchoMode(QLineEdit::Password);
    llamaLayout->addRow(tr("API Key:"), llamaKey);

    tabs->addTab(llamaPage, tr("Remote llama.cpp"));

    // ─── LAN Providers Tab ────────────────────────────────────────
    auto *lanPage = new QWidget();
    auto *lanLayout = new QVBoxLayout(lanPage);

    auto *lanTableLabel = new QLabel(tr("LAN-discovered providers:"), lanPage);
    lanTableLabel->setStyleSheet("font-weight: bold;");
    lanLayout->addWidget(lanTableLabel);

    if (agentDetector) {
        auto agents = agentDetector->detectAll();
        for (const auto &a : agents) {
            auto *row = new QHBoxLayout();
            auto *name = new QLabel(a.name, lanPage);
            name->setStyleSheet("font-weight: bold;");
            auto *status = new QLabel(a.diagnostic, lanPage);
            status->setFont(UiMetrics::secondaryFont());
            status->setStyleSheet("color: #8b949e;");
            row->addWidget(name);
            row->addStretch();
            row->addWidget(status);
            lanLayout->addLayout(row);
        }
    }

    tabs->addTab(lanPage, tr("LAN Providers"));

    // ─── Local Behavior Tab ───────────────────────────────────────
    auto *behaviorPage = new QWidget();
    auto *behaviorLayout = new QVBoxLayout(behaviorPage);

    auto *isolateCheck = new QCheckBox(tr("Isolate each profile in its own workspace"), behaviorPage);
    isolateCheck->setChecked(settings->localProviderBehavior.isolateProfile);
    behaviorLayout->addWidget(isolateCheck);

    auto *searchCheck = new QCheckBox(tr("Enable web search for local providers"), behaviorPage);
    searchCheck->setChecked(settings->localProviderBehavior.enableWebSearch);
    behaviorLayout->addWidget(searchCheck);

    auto *multiAgentCheck = new QCheckBox(tr("Enable multi-agent collaboration"), behaviorPage);
    multiAgentCheck->setChecked(settings->localProviderBehavior.enableMultiAgent);
    behaviorLayout->addWidget(multiAgentCheck);

    tabs->addTab(behaviorPage, tr("Local Behavior"));

    mainLayout->addWidget(tabs);
    mainLayout->addStretch();

    // ─── Buttons ──────────────────────────────────────────────────
    auto *buttonLayout = new QHBoxLayout();
    auto *saveBtn = new QPushButton(tr("Save"), this);
    saveBtn->setStyleSheet(R"(
        QPushButton {
            background: #238636;
            color: white;
            padding: 8px 24px;
            border-radius: 4px;
            font-weight: bold;
        }
        QPushButton:hover { background: #2ea043; }
    )");
    auto *cancelBtn = new QPushButton(tr("Cancel"), this);
    cancelBtn->setStyleSheet(R"(
        QPushButton {
            background: transparent;
            border: 1px solid #30363d;
            color: #c9d1d9;
            padding: 8px 24px;
            border-radius: 4px;
        }
        QPushButton:hover { background: rgba(255,255,255,0.06); }
    )");

    connect(saveBtn, &QPushButton::clicked, this, [this, settings, providerCombo, modelEdit,
            ollamaUrl, ollamaModel, ollamaKey, llamaUrl, llamaModel, llamaKey,
            isolateCheck, searchCheck, multiAgentCheck]() {
        settings->defaultProvider = providerCombo->currentText();
        settings->defaultModel = modelEdit->text();
        settings->ollama.baseUrl = ollamaUrl->text();
        settings->ollama.model = ollamaModel->text();
        settings->ollama.apiKey = ollamaKey->text();
        settings->remoteLlamaCpp.baseUrl = llamaUrl->text();
        settings->remoteLlamaCpp.model = llamaModel->text();
        settings->remoteLlamaCpp.apiKey = llamaKey->text();
        settings->localProviderBehavior.isolateProfile = isolateCheck->isChecked();
        settings->localProviderBehavior.enableWebSearch = searchCheck->isChecked();
        settings->localProviderBehavior.enableMultiAgent = multiAgentCheck->isChecked();
        accept();
    });

    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    buttonLayout->addStretch();
    buttonLayout->addWidget(cancelBtn);
    buttonLayout->addWidget(saveBtn);
    mainLayout->addLayout(buttonLayout);
}
