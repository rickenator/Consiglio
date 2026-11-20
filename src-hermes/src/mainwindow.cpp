#include "mainwindow.h"

#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QMessageBox>
#include <QDateTime>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>
#include <QDir>
#include <QPushButton>
#include "fontsettingsdialog.h"

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("Consiglio · Hermes");
    const QScreen *screen = QGuiApplication::screenAt(QPoint(0, 0));
    const QSize available = screen ? screen->availableGeometry().size() : QSize(1440, 900);
    setMinimumSize(640, 400);
    resize(qRound(available.width() * 0.86), qRound(available.height() * 0.86));

    setupUI();
    setupMenuBar();
    setupStatusBar();
    connectClient();

    // Connect to Hermes eagerly so the previous-sessions list is populated on
    // launch and starting a session doesn't pay the process-startup latency.
    // The list itself is fetched in onConnected() once initialize completes.
    m_stateLabel->setText(tr("Connecting…"));
    if (!m_client.launch()) {
        m_stateLabel->setText(tr("Error"));
    }

    // Center on screen after show.
    QTimer::singleShot(100, this, [this, screen]() {
        if (screen) {
            const QRect geo = screen->availableGeometry();
            move(geo.x() + (geo.width() - width()) / 2,
                 geo.y() + (geo.height() - height()) / 2);
        }
    });
}

MainWindow::~MainWindow() {
    m_client.shutdown();
}

void MainWindow::setupUI() {
    auto *central = new QWidget(this);
    auto *layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Left: session rail.
    m_rail = new SessionRail(this);
    m_rail->setFixedWidth(260);
    layout->addWidget(m_rail, 0);

    // Center: chat pane.
    m_chat = new ChatPane(this);
    layout->addWidget(m_chat, 1);

    setCentralWidget(central);

    // Apply persisted font sizes to the pane, and keep them in sync on change.
    m_chat->applyFontSizes(m_fonts.chatPx(), m_fonts.thinkingPx(), m_fonts.toolPx());
    connect(&m_fonts, &FontSettings::changed, this, [this]() {
        m_chat->applyFontSizes(m_fonts.chatPx(), m_fonts.thinkingPx(), m_fonts.toolPx());
    });

    // Command palette (Ctrl+K).
    m_palette = new CommandPalette(this);
    QList<PaletteAction> actions;
    actions.append({"new-session", tr("New Session"), "Ctrl+N"});
    actions.append({"clear", tr("Clear Conversation"), ""});
    actions.append({"about", tr("About Consiglio"), ""});
    m_palette->setActions(actions);
    connect(m_palette, &CommandPalette::actionSelected, this, &MainWindow::onPaletteAction);

    // File-system picker (opened by "/" at a non-first position in the composer).
    m_filePicker = new FilePicker(this);
    connect(m_filePicker, &FilePicker::pathSelected, this, &MainWindow::onPathSelected);

    // Turn timer.
    m_turnTimer = new QTimer(this);
    m_turnTimer->setInterval(500);
    connect(m_turnTimer, &QTimer::timeout, this, &MainWindow::onTimerTick);

    connect(m_rail, &SessionRail::startRequested, this, &MainWindow::onSessionStarted);
    connect(m_rail, &SessionRail::stopRequested, this, &MainWindow::onStopRequested);
    connect(m_rail, &SessionRail::resumeRequested, this, &MainWindow::onResumeRequested);
    // Changing the workspace directory re-roots new sessions and refreshes the list.
    connect(m_rail, &SessionRail::dirChanged, this, [this](const QString &cwd) {
        m_cwd = cwd;
        refreshSessionList();
    });
    connect(m_chat, &ChatPane::promptSubmitted, this, &MainWindow::onPromptSubmitted);
    // Typing "/" in the composer opens the command palette (same as Ctrl+K).
    connect(m_chat, &ChatPane::slashCommandRequested, this, [this]() {
        if (m_palette) m_palette->open();
    });
    // A "/" at a non-first position opens the file-system picker instead.
    connect(m_chat, &ChatPane::filePathRequested, this, &MainWindow::onFilePathRequested);
}

