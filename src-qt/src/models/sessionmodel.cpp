#include "sessionmodel.h"
#include <QDateTime>

SessionModel::SessionModel(QObject *parent) : QAbstractListModel(parent) {}

void SessionModel::setSessions(const QList<SessionRecord> &sessions) {
    beginResetModel();
    m_sessions = sessions;
    endResetModel();
}

int SessionModel::rowCount(const QModelIndex &) const {
    return m_sessions.size();
}

QVariant SessionModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_sessions.size()) return {};

    const auto &record = m_sessions[index.row()];
    switch (role) {
        case IdRole: return record.id;
        case ProviderRole: return record.provider;
        case StatusRole: return record.status;
        case RepositoryRole: return record.repository;
        case BranchRole: return record.branch;
        case StartedAtRole: return QDateTime::fromMSecsSinceEpoch(record.startedAt);
        case LastActivityRole: return QDateTime::fromMSecsSinceEpoch(record.lastActivity);
        default: return {};
    }
}

QHash<int, QByteArray> SessionModel::roleNames() const {
    return {
        {IdRole, "id"},
        {ProviderRole, "provider"},
        {StatusRole, "status"},
        {RepositoryRole, "repository"},
        {BranchRole, "branch"},
        {StartedAtRole, "startedAt"},
        {LastActivityRole, "lastActivity"}
    };
}
