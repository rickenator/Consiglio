#pragma once
#include <QWidget>
#include <QTreeView>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include "models/sessionmodel.h"

class SessionList : public QWidget {
    Q_OBJECT
public:
    explicit SessionList(QWidget *parent = nullptr);

signals:
    void sessionSelected(const QString &sessionId);
    void sessionStopped(const QString &sessionId);
    void clearProjectFilterRequested();

public slots:
    void setSessions(const QList<SessionRecord> &sessions);
    void setProjectContext(const QString &projectName, const QString &workspace);
    void onSessionDoubleClicked(const QModelIndex &index);
    void onStopSession();

private:
    void setupUI();
    SessionModel *m_model = nullptr;
    QTreeView *m_treeView = nullptr;
    QLabel *m_contextLabel = nullptr;
    QPushButton *m_clearFilterBtn = nullptr;
    QPushButton *m_stopBtn = nullptr;
    QLabel *m_emptyLabel = nullptr;
};
