#pragma once

#include <QHash>
#include <QLabel>
#include <QMainWindow>
#include <QMap>
#include <QPointer>
#include <QProgressBar>
#include <QTimer>
#include <QToolButton>
#include "acpclient.h"
#include "chatpane.h"
#include "commandcatalog.h"
#include "commandpalette.h"
#include "commandui.h"
#include "filepicker.h"
#include "fontsettings.h"
#include "sessionrail.h"

class QComboBox;
class QDialog;
class QMenu;
class QToolBar;
class SettingsDialog;
namespace ui { class StatusDot; }

// The main window.
//
//   ┌ menu bar: File · Session · Hermes · View · Help ─────────────────────┐
//   ├ toolbar: ☰ · New · model ▾ · reasoning ▾ · edits ▾ ……… Commands ⚙ ──┤
//   │ rail │ banner / conversation / empty state                          │
//   │      │ composer                                                      │
//   └ status: ● state ………………… model · context ▬▬ · 0:04 · tokens ───────────┘
//
// Every Hermes slash command is reachable from the menus (grouped), the
// command palette (Ctrl+K) and the composer ("/"). The list comes from
// Hermes itself (CommandCatalog), so commands a `hermes update` adds show up
// without a Consiglio release. Hermes setup and config live in Settings.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    struct Options {
        QString executable;      // "" = resolve (override setting, $HERMES_BIN, PATH)
        bool autoLaunch = true;  // connect to Hermes on construction
        bool centerOnScreen = true;
    };
    explicit MainWindow(QWidget *parent = nullptr);
    MainWindow(const Options &options, QWidget *parent = nullptr);
    ~MainWindow() override;

    // ── Introspection / automation (tests, screenshot tour) ─────────────
    AcpClient *client() { return &m_client; }
    CommandCatalog *catalog() { return &m_catalog; }
    ChatPane *chat() const { return m_chat; }
    SessionRail *rail() const { return m_rail; }
    CommandPalette *palette() const { return m_palette; }
    QToolBar *toolbar() const { return m_toolbar; }
    QMenu *hermesMenu() const { return m_hermesMenu; }
    QMenu *sessionMenu() const { return m_sessionMenu; }
    QList<QComboBox *> configCombos() const { return m_configCombos.values(); }
    QComboBox *configCombo(const QString &configId) const { return m_configCombos.value(configId); }
    QString stateText() const;
    QAction *action(const QString &id) const { return m_actions.value(id); }
    QStringList actionIds() const { return m_actions.keys(); }
    QList<QPair<QString, QString>> shortcutList() const;

    void connectToHermes();
    void startNewSession();
    // Send a slash command (or any prompt) as if typed. Starts a session
    // first when none is open.
    bool runCommand(const QString &line);
    // What a menu item does: run directly, or open the command's dialog.
    void triggerCommand(const QString &name, const QString &prefill = QString());
    SettingsDialog *openSettings(const QString &page = QString());
    // Build (but don't exec) dialogs — used by the screenshot tour.
    QDialog *createCommandDialog(const QString &name, const QString &prefill = QString());
    void refreshCommands();

protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onPromptSubmitted(const QString &text);
    void onSessionStarted(const QString &cwd);
    void onStopRequested();
    void onResumeRequested(const QString &sessionId, const QString &cwd);
    void onConnected();
    void onSessionsListed(QList<AcpClient::SessionInfo> sessions);
    void onReady();
    void onError(const QString &message);
    void onMessageChunk(const QString &text);
    void onThoughtChunk(const QString &text);
    void onToolCallStarted(const QString &id, const QString &title, const QString &kind);
    void onToolCallUpdated(const QString &id, const QString &status, const QString &output);
    void onContextUsage(int used, int size);
    void onSessionTitle(const QString &title);
    void onCommandsUpdated();
    void onTurnCompleted(const QString &stopReason, qint64 inputTokens, qint64 outputTokens);
    void onProcessExited(int code);
    void onTurnFailed(const QString &message);
    void onSessionFailed(const QString &message);
    void onPermissionRequested(const AcpClient::PermissionRequest &request);
    void onPermissionCancelled(const QString &key);
    void onAgentRestarting(int attempt, int delayMs);
    void onAgentRestartFailed(const QString &message);
    void onAgentVersionChanged(const QString &oldVersion, const QString &newVersion);
    void onSessionRestored(const QString &sessionId);
    void onQuietCommandFinished(const QString &command, const QString &output, bool ok);
    void onPaletteAction(const QString &id);
    void onTimerTick();
    // File picker: "/" at a non-first position opens it; a chosen path is
    // inserted back into the composer at the recorded cursor column.
    void onFilePathRequested(int column, bool relative);
    void onPathSelected(const QString &path);

