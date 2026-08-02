#pragma once

#include <QDialog>
#include <QList>
#include "backend/agentdetector.h"

class QListWidget;
class QPushButton;

class ProviderSelectionDialog : public QDialog {
    Q_OBJECT

public:
    explicit ProviderSelectionDialog(const QList<AgentInfo> &providers,
                                     const QString &preferredProvider,
                                     QWidget *parent = nullptr);

    QString selectedProvider() const;
    QString selectedEndpoint() const;
    QString selectedModel() const;

private:
    QListWidget *m_providerList = nullptr;
    QPushButton *m_continueButton = nullptr;
};