void MainWindow::setupMenuBar() {
    auto *menuBar = this->menuBar();

    auto *fileMenu = menuBar->addMenu("&File");
    auto *newSessionAction = new QAction(tr("&New Session"), fileMenu);
    newSessionAction->setShortcut(QKeySequence::New);
    connect(newSessionAction, &QAction::triggered, this, [this]() {
        // Trigger the rail's start with the current directory.
        onSessionStarted(m_cwd.isEmpty() ? QDir::homePath() : m_cwd);
    });
    fileMenu->addAction(newSessionAction);
    // After `hermes update` the running agent keeps the old code until it is
    // restarted; this relaunches it (re-resolved from PATH) and re-opens the
    // active session.
    auto *restartAction = new QAction(tr("&Restart Hermes Agent"), fileMenu);
    connect(restartAction, &QAction::triggered, this, [this]() {
        m_stateLabel->setText(tr("Restarting Hermes…"));
        m_client.restartAgent();
    });
    fileMenu->addAction(restartAction);
    fileMenu->addSeparator();
    auto *quitAction = new QAction(tr("E&xit"), fileMenu);
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, [this]() {
        m_client.shutdown();
        QApplication::quit();
    });
    fileMenu->addAction(quitAction);

    auto *viewMenu = menuBar->addMenu("&View");
    auto *paletteAction = new QAction(tr("Command Palette"), viewMenu);
    paletteAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_K));
    connect(paletteAction, &QAction::triggered, this, [this]() {
        if (m_palette) m_palette->open();
    });
    viewMenu->addAction(paletteAction);
    viewMenu->addSeparator();
    // Font sizes: one control per balloon type (chat / thinking / tool).
    auto *fontsAction = new QAction(tr("Font Sizes…"), viewMenu);
    connect(fontsAction, &QAction::triggered, this, [this]() {
        FontSettingsDialog dlg(&m_fonts, this);
        dlg.exec();
    });
    viewMenu->addAction(fontsAction);

    auto *helpMenu = menuBar->addMenu("&Help");
    auto *aboutAction = new QAction(tr("&About"), helpMenu);
    connect(aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, tr("About Consiglio"),
            tr("<h2>Consiglio · Hermes</h2>"
               "<p>A native C++ desktop client for the Hermes Agent, "
               "driven by the ACP protocol.</p>")
                .arg(QApplication::applicationVersion()));
    });
    helpMenu->addAction(aboutAction);
}

void MainWindow::setupStatusBar() {
    auto *statusBar = this->statusBar();

    // Left: state label.
    m_stateLabel = new QLabel("Ready", statusBar);
    statusBar->addWidget(m_stateLabel);

    // Right (permanent): model · context meter · timer · tokens.
    m_modelLabel = new QLabel(statusBar);
    m_modelLabel->setObjectName("statusModel");
    m_contextLabel = new QLabel(statusBar);
    m_contextLabel->setObjectName("statusContext");
    m_contextBar = new QProgressBar(statusBar);
    m_contextBar->setFixedSize(140, 31);
    m_contextBar->setRange(0, 100);
    m_contextBar->setValue(0);
    m_contextBar->setTextVisible(false);
    m_timerLabel = new QLabel(statusBar);
    m_timerLabel->setObjectName("statusTimer");
    // Always visible (like the Hermes CLI): shows 0:00 until a turn starts,
    // then ticks live and holds the final duration when the turn completes.
    m_timerLabel->setText("0:00");
    m_tokensLabel = new QLabel(statusBar);
    m_tokensLabel->setObjectName("statusTokens");

    statusBar->addPermanentWidget(m_modelLabel);
    statusBar->addPermanentWidget(m_contextLabel);
    statusBar->addPermanentWidget(m_contextBar);
    statusBar->addPermanentWidget(m_timerLabel);
    statusBar->addPermanentWidget(m_tokensLabel);
}

