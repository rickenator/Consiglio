#include "mobilepairing.h"
#include <QTimer>
#include <QRandomGenerator>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>

MobilePairing::MobilePairing(QWidget *parent) : QWidget(parent) {
    setupUI();
}

void MobilePairing::setupUI() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);

    // Header
    auto *headerLayout = new QHBoxLayout();
    auto *titleLabel = new QLabel(tr("Mobile Pairing"), this);
    titleLabel->setStyleSheet("font-size: 20px; font-weight: bold; color: #58a6ff;");
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();
    mainLayout->addLayout(headerLayout);

    // QR Code display group
    auto *qrGroup = new QGroupBox(tr("Scan to Pair"), this);
    auto *qrLayout = new QVBoxLayout(qrGroup);

    m_qrLabel = new QLabel(this);
    m_qrLabel->setMinimumSize(200, 200);
    m_qrLabel->setMaximumSize(200, 200);
    m_qrLabel->setAlignment(Qt::AlignCenter);
    m_qrLabel->setStyleSheet(R"(
        QLabel {
            background: white;
            border: 2px solid #30363d;
            border-radius: 8px;
        }
    )");

    // Generate a placeholder QR-like pattern
    QPixmap qrPixmap(200, 200);
    qrPixmap.fill(Qt::white);
    QPainter painter(&qrPixmap);
    painter.setPen(Qt::black);
    // Draw simple grid pattern as placeholder
    for (int i = 0; i < 10; ++i) {
        for (int j = 0; j < 10; ++j) {
            if (QRandomGenerator::global()->bounded(2)) {
                painter.fillRect(QRect(i * 20, j * 20, 20, 20), Qt::black);
            }
        }
    }
    // Draw corner markers
    painter.fillRect(QRect(0, 0, 50, 50), Qt::black);
    painter.fillRect(QRect(150, 0, 50, 50), Qt::black);
    painter.fillRect(QRect(0, 150, 50, 50), Qt::black);
    painter.fillRect(QRect(20, 20, 10, 10), Qt::white);
    painter.fillRect(QRect(170, 20, 10, 10), Qt::white);
    painter.fillRect(QRect(20, 170, 10, 10), Qt::white);

    m_qrLabel->setPixmap(qrPixmap);
    qrLayout->addWidget(m_qrLabel);

    auto *qrHint = new QLabel(tr("Open the mobile app and scan this QR code"), this);
    qrHint->setAlignment(Qt::AlignCenter);
    qrHint->setStyleSheet("color: #8b949e; font-size: 12px;");
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
            font-size: 16px;
            letter-spacing: 4px;
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
            font-size: 14px;
        }
        QPushButton:hover { background: #2ea043; }
    )");
    manualLayout->addWidget(m_connectBtn);

    mainLayout->addWidget(manualGroup);

    // Status label
    m_statusLabel = new QLabel(tr("Status: Not paired"), this);
    m_statusLabel->setStyleSheet("color: #8b949e; font-size: 13px;");
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
                m_statusLabel->setStyleSheet("color: #3fb950; font-size: 13px;");
            });
        } else {
            QMessageBox::warning(this, tr("Error"), tr("Please enter a valid 6-digit code."));
        }
    });
}
