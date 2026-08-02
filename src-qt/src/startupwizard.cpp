#include "startupwizard.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QGroupBox>
#include <QProgressBar>
#include <QMessageBox>
#include <QApplication>
#include <QScreen>
#include "uimetrics.h"

StartupWizard::StartupWizard(AgentDetector *agentDetector, AppSettings *settings, QWidget *parent)
    : QDialog(parent, Qt::Dialog | Qt::WindowStaysOnTopHint)
{
    setWindowTitle(tr("Set up Consiglio"));
    const QSize available = QApplication::primaryScreen()->availableGeometry().size();
    setMinimumSize(qMin(UiMetrics::px(550), available.width()),
                   qMin(UiMetrics::px(450), available.height()));

    auto *mainLayout = new QVBoxLayout(this);

    // Header
    auto *header = new QLabel(tr("Set up Consiglio"), this);
    header->setFont(UiMetrics::titleFont());
    header->setStyleSheet("color: #58a6ff;");
    mainLayout->addWidget(header);

    auto *subtitle = new QLabel(tr("Let's detect your available AI agents and configure your first session."), this);
    subtitle->setWordWrap(true);
    subtitle->setStyleSheet("color: #8b949e;");
    mainLayout->addWidget(subtitle);
    mainLayout->addSpacing(10);

    // Detection progress
    auto *progressLabel = new QLabel(tr("Detecting agents..."), this);
    mainLayout->addWidget(progressLabel);

    auto *progressBar = new QProgressBar(this);
    progressBar->setMaximumHeight(UiMetrics::px(6));
    progressBar->setRange(0, 0); // indeterminate
    mainLayout->addWidget(progressBar);

    // Agent results list
    auto *resultsGroup = new QGroupBox(tr("Detected Agents"), this);
    auto *resultsLayout = new QVBoxLayout(resultsGroup);

    auto *agentList = new QListWidget(resultsGroup);
    agentList->setMinimumHeight(UiMetrics::px(150));
    resultsLayout->addWidget(agentList);
    mainLayout->addWidget(resultsGroup);
    mainLayout->addStretch();

    // Buttons
    auto *buttonLayout = new QHBoxLayout();
    auto *skipBtn = new QPushButton(tr("Skip for Now"), this);
    skipBtn->setStyleSheet(R"(
        QPushButton {
            background: transparent;
            border: 1px solid #30363d;
            color: #c9d1d9;
            padding: 8px 20px;
            border-radius: 4px;
        }
        QPushButton:hover { background: rgba(255,255,255,0.06); }
    )");
    auto *continueBtn = new QPushButton(tr("Continue"), this);
    continueBtn->setStyleSheet(R"(
        QPushButton {
            background: #238636;
            color: white;
            padding: 8px 24px;
            border-radius: 4px;
            font-weight: bold;
        }
        QPushButton:hover { background: #2ea043; }
    )");

    buttonLayout->addWidget(skipBtn);
    buttonLayout->addStretch();
    buttonLayout->addWidget(continueBtn);
    mainLayout->addLayout(buttonLayout);

    // Run detection
    if (agentDetector) {
        connect(agentDetector, &AgentDetector::detectionCompleted, this, [this, agentList, progressLabel, progressBar](const QList<AgentInfo> &agents) {
            progressBar->setRange(0, 100);
            progressBar->setValue(100);
            progressLabel->setText(tr("Detection complete!"));

            for (const auto &a : agents) {
                auto *item = new QListWidgetItem(agentList);
                QString icon = a.installed ? "✅" : "❌";
                item->setText(QString("%1 %2").arg(icon).arg(a.name));
                item->setData(Qt::UserRole, a.diagnostic);

                auto *detailLabel = new QLabel(a.diagnostic, agentList);
                detailLabel->setFont(UiMetrics::secondaryFont());
                detailLabel->setStyleSheet("color: #8b949e;");
                agentList->setItemWidget(item, detailLabel);
            }

            int installed = 0;
            for (const auto &a : agents) {
                if (a.installed) installed++;
            }
            if (installed == 0) {
                progressLabel->setText(tr("No agents detected. Install one to get started."));
                progressLabel->setStyleSheet("color: #f0883e;");
            }
        });

        agentDetector->startDetection();
    }

    connect(continueBtn, &QPushButton::clicked, this, [this, settings]() {
        if (!settings->hasRunSetup()) {
            settings->defaultProvider = "codex";
            settings->providerConfigured = true;
            settings->save();
        }
        accept();
    });

    connect(skipBtn, &QPushButton::clicked, this, &QDialog::reject);
}