void MainWindow::connectClient() {
    connect(&m_client, &AcpClient::connected, this, &MainWindow::onConnected);
    connect(&m_client, &AcpClient::sessionsListed, this, &MainWindow::onSessionsListed);
    connect(&m_client, &AcpClient::ready, this, &MainWindow::onReady);
    connect(&m_client, &AcpClient::errorOccurred, this, &MainWindow::onError);
    connect(&m_client, &AcpClient::messageChunk, this, &MainWindow::onMessageChunk);
    connect(&m_client, &AcpClient::thoughtChunk, this, &MainWindow::onThoughtChunk);
    connect(&m_client, &AcpClient::toolCallStarted, this, &MainWindow::onToolCallStarted);
    connect(&m_client, &AcpClient::toolCallUpdated, this, &MainWindow::onToolCallUpdated);
    connect(&m_client, &AcpClient::contextUsage, this, &MainWindow::onContextUsage);
    connect(&m_client, &AcpClient::sessionTitle, this, &MainWindow::onSessionTitle);
    connect(&m_client, &AcpClient::commandsUpdated, this, &MainWindow::onCommandsUpdated);
    connect(&m_client, &AcpClient::turnCompleted, this, &MainWindow::onTurnCompleted);
    connect(&m_client, &AcpClient::processExited, this, &MainWindow::onProcessExited);
    connect(&m_client, &AcpClient::turnFailed, this, &MainWindow::onTurnFailed);
    connect(&m_client, &AcpClient::sessionFailed, this, &MainWindow::onSessionFailed);
    connect(&m_client, &AcpClient::permissionRequested, this, &MainWindow::onPermissionRequested);
    connect(&m_client, &AcpClient::permissionCancelled, this, &MainWindow::onPermissionCancelled);
    connect(&m_client, &AcpClient::agentRestarting, this, &MainWindow::onAgentRestarting);
    connect(&m_client, &AcpClient::agentRestartFailed, this, &MainWindow::onAgentRestartFailed);
    connect(&m_client, &AcpClient::agentVersionChanged, this, &MainWindow::onAgentVersionChanged);
    connect(&m_client, &AcpClient::sessionRestored, this, &MainWindow::onSessionRestored);
}

void MainWindow::startSession(const QString &cwd) {
    // Open a fresh session on the already-connected process.
    m_chat->clearConversation();
    m_stateLabel->setText(tr("Starting session…"));
    m_cwd = cwd;
    m_client.newSession(cwd);
}

void MainWindow::resumeSession(const QString &sessionId, const QString &cwd) {
    m_chat->clearConversation();
    m_stateLabel->setText(tr("Resuming session…"));
    m_cwd = cwd;
    m_client.resumeSession(sessionId, cwd);
}

void MainWindow::stopSession() {
    m_client.stopSession();
    setActive(false);
    m_stateLabel->setText(tr("Ready"));
    refreshSessionList();
}

void MainWindow::refreshSessionList() {
    // List previous sessions for the current workspace. When none is set, use
    // an empty cwd — the ACP server treats that as "list all sessions globally"
    // (a specific cwd only returns sessions under that exact directory).
    m_client.listSessions(m_cwd);
}

void MainWindow::setActive(bool active) {
    m_active = active;
    m_rail->setSessionActive(active, m_sessionTitle, cwd());
}

QString MainWindow::cwd() const { return m_cwd; }

void MainWindow::updateModelLabel() {
    const auto models = m_client.availableModels();
    if (models.isEmpty()) {
        m_modelLabel->setText(QString());
    } else {
        // Show the first (default) model; a full picker is a later iteration.
        m_modelLabel->setText(tr("model: %1").arg(models.first()));
    }
}

// ─── Slots ────────────────────────────────────────────────────────────────

void MainWindow::onPromptSubmitted(const QString &text) {
    if (!m_client.isReady()) {
        QMessageBox::warning(this, tr("No session"),
            tr("Start a session first (left rail → Start Session)."));
        return;
    }
    if (m_client.isBusy()) return;  // one turn at a time
    m_chat->appendUserMessage(text);
    if (!m_client.sendPrompt(text)) {
        m_stateLabel->setText(tr("Could not send the prompt; Hermes is not connected."));
        return;
    }
    beginTurnUi();
}

void MainWindow::beginTurnUi() {
    m_chat->setBusy(true);
    m_busy = true;
    m_streamedChars = 0;
    m_turnStartMs = QDateTime::currentMSecsSinceEpoch();
    m_timerLabel->setText("0:00");
    m_tokensLabel->clear();
    m_turnTimer->start();
}

