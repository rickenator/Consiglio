#pragma once
#include <QWidget>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QGroupBox>

class MobilePairing : public QWidget {
    Q_OBJECT
public:
    explicit MobilePairing(QWidget *parent = nullptr);

private:
    void setupUI();

    QLabel *m_qrLabel = nullptr;
    QLineEdit *m_pairCodeEdit = nullptr;
    QPushButton *m_generateBtn = nullptr;
    QPushButton *m_connectBtn = nullptr;
    QLabel *m_statusLabel = nullptr;
};
