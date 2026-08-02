#include "secretsmanager.h"
#include <QFormLayout>
#include <QHeaderView>
#include <QMessageBox>
#include "uimetrics.h"

SecretsManager::SecretsManager(QWidget *parent) : QWidget(parent) {
    setupUI();
}

void SecretsManager::setupUI() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(UiMetrics::panelMargin(), UiMetrics::panelMargin(),
                                   UiMetrics::panelMargin(), UiMetrics::panelMargin());
    mainLayout->setSpacing(UiMetrics::panelSpacing());

    // Header
    auto *headerLayout = new QHBoxLayout();
    auto *titleLabel = new QLabel(tr("Secrets Manager"), this);
    titleLabel->setFont(UiMetrics::titleFont());
    titleLabel->setStyleSheet("color: #f0f6fc;");
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();
    mainLayout->addLayout(headerLayout);

    // Add secret form
    auto *formLayout = new QFormLayout();
    m_keyEdit = new QLineEdit(this);
    m_keyEdit->setPlaceholderText(tr("Key (e.g., API_KEY)"));
    m_keyEdit->setStyleSheet(R"(
        QLineEdit {
            background: #161b22;
            border: 1px solid #30363d;
            border-radius: 4px;
            padding: 8px;
            color: #c9d1d9;
        }
        QLineEdit:focus { border-color: #58a6ff; }
    )");
    formLayout->addRow(tr("Key:"), m_keyEdit);

    m_valueEdit = new QLineEdit(this);
    m_valueEdit->setPlaceholderText(tr("Value"));
    m_valueEdit->setEchoMode(QLineEdit::Password);
    m_valueEdit->setStyleSheet(R"(
        QLineEdit {
            background: #161b22;
            border: 1px solid #30363d;
            border-radius: 4px;
            padding: 8px;
            color: #c9d1d9;
        }
        QLineEdit:focus { border-color: #58a6ff; }
    )");
    formLayout->addRow(tr("Value:"), m_valueEdit);

    auto *btnLayout = new QHBoxLayout();
    m_addBtn = new QPushButton(tr("Add Secret"), this);
    m_addBtn->setStyleSheet(R"(
        QPushButton {
            background: #238636;
            color: white;
            padding: 8px 16px;
            border-radius: 4px;
            font-weight: bold;
        }
        QPushButton:hover { background: #2ea043; }
    )");
    btnLayout->addWidget(m_addBtn);

    m_deleteBtn = new QPushButton(tr("Delete"), this);
    m_deleteBtn->setStyleSheet(R"(
        QPushButton {
            background: #da3633;
            color: white;
            padding: 8px 16px;
            border-radius: 4px;
        }
        QPushButton:hover { background: #f85149; }
    )");
    btnLayout->addWidget(m_deleteBtn);

    mainLayout->addLayout(formLayout);
    mainLayout->addLayout(btnLayout);

    // Secrets table
    m_table = new QTableWidget(this);
    m_table->setColumnCount(2);
    m_table->setHorizontalHeaderLabels({tr("Key"), tr("Value")});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->setStyleSheet(R"(
        QTableWidget {
            background: #0d1117;
            border: 1px solid #30363d;
            border-radius: 6px;
            gridline-color: #21262d;
        }
        QTableWidget::item {
            padding: 8px;
        }
        QTableWidget::item:selected {
            background: rgba(88, 166, 255, 0.15);
        }
        QHeaderView::section {
            background: #161b22;
            color: #c9d1d9;
            padding: 8px;
            border: none;
            font-weight: bold;
        }
    )");

    mainLayout->addWidget(m_table);

    // Connections
    connect(m_addBtn, &QPushButton::clicked, this, [this]() {
        auto key = m_keyEdit->text().trimmed();
        auto value = m_valueEdit->text();
        if (key.isEmpty()) {
            QMessageBox::warning(this, tr("Error"), tr("Key cannot be empty."));
            return;
        }

        // Check for duplicate
        for (int i = 0; i < m_table->rowCount(); ++i) {
            if (m_table->item(i, 0)->text() == key) {
                m_table->setItem(i, 1, new QTableWidgetItem(value));
                QMessageBox::information(this, tr("Updated"), tr("Secret '%1' updated.").arg(key));
                return;
            }
        }

        // Add new row
        int row = m_table->rowCount();
        m_table->insertRow(row);
        m_table->setItem(row, 0, new QTableWidgetItem(key));
        m_table->setItem(row, 1, new QTableWidgetItem(value));

        m_keyEdit->clear();
        m_valueEdit->clear();
    });

    connect(m_deleteBtn, &QPushButton::clicked, this, [this]() {
        auto currentRow = m_table->currentRow();
        if (currentRow >= 0) {
            m_table->removeRow(currentRow);
        }
    });
}