void MainWindow::endTurnUi() {
    m_chat->finalizeTurn();
    m_chat->setBusy(false);
    m_busy = false;
    m_turnTimer->stop();
}

void MainWindow::onSessionStarted(const QString &cwd) {
    startSession(cwd);
}

void MainWindow::onStopRequested() {
    stopSession();
}

void MainWindow::onResumeRequested(const QString &sessionId, const QString &cwd) {
    resumeSession(sessionId, cwd);
}

void MainWindow::onConnected() {
    m_stateLabel->setText(tr("Ready"));
    // Now that the process is up, fetch the previous-sessions list.
    refreshSessionList();
}

void MainWindow::onSessionsListed(QList<AcpClient::SessionInfo> sessions) {
    m_rail->setSessions(sessions);
}

void MainWindow::onReady() {
    m_stateLabel->setText(tr("Session ready"));
    updateModelLabel();
    setActive(true);
    // Refresh the list so the just-opened session appears (and any stale
    // selection is cleared).
    refreshSessionList();
}

void MainWindow::onError(const QString &message) {
    // Connection-level errors (launch / initialize / list). Never leave the
    // UI stuck in "busy" because of one.
    if (m_busy && !m_client.isBusy()) endTurnUi();
    m_stateLabel->setText(message);
}

void MainWindow::onTurnFailed(const QString &message) {
    endTurnUi();
    m_chat->appendAgentChunk(tr("⚠ %1").arg(message));
    m_chat->finalizeTurn();
    m_stateLabel->setText(tr("Turn failed — ready for the next prompt"));
}

void MainWindow::onSessionFailed(const QString &message) {
    setActive(false);
    m_stateLabel->setText(message);
}

