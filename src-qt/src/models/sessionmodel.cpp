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
    if (role == Qt::DisplayRole) {
        const QString workspace = record.repository.isEmpty()
            ? tr("No workspace") : record.repository;
        const QString activity = record.lastActivity > 0
            ? QDateTime::fromMSecsSinceEpoch(record.lastActivity).toString("MMM d, h:mm AP")
            : tr("No activity");
        return QString("%1  ·  %2\n%3\n%4")
            .arg(record.provider, record.status, workspace, activity);
    }
    switch (role) {
        case IdRole: return record.id;
        case ProviderRole: return record.provider;
        case StatusRole: return record.status;
        case RepositoryRole: return record.repository;
        case BranchRole: return record.branch;
        case ProjectIdRole: return record.projectId;
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
        {ProjectIdRole, "projectId"},
        {StartedAtRole, "startedAt"},
        {LastActivityRole, "lastActivity"}
    };
}
