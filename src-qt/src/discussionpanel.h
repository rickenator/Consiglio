#pragma once
#include <QWidget>
#include <QListWidget>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QTextEdit>
#include <QHBoxLayout>

class DiscussionPanel : public QWidget {
    Q_OBJECT
public:
    explicit DiscussionPanel(QWidget *parent = nullptr);

signals:
    void messageSent(const QString &message);

public slots:
    void addMessage(const QString &sender, const QString &content, bool isUser = true);

private:
    void setupUI();

    QListWidget *m_messageList = nullptr;
    QTextEdit *m_inputEdit = nullptr;
    QPushButton *m_sendBtn = nullptr;
    QLabel *m_emptyLabel = nullptr;
};
