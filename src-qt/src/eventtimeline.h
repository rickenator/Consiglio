#pragma once
#include <QWidget>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QTextEdit>
#include "models/eventmodel.h"

// A single rendered conversation entry. Assistant messages render as rich
// markdown; user messages as right-aligned bubbles; tool output as collapsible
// code blocks; system/error/approval as compact status rows.
class MessageWidget : public QWidget {
    Q_OBJECT
public:
    explicit MessageWidget(const EventModel::EventItem &event, QWidget *parent = nullptr);
    void setCollapsed(bool collapsed);
    bool isCollapsed() const;

private:
    void build(const EventModel::EventItem &event);

    QWidget *m_body = nullptr;
    QPushButton *m_toggleBtn = nullptr;   // only for collapsible output
    QTextEdit *m_collapsibleBody = nullptr; // the code block, hidden when collapsed
    bool m_collapsed = false;
};

// The conversation pane: a scrollable stack of MessageWidgets above a composer.
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
    void scrollToBottom();

    QScrollArea *m_scroll = nullptr;
    QWidget *m_messagesHost = nullptr;
    QVBoxLayout *m_messagesLayout = nullptr;
    QLabel *m_emptyLabel = nullptr;

    QLineEdit *m_commandInput = nullptr;
    QPushButton *m_sendBtn = nullptr;
    QLabel *m_thinkingIndicator = nullptr;
    QTimer *m_thinkingTimer = nullptr;
    int m_thinkingFrame = 0;
    bool m_thinking = false;
};
