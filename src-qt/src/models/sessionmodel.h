#pragma once
#include <QAbstractListModel>
#include <QList>
#include "backend/sessionmanager.h"

class SessionModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        ProviderRole,
        StatusRole,
        RepositoryRole,
        BranchRole,
        ProjectIdRole,
        StartedAtRole,
        LastActivityRole
    };

    explicit SessionModel(QObject *parent = nullptr);

    void setSessions(const QList<SessionRecord> &sessions);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    QList<SessionRecord> m_sessions;
};
