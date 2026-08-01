#pragma once
#include <QSet>

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>

struct ApprovalRequest {
    QString id;
    QString sessionId;
    QString command;
    QString workingDir;
    QString sandboxPolicy;
    QStringList affectedPaths;
    qint64 timestamp = 0;
    enum Status { Pending, Approved, Rejected } status = Pending;
};

class ApprovalRouter : public QObject {
    Q_OBJECT

public:
    explicit ApprovalRouter(QObject *parent = nullptr);

    bool registerApproval(const ApprovalRequest &request);
    bool resolve(const QString &id, bool approved);
    QList<ApprovalRequest> pendingApprovals() const;
    QList<ApprovalRequest> pendingApprovals(const QString &sessionId) const;
    QStringList pendingIds() const;
    bool has(const QString &id) const;
    int pendingCount() const;
    QStringList clearSession(const QString &sessionId);

signals:
    void approvalRequested(const ApprovalRequest &request);
    void approvalResolved(const QString &id, bool approved);
    void pendingCountChanged(int count);

private:
    struct Entry {
        ApprovalRequest approval;
        QString targetSessionId;
    };
    QMap<QString, Entry> m_pending;
    QSet<QString> m_resolved;
};
