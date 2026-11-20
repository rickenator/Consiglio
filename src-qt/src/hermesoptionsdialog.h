#pragma once

#include <QDialog>
#include <QVariantMap>

class QComboBox;
class QLineEdit;
class QCheckBox;
class QLabel;

// The Hermes CLI options surface. Mirrors the flags you'd pass to
// `hermes chat` so the desktop app exposes the same knobs as the terminal:
// model, provider, reasoning level, toolsets, skills, profile, max-turns,
// yolo, and worktree. Returns them as a QVariantMap consumed by
// SessionManager::startSession / the active-session options path.
class HermesOptionsDialog : public QDialog {
    Q_OBJECT

public:
    explicit HermesOptionsDialog(const QString &initialWorkspace,
                                 const QVariantMap &current = {},
                                 QWidget *parent = nullptr);

    // The assembled option map (keys match SessionManager expectations).
    QVariantMap options() const;
    QString workspace() const;

private:
    void setupUI();

    QLineEdit *m_workspaceEdit = nullptr;
    QComboBox *m_modelCombo = nullptr;
    QLineEdit *m_modelEdit = nullptr;
    QComboBox *m_providerCombo = nullptr;
    QComboBox *m_reasoningCombo = nullptr;
    QLineEdit *m_toolsetsEdit = nullptr;
    QLineEdit *m_skillsEdit = nullptr;
    QLineEdit *m_profileEdit = nullptr;
    QLineEdit *m_maxTurnsEdit = nullptr;
    QCheckBox *m_yoloCheck = nullptr;
    QCheckBox *m_worktreeCheck = nullptr;
};
