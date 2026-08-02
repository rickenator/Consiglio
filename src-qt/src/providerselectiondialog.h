#pragma once

#include <QDialog>
#include <QList>
#include "backend/agentdetector.h"

class QListWidget;
class QPushButton;
class QComboBox;
class QLabel;
class QLineEdit;

class ProviderSelectionDialog : public QDialog {
    Q_OBJECT

public:
    explicit ProviderSelectionDialog(const QList<AgentInfo> &providers,
                                     const QString &preferredProvider,
                                     const QString &initialDirectory,
                                     QWidget *parent = nullptr);

    QString selectedProvider() const;
    QString selectedEndpoint() const;
    QString selectedModel() const;
    QString selectedWorkspace() const;
    QString selectedSandboxMode() const;

private:
    void updatePermissionDescription();
    void validateAndAccept();

    QListWidget *m_providerList = nullptr;
    QPushButton *m_continueButton = nullptr;
    QLineEdit *m_workspaceEdit = nullptr;
    QComboBox *m_permissionCombo = nullptr;
    QLabel *m_permissionDescription = nullptr;
};
