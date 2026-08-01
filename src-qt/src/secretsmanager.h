#pragma once
#include <QWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QDialog>

class SecretsManager : public QWidget {
    Q_OBJECT
public:
    explicit SecretsManager(QWidget *parent = nullptr);

private:
    void setupUI();

    QTableWidget *m_table = nullptr;
    QLineEdit *m_keyEdit = nullptr;
    QLineEdit *m_valueEdit = nullptr;
    QPushButton *m_addBtn = nullptr;
    QPushButton *m_deleteBtn = nullptr;
};
