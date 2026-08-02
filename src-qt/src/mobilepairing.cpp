#include "mobilepairing.h"
#include <QTimer>
#include <QRandomGenerator>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include "uimetrics.h"

MobilePairing::MobilePairing(QWidget *parent) : QWidget(parent) {
    setupUI();
}

void MobilePairing::setupUI() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(UiMetrics::panelMargin(), UiMetrics::panelMargin(),
                                   UiMetrics::panelMargin(), UiMetrics::panelMargin());
    mainLayout->setSpacing(UiMetrics::panelSpacing());

    // Header
    auto *headerLayout = new QHBoxLayout();
    auto *titleLabel = new QLabel(tr("Mobile Pairing"), this);
    titleLabel->setFont(UiMetrics::titleFont());
    titleLabel->setStyleSheet("color: #f0f6fc;");
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();
    mainLayout->addLayout(headerLayout);

    // QR Code display group
    auto *qrGroup = new QGroupBox(tr("Scan to Pair"), this);
    auto *qrLayout = new QVBoxLayout(qrGroup);

    m_qrLabel = new QLabel(this);
    const int qrSize = UiMetrics::qrCodeSize();
    const int cellSize = qMax(1, qrSize / 10);
    m_qrLabel->setFixedSize(qrSize, qrSize);
    m_qrLabel->setAlignment(Qt::AlignCenter);
    m_qrLabel->setStyleSheet(R"(
        QLabel {
            background: white;
            border: 2px solid #30363d;
            border-radius: 8px;
        }
    )");

    // Generate a placeholder QR-like pattern
    QPixmap qrPixmap(qrSize, qrSize);
    qrPixmap.fill(Qt::white);
    QPainter painter(&qrPixmap);
    painter.setPen(Qt::black);
    // Draw simple grid pattern as placeholder
    for (int i = 0; i < 10; ++i) {
        for (int j = 0; j < 10; ++j) {
            if (QRandomGenerator::global()->bounded(2)) {
                painter.fillRect(QRect(i * cellSize, j * cellSize, cellSize, cellSize), Qt::black);
            }
        }
    }
    // Draw corner markers
    painter.fillRect(QRect(0, 0, cellSize * 3, cellSize * 3), Qt::black);
    painter.fillRect(QRect(cellSize * 7, 0, cellSize * 3, cellSize * 3), Qt::black);
    painter.fillRect(QRect(0, cellSize * 7, cellSize * 3, cellSize * 3), Qt::black);
    painter.fillRect(QRect(cellSize, cellSize, cellSize, cellSize), Qt::white);
    painter.fillRect(QRect(cellSize * 8, cellSize, cellSize, cellSize), Qt::white);
    painter.fillRect(QRect(cellSize, cellSize * 8, cellSize, cellSize), Qt::white);

    m_qrLabel->setPixmap(qrPixmap);
    qrLayout->addWidget(m_qrLabel);

    auto *qrHint = new QLabel(tr("Open the mobile app and scan this QR code"), this);
    qrHint->setAlignment(Qt::AlignCenter);
    qrHint->setFont(UiMetrics::secondaryFont());
    qrHint->setStyleSheet("color: #8b949e;");
    qrLayout->addWidget(qrHint);

    mainLayout->addWidget(qrGroup);

    // Manual pairing group
    auto *manualGroup = new QGroupBox(tr("Manual Pairing"), this);
    auto *manualLayout = new QVBoxLayout(manualGroup);

    auto *codeLayout = new QHBoxLayout();
    m_pairCodeEdit = new QLineEdit(this);
    m_pairCodeEdit->setPlaceholderText(tr("Enter 6-digit code"));
    m_pairCodeEdit->setMaxLength(6);
    m_pairCodeEdit->setStyleSheet(R"(
        QLineEdit {
            background: #161b22;
            border: 1px solid #30363d;
            border-radius: 4px;
            padding: 8px;
            color: #c9d1d9;
            letter-spacing: 0.3em;
        }
        QLineEdit:focus { border-color: #58a6ff; }
    )");
    codeLayout->addWidget(m_pairCodeEdit);

    m_generateBtn = new QPushButton(tr("Generate Code"), this);
    m_generateBtn->setStyleSheet(R"(
        QPushButton {
            background: #21262d;
            border: 1px solid #30363d;
            color: #c9d1d9;
            padding: 8px 12px;
            border-radius: 4px;
        }
        QPushButton:hover { background: #30363d; }
    )");
    codeLayout->addWidget(m_generateBtn);

    manualLayout->addLayout(codeLayout);

    m_connectBtn = new QPushButton(tr("Connect"), this);
    m_connectBtn->setStyleSheet(R"(
        QPushButton {
            background: #238636;
            color: white;
            padding: 10px 24px;
            border-radius: 4px;
            font-weight: bold;
        }
        QPushButton:hover { background: #2ea043; }
    )");
    manualLayout->addWidget(m_connectBtn);

    mainLayout->addWidget(manualGroup);

    // Status label
    m_statusLabel = new QLabel(tr("Status: Not paired"), this);
    m_statusLabel->setFont(UiMetrics::secondaryFont());
    m_statusLabel->setStyleSheet("color: #8b949e;");
    mainLayout->addWidget(m_statusLabel);
    mainLayout->addStretch();

    // Connections
    connect(m_generateBtn, &QPushButton::clicked, this, [this]() {
        auto code = QString("%1%2%3")
            .arg(QRandomGenerator::global()->bounded(10))
            .arg(QRandomGenerator::global()->bounded(10))
            .arg(QRandomGenerator::global()->bounded(10));
        m_pairCodeEdit->setText(code);
        m_statusLabel->setText(tr("Status: Code generated. Share with mobile device."));
    });

    connect(m_connectBtn, &QPushButton::clicked, this, [this]() {
        auto code = m_pairCodeEdit->text().trimmed();
        if (code.length() == 6) {
            m_statusLabel->setText(tr("Status: Connecting..."));
            // Simulate connection
            QTimer::singleShot(1000, this, [this]() {
                m_statusLabel->setText(tr("Status: Paired! ✓"));
                m_statusLabel->setStyleSheet("color: #3fb950;");
            });
        } else {
            QMessageBox::warning(this, tr("Error"), tr("Please enter a valid 6-digit code."));
        }
    });
}
