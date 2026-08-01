#include "approvalrouter.h"
#include <QUuid>

ApprovalRouter::ApprovalRouter(QObject *parent) : QObject(parent) {}

bool ApprovalRouter::registerApproval(const ApprovalRequest &request) {
    if (request.id.isEmpty() || request.sessionId.isEmpty()) return false;
    if (m_pending.contains(request.id) || m_resolved.contains(request.id)) return false;

    Entry entry;
    entry.approval = request;
    entry.targetSessionId = request.sessionId;
    m_pending[request.id] = entry;

    emit approvalRequested(request);
    emit pendingCountChanged(m_pending.size());
    return true;
}

bool ApprovalRouter::resolve(const QString &id, bool approved) {
    if (m_resolved.contains(id)) return false;

    auto it = m_pending.find(id);
    if (it == m_pending.end()) return false;

    auto entry = it.value();
    m_pending.erase(it);
    m_resolved.insert(id);

    emit approvalResolved(id, approved);
    emit pendingCountChanged(m_pending.size());
    return true;
}

QList<ApprovalRequest> ApprovalRouter::pendingApprovals() const {
    QList<ApprovalRequest> result;
    for (const auto &entry : m_pending) {
        result.append(entry.approval);
    }
    // Sort by timestamp descending
    std::sort(result.begin(), result.end(), [](const ApprovalRequest &a, const ApprovalRequest &b) {
        return a.timestamp > b.timestamp;
    });
    return result;
}

QList<ApprovalRequest> ApprovalRouter::pendingApprovals(const QString &sessionId) const {
    QList<ApprovalRequest> result;
    for (const auto &entry : m_pending) {
        if (entry.approval.sessionId == sessionId || entry.targetSessionId == sessionId) {
            result.append(entry.approval);
        }
    }
    std::sort(result.begin(), result.end(), [](const ApprovalRequest &a, const ApprovalRequest &b) {
        return a.timestamp > b.timestamp;
    });
    return result;
}

QStringList ApprovalRouter::pendingIds() const {
    QStringList ids;
    for (const auto &key : m_pending.keys()) {
        ids.append(key);
    }
    return ids;
}

bool ApprovalRouter::has(const QString &id) const {
    return m_pending.contains(id);
}

int ApprovalRouter::pendingCount() const {
    return m_pending.size();
}

QStringList ApprovalRouter::clearSession(const QString &sessionId) {
    QStringList cleared;
    auto it = m_pending.begin();
    while (it != m_pending.end()) {
        if (it.value().approval.sessionId == sessionId || it.value().targetSessionId == sessionId) {
            m_resolved.insert(it.key());
            it = m_pending.erase(it);
            cleared.append(cleared.last() + QString()); // placeholder
        } else {
            ++it;
        }
    }
    // Re-collect cleared IDs properly
    cleared.clear();
    for (const auto &id : m_resolved) {
        if (!m_pending.contains(id)) {
            cleared.append(id);
        }
    }
    emit pendingCountChanged(m_pending.size());
    return cleared;
}
