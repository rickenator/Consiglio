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
    void eventsCleared();

public slots:
    void addEvent(const EventModel::EventItem &event);
    void setEvents(const QList<EventModel::EventItem> &events);
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