private:
    enum class Conn { Connecting, Connected, Reconnecting, Failed };

    void init(const Options &options);
    void setupUI();
    void setupActions();
    void setupMenuBar();
    void setupToolbar();
    void setupStatusBar();
    void connectClient();
    QAction *makeAction(const QString &id, const QString &text, const QString &icon,
                        const QKeySequence &shortcut, const QString &tip);
    void startSession(const QString &cwd);
    void resumeSession(const QString &sessionId, const QString &cwd);
    void stopSession();
    void refreshSessionList();
    void setActive(bool active);
    void updateUiState();
    void updateEmptyState();
    void updateModelLabel();
    void updateActionStates();
    void rebuildCommandMenus();
    void rebuildConfigCombos();
    void rebuildPalette();
    void addCommandToMenu(QMenu *menu, const CommandCatalog::Command &cmd);
    void startDiscovery(bool force);
    void sendOrQueue(const QString &text, bool display);
    QString cwd() const;
    void beginTurnUi();
    void endTurnUi();
    void showToast(const QString &text, int ms = 4000);
    void restyle();
    void applyZoom(double delta);
    void updateResponsive();   // fit the chrome to the window width (no clipped toolbar)
    bool handleCommandAppLink(const CommandPlan &plan);
    void seedCommandHost(class CommandHostDialog *dialog);
    void applyCommandHost(class CommandHostDialog *dialog, const CommandApplyRequest &request);
    void finishCommandHostQuiet(const QString &command, const QString &output, bool ok);

    AcpClient m_client;
    CommandCatalog m_catalog;
    ChatPane *m_chat = nullptr;
    SessionRail *m_rail = nullptr;
    CommandPalette *m_palette = nullptr;
    FilePicker *m_filePicker = nullptr;
    FontSettings m_fonts;
    QPointer<SettingsDialog> m_settings;
    QString m_executable;

    // Chrome.
    QToolBar *m_toolbar = nullptr;
    QMenu *m_sessionMenu = nullptr;
    QMenu *m_hermesMenu = nullptr;
    QMenu *m_recentMenu = nullptr;
    QList<QAction *> m_dynamicSessionActions;
    QAction *m_configAnchor = nullptr;       // toolbar position for config combos
    QList<QAction *> m_configActions;        // toolbar widgets for combos
    QHash<QString, QComboBox *> m_configCombos;
    QHash<QString, QAction *> m_actions;     // by id
    bool m_railAutoHidden = false;           // rail hidden because the window is narrow

    // Status bar widgets.
    ui::StatusDot *m_stateDot = nullptr;
    QLabel *m_stateLabel = nullptr;
    QLabel *m_sandboxBadge = nullptr;
    QLabel *m_modelLabel = nullptr;
    QLabel *m_contextLabel = nullptr;
    QProgressBar *m_contextBar = nullptr;
    QLabel *m_timerLabel = nullptr;
    QLabel *m_tokensLabel = nullptr;

    QTimer *m_turnTimer = nullptr;
    qint64 m_turnStartMs = 0;
    bool m_busy = false;
    bool m_active = false;   // a session is currently live
    bool m_opening = false;  // session/new or resume in flight
    Conn m_conn = Conn::Connecting;
    QString m_connError;
    qint64 m_streamedChars = 0; // chars of answer text streamed this turn (for est. tok/s)
    QString m_cwd;
    QString m_sessionTitle;  // last title reported by the active session
    QString m_lastCommand;   // slash command of the turn in flight ("" = prompt)
    QString m_pendingPrompt; // sent once the session being opened is ready
    bool m_pendingDisplay = true;
    int m_sessionCount = 0;
    QString m_discoveredVersion;  // agent version the catalog was refreshed for this run
    int m_discoveryPages = 0;
    int m_filePickerColumn = 0; // cursor column where the "/" that opened the picker was typed
    bool m_filePickerRelative = false; // true when the user typed "./" (current directory)
    QMap<QString, QPointer<QDialog>> m_permissionDialogs; // open approval prompts by request key
    QPointer<CommandHostDialog> m_commandUi;
    QString m_pendingConfigId;
    QString m_pendingPersistLine;
    enum class CommandUiWait { None, Seed, Apply, Persist } m_commandUiWait = CommandUiWait::None;
};

