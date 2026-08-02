#pragma once
#include <QLineEdit>
#include <QWidget>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include "models/eventmodel.h"

class EventTimeline : public QWidget {
    Q_OBJECT
public:
    explicit EventTimeline(QWidget *parent = nullptr);

signals:
    void commandExecuted(const QString &command, const QString &workingDir);

public slots:
    void addEvent(const EventModel::EventItem &event);
    void clearEvents();
    void onSendCommand();
    void setThinking(bool thinking);

public:
    bool isThinking() const;

private:
    void setupUI();
    void appendEvent(const EventModel::EventItem &event);
    QString formatTimestamp(qint64 ms) const;

    QTextEdit *m_textEdit = nullptr;
    QLineEdit *m_commandInput = nullptr;
    QPushButton *m_sendBtn = nullptr;
    QLabel *m_emptyLabel = nullptr;
    QLabel *m_thinkingIndicator = nullptr;
    QTimer *m_thinkingTimer = nullptr;
    int m_thinkingFrame = 0;
    bool m_thinking = false;
};
