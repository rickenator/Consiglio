#pragma once

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;

class NewSessionDialog : public QDialog {
    Q_OBJECT

public:
    explicit NewSessionDialog(const QString &provider, const QString &initialDirectory,
                              QWidget *parent = nullptr);

    QString workspace() const;
    QString sandboxMode() const;

private:
    void updatePermissionDescription();
    void validateAndAccept();

    QString m_provider;
    QLineEdit *m_workspaceEdit = nullptr;
    QComboBox *m_permissionCombo = nullptr;
    QLabel *m_permissionDescription = nullptr;
};