void MainWindow::onPermissionRequested(const AcpClient::PermissionRequest &request) {
    // Hermes asks before dangerous commands. Offer exactly the options it
    // sent (their ids are opaque and may change between Hermes versions);
    // the button role comes from the option kind, unknown kinds are neutral.
    auto *box = new QMessageBox(QMessageBox::Warning, tr("Hermes wants to run a command"),
                                request.title.isEmpty() ? tr("Hermes requests permission.") : request.title,
                                QMessageBox::NoButton, this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setTextFormat(Qt::PlainText);
    if (!request.detail.isEmpty() && request.detail != request.title) box->setDetailedText(request.detail);
    QPushButton *rejectButton = nullptr;
    for (const auto &opt : request.options) {
        QMessageBox::ButtonRole role = QMessageBox::ActionRole;
        if (opt.kind.startsWith("allow")) role = QMessageBox::AcceptRole;
        else if (opt.kind.startsWith("reject")) role = QMessageBox::RejectRole;
        QPushButton *b = box->addButton(opt.name.isEmpty() ? opt.optionId : opt.name, role);
        b->setProperty("acpOptionId", opt.optionId);
        if (role == QMessageBox::RejectRole && !rejectButton) rejectButton = b;
    }
    if (rejectButton) box->setDefaultButton(rejectButton);  // safe default
    const QString key = request.key;
    connect(box, &QMessageBox::buttonClicked, this, [this, key](QAbstractButton *b) {
        const QString optionId = b->property("acpOptionId").toString();
        if (optionId.isEmpty() || !m_client.respondPermission(key, optionId)) m_client.cancelPermission(key);
    });
    connect(box, &QDialog::rejected, this, [this, key]() {
        // Esc / window close without a choice = ACP "cancelled".
        m_client.cancelPermission(key);
    });
    connect(box, &QObject::destroyed, this, [this, key]() { m_permissionDialogs.remove(key); });
    m_permissionDialogs.insert(key, box);
    m_stateLabel->setText(tr("Waiting for your approval…"));
    box->open();
}

void MainWindow::onPermissionCancelled(const QString &key) {
    // The request went away (turn ended, session stopped, agent exited).
    if (auto box = m_permissionDialogs.take(key)) {
        box->disconnect(this);
        box->close();
    }
}

void MainWindow::onAgentRestarting(int attempt, int delayMs) {
    setActive(false);
    m_stateLabel->setText(tr("Hermes exited — reconnecting (attempt %1, %2 s)…")
                              .arg(attempt).arg(delayMs / 1000.0, 0, 'f', 1));
}

void MainWindow::onAgentRestartFailed(const QString &message) {
    if (m_busy) endTurnUi();
    m_stateLabel->setText(message);
}

void MainWindow::onAgentVersionChanged(const QString &oldVersion, const QString &newVersion) {
    statusBar()->showMessage(tr("Hermes updated: %1 → %2").arg(oldVersion, newVersion), 8000);
}

void MainWindow::onSessionRestored(const QString &sessionId) {
    Q_UNUSED(sessionId);
    m_stateLabel->setText(tr("Reconnected — session restored"));
}

void MainWindow::onMessageChunk(const QString &text) {
    m_chat->appendAgentChunk(text);
    // Track how much answer text has streamed so far — the basis for the live
    // estimated output rate (chars ≈ tokens/4). Reset at turn start.
    m_streamedChars += text.length();
}

void MainWindow::onThoughtChunk(const QString &text) {
    m_chat->appendThoughtChunk(text);
}

void MainWindow::onToolCallStarted(const QString &id, const QString &title, const QString &kind) {
    Q_UNUSED(kind);
    m_chat->addToolCall(id, title, kind);
}

void MainWindow::onToolCallUpdated(const QString &id, const QString &status, const QString &output) {
    m_chat->updateToolCall(id, status, output);
}

void MainWindow::onContextUsage(int used, int size) {
    if (size <= 0) return;
    const int pct = qBound(0, (used * 100) / size, 100);
    m_contextBar->setValue(pct);
    m_contextLabel->setText(tr("ctx: %1/%2 (%3%)")
                                .arg(QString::number(used / 1000.0, 'g', 4))
                                .arg(QString::number(size / 1000.0, 'g', 4))
                                .arg(pct));
}

void MainWindow::onSessionTitle(const QString &title) {
    m_sessionTitle = title;
    if (m_active) {
        m_rail->setSessionActive(true, title, cwd());
    }
}

void MainWindow::onCommandsUpdated() {
    // Merge the Hermes slash commands into the palette.
    QList<PaletteAction> actions;
    actions.append({"new-session", tr("New Session"), "Ctrl+N"});
    actions.append({"clear", tr("Clear Conversation"), ""});
    actions.append({"about", tr("About Consiglio"), ""});
    for (const auto &cmd : m_client.slashCommands()) {
        PaletteAction a;
        a.id = "/" + cmd.name;
        a.label = QString("/%1 — %2").arg(cmd.name, cmd.description);
        a.shortcut = "";
        actions.append(a);
    }
    m_palette->setActions(actions);
}

void MainWindow::onTurnCompleted(const QString &stopReason, qint64 inputTokens, qint64 outputTokens) {
    Q_UNUSED(stopReason);
    endTurnUi();
    // Exact in/out from ACP (replaces the live estimate). Show BOTH rates at turn
    // end: input tokens ÷ duration (how fast the model consumed the context) and
    // output tokens ÷ duration (generation speed). Each count carries its own rate.
    const qint64 elapsed = (QDateTime::currentMSecsSinceEpoch() - m_turnStartMs) / 1000;
    const double inRate = elapsed >= 1 ? static_cast<double>(inputTokens) / elapsed : 0.0;
    const double outRate = elapsed >= 1 ? static_cast<double>(outputTokens) / elapsed : 0.0;
    m_tokensLabel->setText(tr("in %1 (%2 tok/s) · out %3 (%4 tok/s)")
                                .arg(inputTokens)
                                .arg(inRate, 0, 'f', 1)
                                .arg(outputTokens)
                                .arg(outRate, 0, 'f', 1));
    m_stateLabel->setText(tr("Ready"));
}

void MainWindow::onProcessExited(int code) {
    Q_UNUSED(code);
    // The client has already failed any in-flight turn; make sure the UI
    // isn't left spinning, then wait for the automatic reconnect.
    if (m_busy) endTurnUi();
    setActive(false);
    m_stateLabel->setText(tr("Connection lost"));
}

void MainWindow::onPaletteAction(const QString &id) {
    if (id == "new-session") {
        onSessionStarted(m_cwd.isEmpty() ? QDir::homePath() : m_cwd);
    } else if (id == "clear") {
        m_chat->clearConversation();
    } else if (id == "about") {
        QMessageBox::about(this, tr("About Consiglio"),
            tr("<h2>Consiglio · Hermes</h2><p>A native C++ desktop client for the Hermes Agent.</p>"));
    } else if (id.startsWith("/")) {
        // Slash commands are sent to the agent as a prompt WITH the leading "/" —
        // the ACP agent dispatches on the slash (e.g. "/model", "/context").
        if (!m_client.isReady() || m_client.isBusy()) {
            m_stateLabel->setText(tr("Start a session first to run %1.").arg(id));
            return;
        }
        m_chat->appendUserMessage(id);
        if (!m_client.sendPrompt(id)) {
            m_stateLabel->setText(tr("Could not send %1.").arg(id));
            return;
        }
        beginTurnUi();
    }
}

void MainWindow::onTimerTick() {
    const qint64 elapsed = (QDateTime::currentMSecsSinceEpoch() - m_turnStartMs) / 1000;
    const int minutes = static_cast<int>(elapsed / 60);
    const int seconds = static_cast<int>(elapsed % 60);
    m_timerLabel->setText(QString("%1:%2")
                               .arg(minutes)
                               .arg(seconds, 2, 10, QLatin1Char('0')));
    // Live estimated output while the turn runs: ACP streams text deltas, not
    // per-chunk token counts, so estimate tokens from characters (~4 chars per
    // token). Show BOTH the running out-count and the rate — the count is an
    // approximation that gets reconciled with the exact ACP total at turn end.
    // Refreshes on this 500ms tick (a few times a second, matching the server's
    // own update cadence). Guarded against div-by-zero before 1s.
    if (m_busy && elapsed >= 1) {
        const double estTokens = m_streamedChars / 4.0;
        const double rate = estTokens / static_cast<double>(elapsed);
        m_tokensLabel->setText(tr("out ~%1 · ~%2 tok/s")
                                    .arg(static_cast<qint64>(estTokens))
                                    .arg(rate, 0, 'f', 1));
    }
}

void MainWindow::onFilePathRequested(int column, bool relative) {
    // Remember where the "/" was typed so the chosen path can be inserted there.
    m_filePickerColumn = column;
    m_filePickerRelative = relative;
    // "./" means "current directory": root the picker at the workspace cwd so the
    // user browses from here (and the chosen path is inserted as "./rel"). A bare
    // "/" roots at the filesystem root so the user can browse anywhere (/tmp, /usr).
    const QString root = relative ? (m_cwd.isEmpty() ? QDir::homePath() : m_cwd)
                                  : QDir::rootPath();
    if (m_filePicker) m_filePicker->open(root);
}

// Compute `path` relative to `base` (both absolute). Qt5 has no QDir::relativePath,
// so walk the components: drop the shared prefix, then ".." for each base segment
// left over and the target segments for the rest. Returns "" if not possible.
static QString relativeToCwd(const QString &base, const QString &path) {
    auto split = [](const QString &p) {
        QStringList parts;
        for (const QString &seg : p.split('/')) if (!seg.isEmpty()) parts << seg;
        return parts;
    };
    const QStringList b = split(QDir::cleanPath(base));
    const QStringList t = split(QDir::cleanPath(path));
    int common = 0;
    while (common < b.size() && common < t.size() && b[common] == t[common]) ++common;
    QStringList up;
    for (int i = 0; i < b.size() - common; ++i) up << QStringLiteral("..");
    const QStringList down = t.mid(common);
    QStringList result = up;
    result.append(down);
    return result.join('/');
}

void MainWindow::onPathSelected(const QString &path) {
    // Insert the chosen path into the composer at the recorded column. For "./"
    // the user wants a relative reference, so convert the absolute path to
    // "./rel" (relative to cwd) — matching the CLI's "./" completion.
    QString insert = path;
    if (m_filePickerRelative && !m_cwd.isEmpty()) {
        const QString rel = relativeToCwd(m_cwd, path);
        if (!rel.isEmpty() && !rel.startsWith("..")) insert = "./" + rel;
    }
    m_chat->insertPathAt(m_filePickerColumn, insert, m_filePickerRelative);
}
