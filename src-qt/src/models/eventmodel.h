#pragma once
#include <QAbstractListModel>
#include <QString>
#include <QDateTime>
#include <QList>

class EventModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum EventType {
        SystemEvent,
        UserMessage,
        AssistantMessage,
        CommandOutput,
        Error,
        ApprovalRequest
    };

    struct EventItem {
        EventType type = SystemEvent;
        QString content;
        QString sessionId;
        qint64 timestamp = 0;
        QString command;      // for command output events
        QString workingDir;   // for command output events
    };

    enum Role {
        TypeRole = Qt::UserRole + 1,
        ContentRole,
        SessionIdRole,
        TimestampRole,
        CommandRole,
        WorkingDirRole
    };

    explicit EventModel(QObject *parent = nullptr);

    void addEvent(const EventItem &event);
    void clear();
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    QList<EventItem> m_events;
};
