#pragma once

#include <QDialog>
#include <QPair>
#include <QPointer>
#include <functional>

class AcpClient;
class FontSettings;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QVBoxLayout;
class QButtonGroup;
namespace ui { class Spinner; class StatusDot; }

// Preferences + everything about the Hermes install, in one place:
//   Appearance     theme, interface size, conversation text sizes
//   Session        the live session's config options (model, reasoning,
//                  approvals… whatever Hermes advertises)
//   Hermes Agent   connection, version, executable, restart, updates,
//                  provider setup, doctor
//   Connection     which host runs Hermes: this machine, or a saved SSH host
//   Configuration  `hermes config show`, rendered; set a value; open the file
//   Shortcuts      every keyboard shortcut
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(AcpClient *client, FontSettings *fonts, QWidget *parent = nullptr);

    void showPage(const QString &id);   // "appearance", "session", "agent", "connection", "config", "shortcuts"
    QString currentPage() const;
    void setShortcuts(const QList<QPair<QString, QString>> &shortcuts);
    // Load `hermes config show` now instead of on first view (tests, screenshots).
    void loadConfiguration();
    bool configurationLoaded() const { return m_configLoaded; }

signals:
    void restartRequested();
    void executableChanged();
    void autoRestartChanged(bool enabled);
    // The remote (SSH) target changed; the next connection uses it.
    void connectionChanged();

private:
    QWidget *buildAppearance();
    QWidget *buildSession();
    QWidget *buildAgent();
    QWidget *buildConnection();
    QWidget *buildConfig();
    QWidget *buildShortcuts();
    QWidget *page(const QString &title, const QString &subtitle, QWidget **content, QVBoxLayout **layout);
    void rebuildSession();
    void refreshAgent();
    void testConnection();
    void runToOutput(const QString &title, const QStringList &args, int timeoutMs = 120000,
                     std::function<void(int)> after = {});
    void renderConfig(const QString &output);
    void openTerminal(const QStringList &args);

    AcpClient *m_client;
    FontSettings *m_fonts;
    QListWidget *m_nav = nullptr;
    QStackedWidget *m_pages = nullptr;
    QStringList m_pageIds;

    // Appearance
    QButtonGroup *m_themeGroup = nullptr;
    QSlider *m_scale = nullptr;
    QLabel *m_scaleLabel = nullptr;
    QSpinBox *m_chatPx = nullptr;
    QSpinBox *m_thinkingPx = nullptr;
    QSpinBox *m_toolPx = nullptr;

    // Session
    QVBoxLayout *m_sessionLayout = nullptr;
    QWidget *m_sessionContent = nullptr;

    // Connection (which host runs Hermes)
    QComboBox *m_remoteHosts = nullptr;
    QPushButton *m_remoteAdd = nullptr;
    QPushButton *m_remoteRemove = nullptr;
    QLineEdit *m_remoteDestination = nullptr;
    QLineEdit *m_remoteOptions = nullptr;
    QLineEdit *m_remoteLauncher = nullptr;
    QLabel *m_remoteResult = nullptr;
    ui::Spinner *m_remoteSpinner = nullptr;

    // Agent
    ui::StatusDot *m_agentDot = nullptr;
    QLabel *m_agentStatus = nullptr;
    QLabel *m_agentDetail = nullptr;
    QWidget *m_capsRow = nullptr;
    QLabel *m_resolved = nullptr;
    QLineEdit *m_override = nullptr;
    QCheckBox *m_autoRestart = nullptr;
    QLabel *m_authLabel = nullptr;
    QWidget *m_outputCard = nullptr;
    QLabel *m_outputTitle = nullptr;
    ui::Spinner *m_outputSpinner = nullptr;
    QPlainTextEdit *m_output = nullptr;
    QList<QPushButton *> m_cliButtons;

    // Config
    QVBoxLayout *m_configLayout = nullptr;
    QWidget *m_configSections = nullptr;
    QLabel *m_configState = nullptr;
    QLineEdit *m_setKey = nullptr;
    QLineEdit *m_setValue = nullptr;
    QLabel *m_setResult = nullptr;
    bool m_configLoaded = false;
    bool m_configLoading = false;

    // Shortcuts
    QVBoxLayout *m_shortcutsLayout = nullptr;
};
