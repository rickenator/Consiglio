#pragma once

#include <QMainWindow>
#include <QLabel>
#include <QProgressBar>
#include <QTimer>
#include <QMap>
#include <QPointer>
#include <QMessageBox>
#include "acpclient.h"
#include "chatpane.h"
#include "commandpalette.h"
#include "filepicker.h"
#include "fontsettings.h"
#include "sessionrail.h"

// The chat-first main window. Center = ChatPane (streaming conversation).
// Left = SessionRail (start/switch sessions). Bottom = a rich status bar that
// shows what you complained was missing: a context meter, the active model,
// a live turn timer, and token usage. Ctrl+K opens the slash-command palette.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

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
    void onPaletteAction(const QString &id);
    void onTimerTick();
    // File picker: "/" at a non-first position opens it; a chosen path is
    // inserted back into the composer at the recorded cursor column.
    void onFilePathRequested(int column, bool relative);
    void onPathSelected(const QString &path);

private:
    void setupUI();
    void setupStatusBar();
    void setupMenuBar();
    void connectClient();
    void startSession(const QString &cwd);
    void resumeSession(const QString &sessionId, const QString &cwd);
    void stopSession();
    void refreshSessionList();
    void setActive(bool active);
    void updateModelLabel();
    QString cwd() const;
    void beginTurnUi();
    void endTurnUi();

    AcpClient m_client;
    ChatPane *m_chat = nullptr;
    SessionRail *m_rail = nullptr;
    CommandPalette *m_palette = nullptr;
    FilePicker *m_filePicker = nullptr;
    FontSettings m_fonts;

    // Status bar widgets.
    QLabel *m_modelLabel = nullptr;
    QLabel *m_contextLabel = nullptr;
    QProgressBar *m_contextBar = nullptr;
    QLabel *m_timerLabel = nullptr;
    QLabel *m_tokensLabel = nullptr;
    QLabel *m_stateLabel = nullptr;

    QTimer *m_turnTimer = nullptr;
    qint64 m_turnStartMs = 0;
    bool m_busy = false;
    bool m_active = false;   // a session is currently live
    qint64 m_streamedChars = 0; // chars of answer text streamed this turn (for est. tok/s)
    QString m_cwd;
    QString m_sessionTitle;  // last title reported by the active session
    int m_filePickerColumn = 0; // cursor column where the "/" that opened the picker was typed
    bool m_filePickerRelative = false; // true when the user typed "./" (current directory)
    QMap<QString, QPointer<QMessageBox>> m_permissionDialogs; // open approval prompts by request key
};
