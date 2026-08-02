#include "approvaldialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QGroupBox>
#include <QApplication>
#include <QScreen>
#include "uimetrics.h"

ConsiglioCmdApproval::ConsiglioCmdApproval(const ApprovalRequest &request, QWidget *parent)
    : QWidget(parent, Qt::Dialog | Qt::WindowStaysOnTopHint)
{
    setWindowTitle(tr("Command Approval Required"));
    const QSize available = QApplication::primaryScreen()->availableGeometry().size();
    setMinimumSize(qMin(UiMetrics::px(500), available.width()),
                   qMin(UiMetrics::px(300), available.height()));

    auto *mainLayout = new QVBoxLayout(this);

    // Warning label
    auto *warning = new QLabel(tr("⚠ The agent is requesting permission to run a command"), this);
    warning->setStyleSheet("color: #f0883e; font-weight: bold;");
    mainLayout->addWidget(warning);

    // Command details group
    auto *detailsGroup = new QGroupBox(tr("Command Details"), this);
    auto *detailsLayout = new QVBoxLayout(detailsGroup);

    auto addDetail = [detailsLayout, detailsGroup](const QString &label, const QString &value) {
        auto *hLayout = new QHBoxLayout();
        auto *l = new QLabel(label, detailsGroup);
        l->setStyleSheet("font-weight: bold; color: #8b949e; min-width: 100px;");
        auto *v = new QLabel(value, detailsGroup);
        v->setWordWrap(true);
        hLayout->addWidget(l);
        hLayout->addWidget(v);
        detailsLayout->addLayout(hLayout);
    };

    addDetail("Command:", request.command);
    addDetail("Working Directory:", request.workingDir);
    addDetail("Sandbox Policy:", request.sandboxPolicy.isEmpty() ? "default" : request.sandboxPolicy);
    if (!request.affectedPaths.isEmpty()) {
        auto *pathsLabel = new QLabel(tr("Affected Paths:"), detailsGroup);
        pathsLabel->setStyleSheet("font-weight: bold; color: #8b949e;");
        detailsLayout->addWidget(pathsLabel);
        auto *pathsText = new QTextEdit(detailsGroup);
        pathsText->setReadOnly(true);
        pathsText->setMaximumHeight(UiMetrics::px(80));
        pathsText->setPlainText(request.affectedPaths.join("\n"));
        detailsLayout->addWidget(pathsText);
    }

    mainLayout->addWidget(detailsGroup);
    mainLayout->addStretch();

    // Buttons
    auto *buttonLayout = new QHBoxLayout();
    auto *rejectBtn = new QPushButton(tr("Reject"), this);
    rejectBtn->setStyleSheet(R"(
        QPushButton {
            background: #da3633;
            color: white;
            padding: 8px 20px;
            border-radius: 4px;
            font-weight: bold;
        }
        QPushButton:hover { background: #f85149; }
    )");
    auto *approveBtn = new QPushButton(tr("Approve"), this);
    approveBtn->setStyleSheet(R"(
        QPushButton {
            background: #238636;
            color: white;
            padding: 8px 20px;
            border-radius: 4px;
            font-weight: bold;
        }
        QPushButton:hover { background: #2ea043; }
    )");

    connect(rejectBtn, &QPushButton::clicked, this, [this]() { emit rejected(); close(); });
    connect(approveBtn, &QPushButton::clicked, this, [this]() { emit approved(); close(); });

    buttonLayout->addWidget(rejectBtn);
    buttonLayout->addStretch();
    buttonLayout->addWidget(approveBtn);
    mainLayout->addLayout(buttonLayout);
}
