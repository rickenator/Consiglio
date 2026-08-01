#pragma once
#include <QLineEdit>
#include <QWidget>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
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

private:
    void setupUI();
    void appendEvent(const EventModel::EventItem &event);
    QString formatTimestamp(qint64 ms) const;

    QTextEdit *m_textEdit = nullptr;
    QLineEdit *m_commandInput = nullptr;
    QPushButton *m_sendBtn = nullptr;
    QLabel *m_emptyLabel = nullptr;
};
