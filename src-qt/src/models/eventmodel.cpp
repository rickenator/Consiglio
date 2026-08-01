#include "eventmodel.h"
#include <QDateTime>

EventModel::EventModel(QObject *parent) : QAbstractListModel(parent) {}

void EventModel::addEvent(const EventItem &event) {
    beginInsertRows(QModelIndex(), m_events.size(), m_events.size());
    m_events.append(event);
    endInsertRows();
}

void EventModel::clear() {
    beginResetModel();
    m_events.clear();
    endResetModel();
}

int EventModel::rowCount(const QModelIndex &) const {
    return m_events.size();
}

QVariant EventModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_events.size()) return {};

    const auto &event = m_events[index.row()];
    switch (role) {
        case TypeRole: return static_cast<int>(event.type);
        case ContentRole: return event.content;
        case SessionIdRole: return event.sessionId;
        case TimestampRole: return QDateTime::fromMSecsSinceEpoch(event.timestamp);
        case CommandRole: return event.command;
        case WorkingDirRole: return event.workingDir;
        default: return {};
    }
}

QHash<int, QByteArray> EventModel::roleNames() const {
    return {
        {TypeRole, "type"},
        {ContentRole, "content"},
        {SessionIdRole, "sessionId"},
        {TimestampRole, "timestamp"},
        {CommandRole, "command"},
        {WorkingDirRole, "workingDir"}
    };
}
