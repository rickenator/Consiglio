#pragma once

#include <QDialog>
#include <functional>

#include "hermessetup.h"

class QButtonGroup;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;

// First-run "Set up Hermes Agent" dialog. It shows what was detected and asks
// before anything is installed or changed. With an existing Hermes the default
// is to use it exactly as it is.
class HermesSetupDialog : public QDialog {
    Q_OBJECT
public:
    enum class Option { UseExisting, ChooseExecutable, InstallSeparate, InstallOfficial, InstallIntoExistingData };

    HermesSetupDialog(const HermesSetup::Environment &env, const HermesSetup::Detection &detection,
                      QWidget *parent = nullptr);

    Option selectedOption() const;
    void selectOption(Option option);
    bool isOptionAvailable(Option option) const;
    bool isOptionEnabled(Option option) const;
    QString commandPreview() const;
    QString executablePath() const;
    void setExecutablePath(const QString &path);
    QString separateRoot() const;
    void setSeparateRoot(const QString &path);
    const HermesSetup::Detection &detection() const { return m_detection; }
    HermesSetup::Choice savedChoice() const { return m_saved; }
    bool installerStarted() const { return m_installerStarted; }

    // Re-run detection (after the installer finishes).
    void recheck();
    // Test hook: replaces opening a real terminal.
    void setInstallerLauncher(std::function<bool(const HermesSetup::InstallCommand &, QString *)> launcher);

    // Show the dialog when no choice has been saved yet. Returns the saved
    // choice (None if the dialog was closed without one).
    static HermesSetup::Choice runIfNeeded(QWidget *parent = nullptr);

private:
    void build();
    void rebuildDetails();
    void updateState();
    void onContinue();
    void onSkip();
    bool startInstaller(const HermesSetup::InstallCommand &cmd);
    QRadioButton *radio(Option o) const;

    HermesSetup::Environment m_env;
    HermesSetup::Detection m_detection;
    HermesSetup::Choice m_saved = HermesSetup::Choice::None;
    bool m_installerStarted = false;
    Option m_pendingInstall = Option::InstallOfficial;
    std::function<bool(const HermesSetup::InstallCommand &, QString *)> m_launcher;

    QLabel *m_summary = nullptr;
    QWidget *m_details = nullptr;
    QButtonGroup *m_group = nullptr;
    QLineEdit *m_exePath = nullptr;
    QLineEdit *m_rootPath = nullptr;
    QCheckBox *m_consent = nullptr;
    QLabel *m_preview = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_continue = nullptr;
    QPushButton *m_skip = nullptr;
};
