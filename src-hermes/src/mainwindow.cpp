#include "mainwindow.h"

#include "commanddialog.h"
#include "hermescli.h"
#include "settingsdialog.h"
#include "theme.h"
#include "uiutil.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QScopedPointer>
#include <QStyle>
#include <functional>
#include <QDir>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>

namespace {

const char *kDocsUrl = "https://hermes-agent.nousresearch.com/docs/";
const char *kInstallUrl = "https://hermes-agent.nousresearch.com/docs/getting-started/installation";
constexpr int kMaxDiscoveryPages = 30;

Theme &theme() { return Theme::instance(); }

// A short, human form of a token count: 950, 12.4k, 1.2M.
QString shortCount(qint64 n) {
    if (n < 1000) return QString::number(n);
    if (n < 1000000) return QString::number(n / 1000.0, 'f', n < 10000 ? 1 : 0) + QLatin1Char('k');
    return QString::number(n / 1000000.0, 'f', 1) + QLatin1Char('M');
}

// Path `path` relative to `base` (both absolute). Qt5 lacks QDir::relativePath
// for this use, so walk the components.
QString relativeToCwd(const QString &base, const QString &path) {
    auto split = [](const QString &p) {
        QStringList parts;
        for (const QString &seg : p.split('/')) if (!seg.isEmpty()) parts << seg;
        return parts;
    };
    const QStringList b = split(QDir::cleanPath(base));
    const QStringList t = split(QDir::cleanPath(path));
    int common = 0;
    while (common < b.size() && common < t.size() && b[common] == t[common]) ++common;
    QStringList result;
    for (int i = 0; i < b.size() - common; ++i) result << QStringLiteral("..");
    result.append(t.mid(common));
    return result.join('/');
}

bool isGoalControl(const QString &line) {
    return line == QLatin1String("/goal") || line.startsWith(QLatin1String("/goal "))
        || line.startsWith(QLatin1String("/subgoal"));
}

} // namespace

// ─── Permission dialog ────────────────────────────────────────────────────
// Hermes asks before dangerous commands. Offers exactly the options it sent
// (their ids are opaque and may change between versions); the look comes from
// the option kind and unknown kinds are neutral. Reject is the safe default.
class PermissionDialog : public QDialog {
public:
    PermissionDialog(const AcpClient::PermissionRequest &request, QWidget *parent)
        : QDialog(parent) {
        setObjectName(QStringLiteral("permissionDialog"));
        setWindowTitle(tr("Approval Needed"));
        setModal(true);
        Theme &t = theme();
        auto *outer = new QVBoxLayout(this);
        outer->setContentsMargins(t.px(22), t.px(20), t.px(22), t.px(18));
        outer->setSpacing(t.px(12));

        auto *head = new QHBoxLayout;
        head->setSpacing(t.px(12));
        auto *icon = new QLabel(this);
        icon->setPixmap(t.pixmap(QStringLiteral("shield"), t.px(28), t.tokens().warning));
        icon->setAlignment(Qt::AlignTop);
        head->addWidget(icon, 0, Qt::AlignTop);
        auto *titles = new QVBoxLayout;
        titles->setSpacing(t.px(4));
        auto *title = new QLabel(tr("Hermes wants your approval"), this);
        title->setProperty("role", "heading");
        auto *sub = new QLabel(tr("Review what it is about to do. Nothing runs until you choose."), this);
        sub->setProperty("role", "muted");
        sub->setWordWrap(true);
        titles->addWidget(title);
        titles->addWidget(sub);
        head->addLayout(titles, 1);
        outer->addLayout(head);

        const QString what = request.title.isEmpty() ? tr("Hermes requests permission.") : request.title;
        auto *card = new QLabel(ui::maskSecrets(what), this);
        card->setObjectName(QStringLiteral("permissionCommand"));
        card->setProperty("role", "mono");
        card->setWordWrap(true);
        card->setTextInteractionFlags(Qt::TextSelectableByMouse);
        card->setTextFormat(Qt::PlainText);
        card->setStyleSheet(QStringLiteral("QLabel { background: %1; border: 1px solid %2; border-radius: %3px; padding: %4px; }")
                                .arg(t.tokens().code.name(QColor::HexArgb), t.tokens().border.name(QColor::HexArgb))
                                .arg(t.px(8)).arg(t.px(10)));
        ui::describe(card, tr("Requested action"), what);
        outer->addWidget(card);
        if (!request.detail.isEmpty() && request.detail != request.title) {
            auto *detail = new QLabel(ui::maskSecrets(request.detail.left(2000)), this);
            detail->setProperty("role", "muted");
            detail->setWordWrap(true);
            detail->setTextFormat(Qt::PlainText);
            detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
            outer->addWidget(detail);
        }

        auto *row = new QHBoxLayout;
        row->setSpacing(t.px(8));
        QList<QPushButton *> rejects, others;
        QPushButton *firstReject = nullptr;
        for (const auto &opt : request.options) {
            auto *b = new QPushButton(opt.name.isEmpty() ? opt.optionId : opt.name, this);
            b->setProperty("acpOptionId", opt.optionId);
            b->setCursor(Qt::PointingHandCursor);
            b->setAutoDefault(false);
            if (opt.kind.startsWith(QLatin1String("reject"))) {
                b->setProperty("role", "danger");
                rejects << b;
                if (!firstReject) firstReject = b;
                ui::describe(b, b->text(), tr("Don't allow this (%1)").arg(opt.kind));
            } else {
                if (opt.kind == QLatin1String("allow_once")) b->setProperty("role", "primary");
                others << b;
                ui::describe(b, b->text(), opt.kind.isEmpty() ? b->text() : tr("%1 (%2)").arg(b->text(), opt.kind));
            }
            connect(b, &QPushButton::clicked, this, [this, b]() {
                m_choice = b->property("acpOptionId").toString();
                accept();
            });
        }
        for (auto *b : rejects) row->addWidget(b);
        row->addStretch(1);
        for (auto *b : others) row->addWidget(b);
        outer->addLayout(row);
        if (firstReject) { firstReject->setDefault(true); firstReject->setFocus(); }
        setMinimumWidth(t.px(460));
    }
    QString choice() const { return m_choice; }

private:
    QString m_choice;
};

// ─── Construction ─────────────────────────────────────────────────────────

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) { init(Options()); }

MainWindow::MainWindow(const Options &options, QWidget *parent) : QMainWindow(parent) { init(options); }

MainWindow::~MainWindow() { m_client.shutdown(); }

void MainWindow::init(const Options &options) {
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(tr("Consiglio · Hermes"));
    setWindowIcon(QIcon(QStringLiteral(":/logo.svg")));
    m_executable = options.executable;

    const QScreen *screen = QGuiApplication::primaryScreen();
    const QSize available = screen ? screen->availableGeometry().size() : QSize(1440, 900);
    setMinimumSize(theme().px(720), theme().px(480));
    resize(qMin(qRound(available.width() * 0.82), theme().px(1500)),
           qMin(qRound(available.height() * 0.86), theme().px(980)));

    QSettings s;
    m_cwd = s.value(QStringLiteral("session/workspace")).toString();
    if (!m_cwd.isEmpty() && !QDir(m_cwd).exists()) m_cwd.clear();
    m_client.setAutoRestart(s.value(QStringLiteral("hermes/autoRestart"), true).toBool());

    setupUI();
    setupActions();
    setupMenuBar();
    setupToolbar();
    setupStatusBar();
    connectClient();

    // A catalog cached from the last run fills the menus instantly; it is
    // refreshed from the live agent once a session opens.
    m_catalog.loadCache();
    connect(&m_catalog, &CommandCatalog::changed, this, [this]() {
        rebuildCommandMenus();
        rebuildPalette();
    });
    rebuildCommandMenus();
    rebuildPalette();
    connect(&theme(), &Theme::changed, this, &MainWindow::restyle);

    m_conn = Conn::Connecting;
    updateUiState();
    if (options.autoLaunch) connectToHermes();
    else { m_conn = Conn::Failed; m_connError = tr("Not connected."); updateUiState(); }

    if (options.centerOnScreen && screen) {
        const QRect geo = screen->availableGeometry();
        move(geo.x() + (geo.width() - width()) / 2, geo.y() + (geo.height() - height()) / 2);
    }
}

void MainWindow::connectToHermes() {
    m_conn = Conn::Connecting;
    m_connError.clear();
    updateUiState();
    const QString exe = !m_executable.isEmpty() ? m_executable : HermesCli::executableOverride();
    if (!m_client.launch(exe)) {
        m_conn = Conn::Failed;
        if (m_connError.isEmpty()) m_connError = tr("Couldn't start Hermes.");
        updateUiState();
    }
}

void MainWindow::setupUI() {
    Theme &t = theme();
    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("appCentral"));
    auto *layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_rail = new SessionRail(central);
    m_rail->setFixedWidth(t.px(268));
    m_rail->setWorkspace(m_cwd.isEmpty() ? QDir::homePath() : m_cwd);
    m_rail->setVisible(QSettings().value(QStringLiteral("view/sidebar"), true).toBool());
    layout->addWidget(m_rail, 0);

    m_chat = new ChatPane(central);
    layout->addWidget(m_chat, 1);
    setCentralWidget(central);

    m_chat->applyFontSizes(m_fonts.chatPx(), m_fonts.thinkingPx(), m_fonts.toolPx());
    connect(&m_fonts, &FontSettings::changed, this, [this]() {
        m_chat->applyFontSizes(m_fonts.chatPx(), m_fonts.thinkingPx(), m_fonts.toolPx());
    });

    m_palette = new CommandPalette(this);
    connect(m_palette, &CommandPalette::actionSelected, this, &MainWindow::onPaletteAction);

    m_filePicker = new FilePicker(this);
    connect(m_filePicker, &FilePicker::pathSelected, this, &MainWindow::onPathSelected);

    m_turnTimer = new QTimer(this);
    m_turnTimer->setInterval(500);
    connect(m_turnTimer, &QTimer::timeout, this, &MainWindow::onTimerTick);

    connect(m_rail, &SessionRail::startRequested, this, &MainWindow::onSessionStarted);
    connect(m_rail, &SessionRail::stopRequested, this, &MainWindow::onStopRequested);
    connect(m_rail, &SessionRail::resumeRequested, this, &MainWindow::onResumeRequested);
    connect(m_rail, &SessionRail::dirChanged, this, [this](const QString &dir) {
        m_cwd = dir;
        QSettings().setValue(QStringLiteral("session/workspace"), dir);
        refreshSessionList();
        updateEmptyState();
    });
    connect(m_chat, &ChatPane::promptSubmitted, this, &MainWindow::onPromptSubmitted);
    connect(m_chat, &ChatPane::slashCommandRequested, this, [this]() { m_palette->open(); });
    connect(m_chat, &ChatPane::commandsButtonClicked, this, [this]() { m_palette->open(); });
    connect(m_chat, &ChatPane::filePathRequested, this, &MainWindow::onFilePathRequested);
    connect(m_chat, &ChatPane::stopRequested, this, [this]() {
        if (m_busy) m_client.cancelTurn();
    });
    connect(m_chat, &ChatPane::quickActionTriggered, this, [this](const QString &id) {
        if (id.startsWith(QLatin1Char('/'))) triggerCommand(id.mid(1));
        else onPaletteAction(id);
    });
    connect(m_chat, &ChatPane::bannerActionTriggered, this, [this](const QString &id) { onPaletteAction(id); });
}

// ─── Actions, menus, toolbar, status bar ──────────────────────────────────

QAction *MainWindow::makeAction(const QString &id, const QString &text, const QString &icon,
                                const QKeySequence &shortcut, const QString &tip) {
    auto *a = new QAction(text, this);
    a->setObjectName(id);
    if (!icon.isEmpty()) {
        a->setIcon(theme().icon(icon));
        a->setProperty("themeIcon", icon);
    }
    if (!shortcut.isEmpty()) {
        a->setShortcut(shortcut);
        a->setShortcutContext(Qt::WindowShortcut);
    }
    a->setShortcutVisibleInContextMenu(true);
    ui::describe(a, tip);
    m_actions.insert(id, a);
    addAction(a);   // shortcuts work even when the menu bar is hidden
    connect(a, &QAction::triggered, this, [this, id]() { onPaletteAction(id); });
    return a;
}

void MainWindow::setupActions() {
    makeAction("app:new-session", tr("&New Session"), "message-square-plus", QKeySequence::New,
               tr("Start a new Hermes session in the workspace folder"));
    makeAction("app:end-session", tr("&End Session"), "circle-x", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_W),
               tr("End the current session (it stays in Recent Sessions)"));
    makeAction("app:stop", tr("&Stop Response"), "square", QKeySequence(Qt::CTRL | Qt::Key_Period),
               tr("Stop the response Hermes is writing"));
    makeAction("app:settings", tr("&Settings…"), "settings", QKeySequence(Qt::CTRL | Qt::Key_Comma),
               tr("Appearance, session options, Hermes setup and configuration"));
    makeAction("app:quit", tr("&Quit"), "power", QKeySequence::Quit, tr("Quit Consiglio"));
    makeAction("app:workspace", tr("Choose &Workspace Folder…"), "folder-open", QKeySequence(Qt::CTRL | Qt::Key_O),
               tr("Pick the folder new sessions work in"));

    makeAction("app:goal", tr("Set &Goal…"), "target", QKeySequence(Qt::CTRL | Qt::Key_G),
               tr("Give Hermes a standing goal to work toward across turns"));
    makeAction("app:goal-status", tr("Goal &Status"), "activity", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G),
               tr("Ask Hermes for the current goal and its progress"));
    makeAction("app:goal-show", tr("Show Goal &Details"), "eye", QKeySequence(),
               tr("Show the goal, its contract and turn budget"));
    makeAction("app:goal-pause", tr("&Pause Goal"), "circle-pause", QKeySequence(),
               tr("Pause automatic work on the goal"));
    makeAction("app:goal-resume", tr("&Resume Goal"), "circle-play", QKeySequence(),
               tr("Resume automatic work on the goal"));
    makeAction("app:goal-clear", tr("&Clear Goal"), "eraser", QKeySequence(),
               tr("Clear the standing goal"));

    makeAction("app:palette", tr("&Command Palette…"), "command", QKeySequence(Qt::CTRL | Qt::Key_K),
               tr("Search every Hermes command and app action"));
    makeAction("app:refresh-commands", tr("Refresh Command &List"), "refresh-cw", QKeySequence(),
               tr("Re-read the command list from Hermes (after an update or new skills)"));
    makeAction("app:configure-provider", tr("Configure &Provider…"), "log-in", QKeySequence(),
               tr("Run Hermes's provider setup in a terminal (API keys and logins stay in Hermes)"));
    makeAction("app:configuration", tr("Hermes &Configuration…"), "sliders-horizontal", QKeySequence(),
               tr("View and change Hermes's configuration"));
    makeAction("app:check-updates", tr("Check for &Updates…"), "download", QKeySequence(),
               tr("Check whether a newer Hermes is available"));
    makeAction("app:restart", tr("&Restart Agent"), "rotate-ccw", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R),
               tr("Relaunch Hermes (picks up an update) and reopen the session"));

    auto *sidebar = makeAction("app:sidebar", tr("&Sidebar"), "panel-left", QKeySequence(Qt::CTRL | Qt::Key_B),
                               tr("Show or hide the sessions sidebar"));
    sidebar->setCheckable(true);
    sidebar->setChecked(m_rail->isVisible());
    makeAction("app:focus-composer", tr("&Focus Message Box"), "text-cursor-input", QKeySequence(Qt::CTRL | Qt::Key_L),
               tr("Move the cursor to the message box"));
    makeAction("app:zoom-in", tr("Zoom &In"), "", QKeySequence::ZoomIn, tr("Make the interface larger"));
    makeAction("app:zoom-out", tr("Zoom &Out"), "", QKeySequence::ZoomOut, tr("Make the interface smaller"));
    makeAction("app:zoom-reset", tr("&Actual Size"), "", QKeySequence(Qt::CTRL | Qt::Key_0), tr("Reset the interface size"));
    auto *themeGroup = new QActionGroup(this);
    const struct { const char *id; const char *text; const char *icon; Theme::Mode mode; } modes[] = {
        {"app:theme-system", QT_TR_NOOP("Match &System"), "monitor", Theme::Mode::System},
        {"app:theme-light", QT_TR_NOOP("&Light"), "sun", Theme::Mode::Light},
        {"app:theme-dark", QT_TR_NOOP("&Dark"), "moon", Theme::Mode::Dark},
    };
    for (const auto &m : modes) {
        auto *a = makeAction(m.id, tr(m.text), m.icon, QKeySequence(), tr("Use the %1 theme").arg(tr(m.text).remove('&').toLower()));
        a->setCheckable(true);
        a->setChecked(theme().mode() == m.mode);
        themeGroup->addAction(a);
    }

    makeAction("app:help", tr("Hermes &Help"), "circle-help", QKeySequence(Qt::Key_F1),
               tr("Ask Hermes for its built-in help (/help)"));
    makeAction("app:docs", tr("Hermes &Documentation"), "book-open", QKeySequence(),
               tr("Open the Hermes documentation in your browser"));
    makeAction("app:shortcuts", tr("&Keyboard Shortcuts"), "keyboard", QKeySequence(Qt::CTRL | Qt::Key_Slash),
               tr("Every keyboard shortcut"));
    makeAction("app:about", tr("&About Consiglio"), "info", QKeySequence(), tr("Version and credits"));

    // Second palette shortcut (VS Code habit).
    auto *alt = new QAction(this);
    alt->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P));
    connect(alt, &QAction::triggered, this, [this]() { m_palette->open(); });
    addAction(alt);
}

void MainWindow::setupMenuBar() {
    QMenuBar *bar = menuBar();
    auto a = [this](const char *id) { return m_actions.value(QString::fromLatin1(id)); };

    QMenu *file = bar->addMenu(tr("&File"));
    file->addAction(a("app:new-session"));
    m_recentMenu = file->addMenu(theme().icon("history"), tr("&Recent Sessions"));
    m_recentMenu->setProperty("themeIcon", "history");
    m_recentMenu->setToolTipsVisible(true);
    file->addAction(a("app:workspace"));
    file->addAction(a("app:end-session"));
    file->addSeparator();
    file->addAction(a("app:settings"));
    file->addSeparator();
    file->addAction(a("app:quit"));

    m_sessionMenu = bar->addMenu(tr("&Session"));
    m_sessionMenu->setToolTipsVisible(true);
    m_sessionMenu->addAction(a("app:new-session"));
    m_sessionMenu->addAction(a("app:stop"));
    m_sessionMenu->addAction(a("app:end-session"));

    m_goalMenu = bar->addMenu(tr("&Goal"));
    m_goalMenu->setToolTipsVisible(true);
    m_goalMenu->addAction(a("app:goal"));
    m_goalMenu->addSeparator();
    m_goalMenu->addAction(a("app:goal-status"));
    m_goalMenu->addAction(a("app:goal-show"));
    m_goalMenu->addAction(a("app:goal-pause"));
    m_goalMenu->addAction(a("app:goal-resume"));
    m_goalMenu->addAction(a("app:goal-clear"));

    m_hermesMenu = bar->addMenu(tr("&Hermes"));
    m_hermesMenu->setToolTipsVisible(true);

    QMenu *view = bar->addMenu(tr("&View"));
    view->addAction(a("app:palette"));
    view->addAction(a("app:sidebar"));
    view->addAction(a("app:focus-composer"));
    view->addSeparator();
    QMenu *themeMenu = view->addMenu(theme().icon("palette"), tr("&Theme"));
    themeMenu->setProperty("themeIcon", "palette");
    themeMenu->addAction(a("app:theme-system"));
    themeMenu->addAction(a("app:theme-light"));
    themeMenu->addAction(a("app:theme-dark"));
    view->addAction(a("app:zoom-in"));
    view->addAction(a("app:zoom-out"));
    view->addAction(a("app:zoom-reset"));

    QMenu *help = bar->addMenu(tr("H&elp"));
    help->addAction(a("app:help"));
    help->addAction(a("app:docs"));
    help->addAction(a("app:shortcuts"));
    help->addSeparator();
    help->addAction(a("app:about"));
    for (QMenu *m : {file, view, help, themeMenu}) m->setToolTipsVisible(true);
}

void MainWindow::setupToolbar() {
    Theme &t = theme();
    m_toolbar = new QToolBar(tr("Main Toolbar"), this);
    m_toolbar->setObjectName(QStringLiteral("mainToolbar"));
    m_toolbar->setMovable(false);
    m_toolbar->setFloatable(false);
    m_toolbar->setContextMenuPolicy(Qt::PreventContextMenu);
    m_toolbar->setIconSize(QSize(t.px(16), t.px(16)));
    m_toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    addToolBar(Qt::TopToolBarArea, m_toolbar);

    m_toolbar->addAction(m_actions.value("app:sidebar"));
    m_toolbar->addAction(m_actions.value("app:new-session"));
    if (auto *b = qobject_cast<QToolButton *>(m_toolbar->widgetForAction(m_actions.value("app:new-session")))) {
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setText(tr("New"));
    }
    m_toolbar->addSeparator();

    m_goalButton = new QToolButton(m_toolbar);
    m_goalButton->setObjectName(QStringLiteral("goalButton"));
    m_goalButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_goalButton->setPopupMode(QToolButton::MenuButtonPopup);
    m_goalButton->setMenu(m_goalMenu);
    m_goalButton->setCursor(Qt::PointingHandCursor);
    m_goalButton->setIconSize(QSize(t.px(16), t.px(16)));
    m_goalButton->setMaximumWidth(t.px(320));
    connect(m_goalButton, &QToolButton::clicked, this, &MainWindow::openGoalDialog);
    m_toolbar->addWidget(m_goalButton);
    m_toolbar->addSeparator();

    // Config option pickers are inserted before this anchor.
    auto *spacer = new QWidget(m_toolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_configAnchor = m_toolbar->addWidget(spacer);

    m_toolbar->addAction(m_actions.value("app:palette"));
    if (auto *b = qobject_cast<QToolButton *>(m_toolbar->widgetForAction(m_actions.value("app:palette")))) {
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setText(tr("Commands"));
    }
    m_toolbar->addAction(m_actions.value("app:settings"));
    updateGoalButton();
}

void MainWindow::setupStatusBar() {
    QStatusBar *bar = statusBar();
    bar->setSizeGripEnabled(false);
    m_stateDot = new ui::StatusDot(bar);
    m_stateLabel = new QLabel(bar);
    m_stateLabel->setObjectName(QStringLiteral("statusState"));
    auto *left = new QWidget(bar);
    auto *ll = new QHBoxLayout(left);
    ll->setContentsMargins(theme().px(6), 0, 0, 0);
    ll->setSpacing(theme().px(2));
    ll->addWidget(m_stateDot);
    ll->addWidget(m_stateLabel);
    bar->addWidget(left, 1);

    m_modelLabel = new QLabel(bar);
    m_modelLabel->setObjectName(QStringLiteral("statusModel"));
    ui::describe(m_modelLabel, tr("Model"));
    m_contextLabel = new QLabel(bar);
    m_contextLabel->setObjectName(QStringLiteral("statusContext"));
    m_contextBar = new QProgressBar(bar);
    m_contextBar->setObjectName(QStringLiteral("contextBar"));
    m_contextBar->setFixedWidth(theme().px(72));
    m_contextBar->setRange(0, 100);
    m_contextBar->setTextVisible(false);
    ui::describe(m_contextBar, tr("Context window used"));
    m_timerLabel = new QLabel(QStringLiteral("0:00"), bar);
    m_timerLabel->setObjectName(QStringLiteral("statusTimer"));
    ui::describe(m_timerLabel, tr("Duration of the last response"));
    m_tokensLabel = new QLabel(bar);
    m_tokensLabel->setObjectName(QStringLiteral("statusTokens"));
    ui::describe(m_tokensLabel, tr("Tokens in ↑ and out ↓ for the last response"));
    bar->addPermanentWidget(m_modelLabel);
    bar->addPermanentWidget(m_contextLabel);
    bar->addPermanentWidget(m_contextBar);
    bar->addPermanentWidget(m_timerLabel);
    bar->addPermanentWidget(m_tokensLabel);
    m_contextLabel->hide();
    m_contextBar->hide();
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
    connect(&m_client, &AcpClient::quietCommandFinished, this, &MainWindow::onQuietCommandFinished);
    connect(&m_client, &AcpClient::configOptionsChanged, this, &MainWindow::rebuildConfigCombos);
    connect(&m_client, &AcpClient::modesChanged, this, &MainWindow::rebuildConfigCombos);
    connect(&m_client, &AcpClient::configChangeFailed, this, [this](const QString &id, const QString &message) {
        rebuildConfigCombos();
        showToast(tr("Couldn't change %1: %2").arg(id, message), 6000);
    });
    connect(&m_client, &AcpClient::sessionStopped, this, [this]() { setActive(false); updateUiState(); });
}

// ─── Dynamic command menus ────────────────────────────────────────────────

void MainWindow::addCommandToMenu(QMenu *menu, const CommandCatalog::Command &cmd) {
    const QString slash = QLatin1Char('/') + cmd.name;
    const QString syntax = cmd.argsHint.isEmpty() ? slash : slash + QLatin1Char(' ') + cmd.argsHint;
    QString tip = syntax;
    if (!cmd.description.isEmpty()) tip += QStringLiteral(" — ") + cmd.description;
    if (!cmd.aliases.isEmpty())
        tip += QStringLiteral("  (") + tr("also") + QStringLiteral(" /") + cmd.aliases.join(QStringLiteral(", /")) + QLatin1Char(')');
    const QIcon icon = theme().icon(cmd.icon);
    const QString name = cmd.name;

    auto finish = [&](QAction *a) {
        a->setProperty("catalogCommand", name);
        a->setProperty("themeIcon", cmd.icon);
        a->setToolTip(tip);
        a->setStatusTip(tip);
        a->setIcon(icon);
    };

    if (name == QLatin1String("goal")) {
        QAction *a = menu->addAction(tr("Goal Contract & Options…"));
        finish(a);
        connect(a, &QAction::triggered, this, &MainWindow::openGoalDialog);
        return;
    }
    const QStringList choices = cmd.choices();
    if (!choices.isEmpty() && !cmd.requiresArgs()) {
        QMenu *sub = menu->addMenu(icon, cmd.label);
        sub->setToolTipsVisible(true);
        finish(sub->menuAction());
        QAction *run = sub->addAction(tr("Run %1").arg(slash));
        run->setToolTip(tip);
        connect(run, &QAction::triggered, this, [this, slash]() { runCommand(slash); });
        sub->addSeparator();
        for (const QString &choice : choices) {
            QAction *c = sub->addAction(choice);
            c->setToolTip(QStringLiteral("%1 %2").arg(slash, choice));
            connect(c, &QAction::triggered, this, [this, slash, choice]() { runCommand(slash + QLatin1Char(' ') + choice); });
        }
        sub->addSeparator();
        QAction *more = sub->addAction(tr("More Options…"));
        more->setToolTip(tr("Type arguments for %1").arg(slash));
        connect(more, &QAction::triggered, this, [this, name]() { triggerCommand(name); });
        return;
    }
    const QString label = cmd.takesArgs() ? cmd.label + QStringLiteral("…") : cmd.label;
    QAction *a = menu->addAction(label);
    finish(a);
    connect(a, &QAction::triggered, this, [this, name]() { triggerCommand(name); });
}

void MainWindow::rebuildCommandMenus() {
    if (!m_hermesMenu) return;
    for (QAction *a : qAsConst(m_dynamicSessionActions)) { m_sessionMenu->removeAction(a); a->deleteLater(); }
    for (QAction *a : qAsConst(m_dynamicGoalActions)) { m_goalMenu->removeAction(a); a->deleteLater(); }
    m_dynamicSessionActions.clear();
    m_dynamicGoalActions.clear();

    // Collect actions a menu gains so they can be removed on the next rebuild.
    auto track = [](QMenu *menu, QList<QAction *> &list, const std::function<void()> &build) {
        const QList<QAction *> before = menu->actions();
        build();
        for (QAction *a : menu->actions()) if (!before.contains(a)) list << a;
    };

    const auto sessionCmds = m_catalog.commandsInGroup(QStringLiteral("session"));
    if (!sessionCmds.isEmpty()) {
        track(m_sessionMenu, m_dynamicSessionActions, [&]() {
            m_sessionMenu->addSection(tr("Hermes Session Commands"));
            for (const auto &c : sessionCmds) addCommandToMenu(m_sessionMenu, c);
        });
    }
    QList<CommandCatalog::Command> goalCmds;
    for (const auto &c : m_catalog.commandsInGroup(QStringLiteral("goal")))
        if (c.name != QLatin1String("goal")) goalCmds << c;
    if (!goalCmds.isEmpty()) {
        track(m_goalMenu, m_dynamicGoalActions, [&]() {
            m_goalMenu->addSection(tr("Automation"));
            for (const auto &c : goalCmds) addCommandToMenu(m_goalMenu, c);
        });
    }

    m_hermesMenu->clear();
    bool any = false;
    for (const auto &g : CommandCatalog::groups()) {
        if (g.id == QLatin1String("session") || g.id == QLatin1String("goal")) continue;
        const auto cmds = m_catalog.commandsInGroup(g.id);
        if (cmds.isEmpty()) continue;
        QMenu *sub = m_hermesMenu->addMenu(theme().icon(g.icon), g.title);
        sub->menuAction()->setProperty("themeIcon", g.icon);
        sub->menuAction()->setProperty("catalogGroup", g.id);
        sub->setToolTipsVisible(true);
        for (const auto &c : cmds) addCommandToMenu(sub, c);
        any = true;
    }
    if (!any) {
        QAction *none = m_hermesMenu->addAction(tr("Commands appear once Hermes connects"));
        none->setEnabled(false);
    }
    m_hermesMenu->addSeparator();
    m_hermesMenu->addAction(m_actions.value("app:palette"));
    m_hermesMenu->addAction(m_actions.value("app:refresh-commands"));
    m_hermesMenu->addSeparator();
    m_hermesMenu->addAction(m_actions.value("app:configure-provider"));
    m_hermesMenu->addAction(m_actions.value("app:configuration"));
    m_hermesMenu->addAction(m_actions.value("app:check-updates"));
    m_hermesMenu->addAction(m_actions.value("app:restart"));
    updateActionStates();
}

void MainWindow::rebuildConfigCombos() {
    if (!m_toolbar) return;
    for (QAction *a : qAsConst(m_configActions)) { m_toolbar->removeAction(a); }
    for (QComboBox *c : qAsConst(m_configCombos)) c->deleteLater();
    m_configActions.clear();
    m_configCombos.clear();
    if (!m_client.isReady()) { updateModelLabel(); return; }

    Theme &t = theme();
    auto addCombo = [&](const QString &key, const QString &name, const QString &description,
                        const QList<AcpClient::ConfigChoice> &choices, const QString &current,
                        std::function<void(const QString &)> apply) {
        if (choices.isEmpty()) return;
        auto *combo = new QComboBox(m_toolbar);
        combo->setObjectName(QStringLiteral("toolbarCombo"));
        combo->setProperty("configId", key);
        combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        combo->setMaximumWidth(t.px(key.contains(QLatin1String("model")) ? 260 : 190));
        combo->setFocusPolicy(Qt::TabFocus);
        combo->setCursor(Qt::PointingHandCursor);
        QString lastGroup;
        for (const auto &c : choices) {
            if (!c.group.isEmpty() && c.group != lastGroup) {
                if (combo->count() > 0) combo->insertSeparator(combo->count());
                lastGroup = c.group;
            }
            combo->addItem(c.name.isEmpty() ? c.value : c.name, c.value);
            const int i = combo->count() - 1;
            QString itemTip = c.description;
            if (!c.group.isEmpty()) itemTip = c.group + (itemTip.isEmpty() ? QString() : QStringLiteral(" — ") + itemTip);
            if (!itemTip.isEmpty()) combo->setItemData(i, itemTip, Qt::ToolTipRole);
        }
        const int idx = combo->findData(current);
        if (idx >= 0) combo->setCurrentIndex(idx);
        else if (!current.isEmpty()) { combo->insertItem(0, current, current); combo->setCurrentIndex(0); }
        const QString tip = description.isEmpty() ? name : QStringLiteral("%1 — %2").arg(name, description);
        ui::describe(combo, name, tip);
        connect(combo, QOverload<int>::of(&QComboBox::activated), this, [combo, apply](int i) {
            apply(combo->itemData(i).toString());
        });
        m_configActions << m_toolbar->insertWidget(m_configAnchor, combo);
        m_configCombos.insert(key, combo);
    };

    // Order: model, reasoning, mode, then anything else Hermes adds.
    auto rank = [](const AcpClient::ConfigOption &o) {
        if (o.category == QLatin1String("model")) return 0;
        if (o.category == QLatin1String("thought_level")) return 1;
        if (o.category == QLatin1String("mode")) return 2;
        return 3;
    };
    QList<AcpClient::ConfigOption> options = m_client.configOptions();
    std::stable_sort(options.begin(), options.end(), [&](const auto &a, const auto &b) { return rank(a) < rank(b); });
    bool haveMode = false;
    for (const auto &o : options) {
        if (o.type != QLatin1String("select")) continue;
        if (o.category == QLatin1String("mode")) haveMode = true;
        const QString id = o.id;
        addCombo(id, o.name.isEmpty() ? id : o.name, o.description, o.choices, o.currentValue,
                 [this, id](const QString &v) {
                     if (!m_client.setConfigOption(id, v)) showToast(tr("Couldn't change that setting right now."));
                     updateModelLabel();
                 });
    }
    if (!haveMode && !m_client.sessionModes().isEmpty()) {
        QList<AcpClient::ConfigChoice> modes;
        for (const auto &m : m_client.sessionModes()) modes << AcpClient::ConfigChoice{m.id, m.name, m.description, QString()};
        addCombo(QStringLiteral("__mode"), tr("Mode"), tr("How Hermes works in this session"), modes,
                 m_client.currentModeId(), [this](const QString &v) { m_client.setMode(v); });
    }
    if (!m_configActions.isEmpty()) {
        auto *sep = new QAction(this);
        sep->setSeparator(true);
        m_toolbar->insertAction(m_configAnchor, sep);
        m_configActions << sep;
    }
    updateModelLabel();
}

void MainWindow::rebuildPalette() {
    if (!m_palette) return;
    QList<PaletteAction> list;
    auto app = [&](const char *id, const QString &group) {
        QAction *a = m_actions.value(QString::fromLatin1(id));
        if (!a) return;
        PaletteAction p;
        p.id = QString::fromLatin1(id);
        p.label = a->text().remove(QLatin1Char('&'));
        if (p.label.endsWith(QStringLiteral("…"))) p.label.chop(1);
        p.shortcut = a->shortcut().toString(QKeySequence::NativeText);
        p.description = a->statusTip();
        p.group = group;
        p.icon = a->property("themeIcon").toString();
        p.enabled = a->isEnabled();
        list << p;
    };
    const QString consiglio = tr("Consiglio");
    for (const char *id : {"app:new-session", "app:goal", "app:stop", "app:end-session", "app:workspace",
                           "app:sidebar", "app:settings", "app:theme-light", "app:theme-dark",
                           "app:theme-system", "app:shortcuts"})
        app(id, consiglio);
    const QString setup = tr("Hermes Setup");
    for (const char *id : {"app:configure-provider", "app:configuration", "app:check-updates",
                           "app:restart", "app:refresh-commands", "app:docs"})
        app(id, setup);

    for (const auto &c : m_catalog.commands()) {
        PaletteAction p;
        p.id = QLatin1Char('/') + c.name;
        p.label = c.label;
        p.description = c.description;
        const auto g = CommandCatalog::group(c.group);
        p.group = tr("Hermes · %1").arg(g.title);
        p.icon = c.icon;
        p.detail = c.argsHint.isEmpty() ? p.id : p.id + QLatin1Char(' ') + c.argsHint;
        p.keywords = p.id + QLatin1Char(' ') + c.name;
        for (const QString &al : c.aliases) p.keywords += QStringLiteral(" /") + al;
        p.enabled = m_conn == Conn::Connected;
        list << p;
    }
    m_palette->setActions(list);
}

void MainWindow::refreshCommands() { startDiscovery(true); }

void MainWindow::startDiscovery(bool force) {
    if (!m_client.isReady()) return;
    QList<CommandCatalog::AdvertisedCommand> adv;
    for (const auto &c : m_client.slashCommands()) adv << CommandCatalog::AdvertisedCommand{c.name, c.description, c.hint};
    m_catalog.setAdvertised(adv);
    const QString version = m_client.agentVersion();
    if (!force && m_discoveredVersion == version && m_catalog.listingComplete()) return;
    if (m_discoveryPages > 0) return;   // already running
    m_discoveredVersion = version;
    m_discoveryPages = 1;
    m_catalog.beginListing();
    m_client.runQuietCommand(QStringLiteral("/commands 1"));
}

void MainWindow::refreshGoalStatus() {
    if (!m_client.isReady() || !m_catalog.contains(QStringLiteral("goal"))) return;
    m_client.runQuietCommand(QStringLiteral("/goal status"));
}

void MainWindow::onQuietCommandFinished(const QString &command, const QString &output, bool ok) {
    if (command.startsWith(QLatin1String("/commands"))) {
        if (!ok) { m_discoveryPages = 0; return; }
        const auto page = CommandCatalog::parsePage(output);
        if (!page.valid) {
            // Not a catalog page (an older/newer Hermes without /commands):
            // the ACP-advertised commands are all we have, and that's fine.
            m_discoveryPages = 0;
            refreshGoalStatus();
            return;
        }
        const int next = m_catalog.addListedPage(page);
        if (next > 0 && m_discoveryPages < kMaxDiscoveryPages) {
            ++m_discoveryPages;
            m_client.runQuietCommand(QStringLiteral("/commands %1").arg(next));
            return;
        }
        m_discoveryPages = 0;
        m_catalog.saveCache(m_client.agentVersion());
        refreshGoalStatus();
        return;
    }
    if (command.startsWith(QLatin1String("/goal"))) {
        if (!ok) return;
        const auto status = CommandCatalog::parseGoalStatus(output);
        if (status.known) {
            m_goal = status;
            updateGoalButton();
            updateActionStates();
        }
    }
}

// ─── Running commands ─────────────────────────────────────────────────────

bool MainWindow::runCommand(const QString &line) {
    const QString text = line.trimmed();
    if (text.isEmpty()) return false;
    if (m_conn != Conn::Connected) {
        showToast(tr("Hermes isn't connected yet."));
        return false;
    }
    if (m_busy || m_client.isBusy()) {
        showToast(tr("Hermes is busy — wait for the response to finish, or press Esc to stop it."));
        return false;
    }
    sendOrQueue(text, true);
    return true;
}

void MainWindow::sendOrQueue(const QString &text, bool display) {
    if (!m_client.isReady()) {
        m_pendingPrompt = text;
        m_pendingDisplay = display;
        if (!m_opening) startNewSession();
        return;
    }
    if (display) m_chat->appendUserMessage(text);
    m_lastCommand = text.startsWith(QLatin1Char('/')) ? text.section(QLatin1Char(' '), 0, 0) : QString();
    if (isGoalControl(text)) m_lastCommand = text;
    if (!m_client.sendPrompt(text)) {
        m_chat->appendNotice(tr("Couldn't send that to Hermes."), ChatBlock::Error);
        return;
    }
    beginTurnUi();
}

QDialog *MainWindow::createCommandDialog(const QString &name) {
    CommandCatalog::Command cmd = m_catalog.command(name);
    if (cmd.name.isEmpty()) {
        cmd.name = name;
        cmd.label = CommandCatalog::labelFor(name);
        cmd.group = CommandCatalog::groupFor(name, false);
        cmd.icon = CommandCatalog::iconFor(name, cmd.group);
    }
    return new CommandDialog(cmd, this);
}

QDialog *MainWindow::createGoalDialog() {
    const bool draft = m_catalog.command(QStringLiteral("goal")).argsHint.contains(QLatin1String("draft"))
                       || m_catalog.command(QStringLiteral("goal")).description.contains(QLatin1String("draft"), Qt::CaseInsensitive);
    return new GoalDialog(m_goal, draft, this);
}

void MainWindow::triggerCommand(const QString &rawName) {
    QString name = rawName;
    if (name.startsWith(QLatin1Char('/'))) name.remove(0, 1);
    const CommandCatalog::Command cmd = m_catalog.command(name);
    if (cmd.name == QLatin1String("goal") || name == QLatin1String("goal")) { openGoalDialog(); return; }
    if (cmd.name.isEmpty() || !cmd.takesArgs()) { runCommand(QLatin1Char('/') + (cmd.name.isEmpty() ? name : cmd.name)); return; }
    if (m_conn != Conn::Connected) { showToast(tr("Hermes isn't connected yet.")); return; }
    QScopedPointer<QDialog> dlg(createCommandDialog(cmd.name));
    if (dlg->exec() == QDialog::Accepted) {
        if (auto *cd = qobject_cast<CommandDialog *>(dlg.data())) runCommand(cd->commandLine());
    }
}

void MainWindow::openGoalDialog() {
    if (m_conn != Conn::Connected) { showToast(tr("Hermes isn't connected yet.")); return; }
    if (!m_client.isReady()) {
        m_pendingGoalDialog = true;
        if (!m_opening) startNewSession();
        return;
    }
    if (m_busy) { showToast(tr("Hermes is busy — set the goal when this response finishes.")); return; }
    QScopedPointer<QDialog> dlg(createGoalDialog());
    if (dlg->exec() == QDialog::Accepted) {
        if (auto *gd = qobject_cast<GoalDialog *>(dlg.data())) runCommand(gd->commandLine());
    }
}

SettingsDialog *MainWindow::openSettings(const QString &page) {
    if (!m_settings) {
        auto *dlg = new SettingsDialog(&m_client, &m_fonts, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setShortcuts(shortcutList());
        connect(dlg, &SettingsDialog::restartRequested, this, [this]() { onPaletteAction(QStringLiteral("app:restart")); });
        connect(dlg, &SettingsDialog::autoRestartChanged, this, [this](bool on) { m_client.setAutoRestart(on); });
        connect(dlg, &SettingsDialog::executableChanged, this, [this]() {
            if (m_conn == Conn::Failed) connectToHermes();
            else showToast(tr("The new Hermes executable is used the next time the agent restarts."), 6000);
        });
        m_settings = dlg;
    }
    if (!page.isEmpty()) m_settings->showPage(page);
    m_settings->show();
    m_settings->raise();
    m_settings->activateWindow();
    return m_settings;
}

// ─── Sessions ─────────────────────────────────────────────────────────────

QString MainWindow::cwd() const {
    if (!m_cwd.isEmpty()) return m_cwd;
    const QString ws = m_rail ? m_rail->workspace() : QString();
    return ws.isEmpty() ? QDir::homePath() : ws;
}

void MainWindow::startNewSession() { startSession(cwd()); }

void MainWindow::startSession(const QString &dir) {
    if (m_conn != Conn::Connected) { showToast(tr("Hermes isn't connected yet.")); return; }
    if (m_busy) m_client.cancelTurn();
    m_chat->clearConversation();
    m_cwd = dir;
    m_sessionTitle.clear();
    m_goal = CommandCatalog::GoalStatus();
    m_opening = true;
    if (!m_client.newSession(dir)) {
        m_opening = false;
        showToast(tr("Couldn't start a session."));
    }
    updateGoalButton();
    updateUiState();
}

void MainWindow::resumeSession(const QString &sessionId, const QString &dir) {
    if (m_conn != Conn::Connected) { showToast(tr("Hermes isn't connected yet.")); return; }
    if (m_busy) m_client.cancelTurn();
    m_chat->clearConversation();
    m_cwd = dir.isEmpty() ? cwd() : dir;
    m_sessionTitle.clear();
    m_goal = CommandCatalog::GoalStatus();
    m_opening = true;
    if (!m_client.resumeSession(sessionId, m_cwd)) {
        m_opening = false;
        showToast(tr("Couldn't reopen that session."));
    }
    updateGoalButton();
    updateUiState();
}

void MainWindow::stopSession() {
    if (m_busy) endTurnUi();
    m_client.stopSession();
    m_goal = CommandCatalog::GoalStatus();
    setActive(false);
    rebuildConfigCombos();
    updateGoalButton();
    updateUiState();
    refreshSessionList();
}

void MainWindow::refreshSessionList() {
    if (m_client.isConnected()) m_client.listSessions(m_cwd);
}

void MainWindow::setActive(bool active) {
    m_active = active;
    m_rail->setSessionActive(active, m_sessionTitle.isEmpty() ? tr("New session") : m_sessionTitle,
                             active ? m_client.sessionCwd() : QString(), active ? m_client.sessionId() : QString());
}

void MainWindow::onSessionStarted(const QString &dir) {
    if (!dir.isEmpty() && dir != m_cwd) {
        m_cwd = dir;
        QSettings().setValue(QStringLiteral("session/workspace"), dir);
    }
    startSession(cwd());
}

void MainWindow::onStopRequested() { stopSession(); }

void MainWindow::onResumeRequested(const QString &sessionId, const QString &dir) { resumeSession(sessionId, dir); }

void MainWindow::onPromptSubmitted(const QString &raw) {
    const QString text = raw.trimmed();
    if (text.isEmpty()) return;
    if (m_conn != Conn::Connected) { showToast(tr("Hermes isn't connected yet.")); return; }
    if (m_busy || m_client.isBusy()) { showToast(tr("Hermes is still answering — press Esc to stop it.")); return; }
    sendOrQueue(text, true);
}

// ─── Client events ────────────────────────────────────────────────────────

void MainWindow::onConnected() {
    m_conn = Conn::Connected;
    m_connError.clear();
    m_chat->hideBanner();
    refreshSessionList();
    updateUiState();
    rebuildPalette();
    if ((!m_pendingPrompt.isEmpty() || m_pendingGoalDialog) && !m_client.isReady() && !m_opening) startNewSession();
}

void MainWindow::onSessionsListed(QList<AcpClient::SessionInfo> sessions) {
    m_rail->setSessions(sessions);
    m_sessionCount = sessions.size();
    m_recentMenu->clear();
    const QDateTime now = QDateTime::currentDateTime();
    int shown = 0;
    for (const auto &s : sessions) {
        if (s.id == m_client.sessionId()) continue;
        if (++shown > 12) break;
        const QString title = s.title.isEmpty() ? tr("Untitled session") : s.title;
        QString when;
        const QDateTime at = QDateTime::fromString(s.updatedAt, Qt::ISODate);
        if (at.isValid()) when = ui::relativeTime(at.toLocalTime(), now);
        QAction *a = m_recentMenu->addAction(when.isEmpty() ? title : QStringLiteral("%1\t%2").arg(title, when));
        a->setToolTip(s.cwd.isEmpty() ? title : QStringLiteral("%1 — %2").arg(title, s.cwd));
        a->setData(QStringList{s.id, s.cwd});
        const QString id = s.id, dir = s.cwd;
        connect(a, &QAction::triggered, this, [this, id, dir]() { resumeSession(id, dir); });
    }
    if (shown == 0) {
        QAction *none = m_recentMenu->addAction(tr("No recent sessions"));
        none->setEnabled(false);
    }
    updateEmptyState();
}

void MainWindow::onReady() {
    m_opening = false;
    setActive(true);
    rebuildConfigCombos();
    startDiscovery(false);
    if (m_discoveryPages == 0) refreshGoalStatus();
    if (!m_pendingPrompt.isEmpty()) {
        const QString text = m_pendingPrompt;
        const bool display = m_pendingDisplay;
        m_pendingPrompt.clear();
        sendOrQueue(text, display);
    }
    if (m_pendingGoalDialog) {
        m_pendingGoalDialog = false;
        QTimer::singleShot(0, this, &MainWindow::openGoalDialog);
    }
    updateUiState();
    refreshSessionList();
}

void MainWindow::onError(const QString &message) {
    if (m_busy && !m_client.isBusy()) endTurnUi();
    if (!m_client.isConnected()) {
        m_conn = Conn::Failed;
        m_connError = message;
        m_opening = false;
        updateUiState();
    } else {
        showToast(message, 6000);
    }
}

void MainWindow::onMessageChunk(const QString &text) {
    m_chat->appendAgentChunk(text);
    m_streamedChars += text.length();
}

void MainWindow::onThoughtChunk(const QString &text) { m_chat->appendThoughtChunk(text); }

void MainWindow::onToolCallStarted(const QString &id, const QString &title, const QString &kind) {
    m_chat->addToolCall(id, title, kind);
}

void MainWindow::onToolCallUpdated(const QString &id, const QString &status, const QString &output) {
    m_chat->updateToolCall(id, status, output);
}

void MainWindow::onContextUsage(int used, int size) {
    if (size <= 0) return;
    const int pct = qBound(0, int((qint64(used) * 100) / size), 100);
    m_contextBar->setValue(pct);
    const char *level = pct >= 90 ? "danger" : pct >= 70 ? "warn" : "ok";
    if (m_contextBar->property("level").toString() != QLatin1String(level)) {
        m_contextBar->setProperty("level", level);
        m_contextBar->style()->unpolish(m_contextBar);
        m_contextBar->style()->polish(m_contextBar);
    }
    m_contextLabel->setText(tr("Context %1%").arg(pct));
    const QString tip = tr("Context window: %1 of %2 tokens used (%3%)").arg(shortCount(used), shortCount(size)).arg(pct);
    m_contextLabel->setToolTip(tip);
    m_contextBar->setToolTip(tip);
    m_contextLabel->show();
    m_contextBar->show();
}

void MainWindow::onSessionTitle(const QString &title) {
    m_sessionTitle = title;
    if (m_active) setActive(true);
}

void MainWindow::onCommandsUpdated() {
    QList<CommandCatalog::AdvertisedCommand> adv;
    for (const auto &c : m_client.slashCommands()) adv << CommandCatalog::AdvertisedCommand{c.name, c.description, c.hint};
    m_catalog.setAdvertised(adv);
}

void MainWindow::onTurnCompleted(const QString &stopReason, qint64 inputTokens, qint64 outputTokens) {
    endTurnUi();
    const double secs = qMax<qint64>(1, QDateTime::currentMSecsSinceEpoch() - m_turnStartMs) / 1000.0;
    if (inputTokens > 0 || outputTokens > 0) {
        m_tokensLabel->setText(tr("↑ %1  ↓ %2 · %3 tok/s")
                                   .arg(shortCount(inputTokens), shortCount(outputTokens))
                                   .arg(outputTokens / secs, 0, 'f', 1));
        m_tokensLabel->setToolTip(tr("Last response: %1 input tokens, %2 output tokens in %3 s")
                                      .arg(inputTokens).arg(outputTokens).arg(secs, 0, 'f', 1));
    }
    if (stopReason == QLatin1String("cancelled")) m_chat->appendNotice(tr("Stopped"), ChatBlock::Warning);
    else if (stopReason == QLatin1String("max_tokens")) m_chat->appendNotice(tr("Response cut short: the model hit its output limit."), ChatBlock::Warning);
    else if (stopReason == QLatin1String("refusal")) m_chat->appendNotice(tr("The model declined to answer."), ChatBlock::Warning);
    const bool goalRelated = isGoalControl(m_lastCommand) || m_goal.state == QLatin1String("active")
                             || m_goal.state == QLatin1String("parked");
    m_lastCommand.clear();
    if (goalRelated) refreshGoalStatus();
    updateUiState();
}

void MainWindow::onProcessExited(int code) {
    if (m_busy) endTurnUi();
    m_opening = false;
    setActive(false);
    m_discoveryPages = 0;
    m_conn = Conn::Failed;
    m_connError = code == 0 ? tr("Hermes exited.") : tr("Hermes exited unexpectedly (code %1).").arg(code);
    rebuildConfigCombos();
    updateUiState();
}

void MainWindow::onTurnFailed(const QString &message) {
    endTurnUi();
    m_lastCommand.clear();
    m_chat->appendNotice(ui::maskSecrets(message), ChatBlock::Error);
    updateUiState();
}

void MainWindow::onSessionFailed(const QString &message) {
    m_opening = false;
    setActive(false);
    m_pendingGoalDialog = false;
    if (!m_pendingPrompt.isEmpty() && m_chat->composerText().isEmpty()) m_chat->setComposerText(m_pendingPrompt);
    m_pendingPrompt.clear();
    m_chat->appendNotice(tr("Couldn't open the session: %1").arg(ui::maskSecrets(message)), ChatBlock::Error);
    updateUiState();
}

void MainWindow::onPermissionRequested(const AcpClient::PermissionRequest &request) {
    auto *dlg = new PermissionDialog(request, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    const QString key = request.key;
    connect(dlg, &QDialog::finished, this, [this, dlg, key](int result) {
        m_permissionDialogs.remove(key);
        const QString choice = dlg->choice();
        if (result != QDialog::Accepted || choice.isEmpty() || !m_client.respondPermission(key, choice))
            m_client.cancelPermission(key);
        updateUiState();
    });
    m_permissionDialogs.insert(key, dlg);
    m_stateDot->setState(QStringLiteral("warn"));
    m_stateLabel->setText(tr("Waiting for your approval…"));
    QApplication::alert(this);
    dlg->open();
}

void MainWindow::onPermissionCancelled(const QString &key) {
    if (auto dlg = m_permissionDialogs.take(key)) {
        dlg->disconnect(this);
        dlg->close();
    }
    updateUiState();
}

void MainWindow::onAgentRestarting(int attempt, int delayMs) {
    if (m_busy) endTurnUi();
    m_conn = Conn::Reconnecting;
    m_opening = false;
    setActive(false);
    m_connError = tr("Reconnecting (attempt %1, in %2 s)…").arg(attempt).arg(delayMs / 1000.0, 0, 'f', 1);
    updateUiState();
}

void MainWindow::onAgentRestartFailed(const QString &message) {
    if (m_busy) endTurnUi();
    m_conn = Conn::Failed;
    m_connError = message;
    updateUiState();
}

void MainWindow::onAgentVersionChanged(const QString &oldVersion, const QString &newVersion) {
    m_discoveredVersion.clear();   // re-read the command list for the new version
    m_chat->appendNotice(tr("Hermes updated %1 → %2 — refreshing commands").arg(oldVersion, newVersion), ChatBlock::Success);
    if (m_client.isReady()) startDiscovery(true);
}

void MainWindow::onSessionRestored(const QString &sessionId) {
    Q_UNUSED(sessionId);
    m_opening = false;
    setActive(true);
    m_chat->appendNotice(tr("Reconnected — session restored"), ChatBlock::Success);
    rebuildConfigCombos();
    startDiscovery(false);
    updateUiState();
}

// ─── App actions ──────────────────────────────────────────────────────────

void MainWindow::onPaletteAction(const QString &id) {
    if (id.startsWith(QLatin1Char('/'))) { triggerCommand(id.mid(1)); return; }
    if (id == QLatin1String("app:new-session")) startNewSession();
    else if (id == QLatin1String("app:end-session")) { if (m_client.isReady()) stopSession(); }
    else if (id == QLatin1String("app:stop")) { if (m_busy) m_client.cancelTurn(); }
    else if (id == QLatin1String("app:settings")) openSettings();
    else if (id == QLatin1String("app:agent-settings")) openSettings(QStringLiteral("agent"));
    else if (id == QLatin1String("app:quit")) close();
    else if (id == QLatin1String("app:workspace")) {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Choose Workspace Folder"), cwd());
        if (!dir.isEmpty()) {
            m_cwd = dir;
            m_rail->setWorkspace(dir);
            QSettings().setValue(QStringLiteral("session/workspace"), dir);
            refreshSessionList();
            updateEmptyState();
        }
    }
    else if (id == QLatin1String("app:goal")) openGoalDialog();
    else if (id == QLatin1String("app:goal-status")) runCommand(QStringLiteral("/goal status"));
    else if (id == QLatin1String("app:goal-show")) runCommand(QStringLiteral("/goal show"));
    else if (id == QLatin1String("app:goal-pause")) runCommand(QStringLiteral("/goal pause"));
    else if (id == QLatin1String("app:goal-resume")) runCommand(QStringLiteral("/goal resume"));
    else if (id == QLatin1String("app:goal-clear")) {
        const auto answer = QMessageBox::question(this, tr("Clear Goal"),
            m_goal.goal.isEmpty() ? tr("Clear the standing goal?")
                                  : tr("Clear the standing goal?\n\n“%1”").arg(m_goal.goal),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer == QMessageBox::Yes) runCommand(QStringLiteral("/goal clear"));
    }
    else if (id == QLatin1String("app:palette") || id == QLatin1String("app:all-commands")) m_palette->open();
    else if (id == QLatin1String("app:refresh-commands")) {
        if (!m_client.isReady()) { showToast(tr("Start a session to read the command list from Hermes.")); return; }
        startDiscovery(true);
        showToast(tr("Refreshing the command list from Hermes…"), 2500);
    }
    else if (id == QLatin1String("app:configure-provider")) {
        QStringList args{QStringLiteral("setup")};
        for (const auto &m : m_client.authMethods())
            if (m.type == QLatin1String("terminal") && !m.args.isEmpty()) { args = m.args; break; }
        QString error;
        if (!HermesCli::openInTerminal(args, &error)) {
            QMessageBox::information(this, tr("Configure Provider"),
                tr("Couldn't open a terminal (%1).\n\nRun this yourself, then restart the agent:\n\n    hermes %2")
                    .arg(error, args.join(QLatin1Char(' '))));
        } else {
            showToast(tr("Provider setup opened in a terminal. Restart the agent when it's done."), 8000);
        }
    }
    else if (id == QLatin1String("app:configuration")) openSettings(QStringLiteral("config"));
    else if (id == QLatin1String("app:check-updates")) openSettings(QStringLiteral("agent"));
    else if (id == QLatin1String("app:restart") || id == QLatin1String("app:retry")) {
        if (m_busy) endTurnUi();
        if (m_conn == Conn::Failed && !m_client.isConnected()) { connectToHermes(); return; }
        m_conn = Conn::Reconnecting;
        m_connError = tr("Restarting Hermes…");
        updateUiState();
        m_client.restartAgent();
    }
    else if (id == QLatin1String("app:sidebar")) {
        const bool show = m_actions.value(id)->isChecked();
        m_rail->setVisible(show);
        QSettings().setValue(QStringLiteral("view/sidebar"), show);
    }
    else if (id == QLatin1String("app:focus-composer")) m_chat->focusComposer();
    else if (id == QLatin1String("app:zoom-in")) applyZoom(0.1);
    else if (id == QLatin1String("app:zoom-out")) applyZoom(-0.1);
    else if (id == QLatin1String("app:zoom-reset")) applyZoom(0);
    else if (id == QLatin1String("app:theme-system")) theme().setMode(Theme::Mode::System);
    else if (id == QLatin1String("app:theme-light")) theme().setMode(Theme::Mode::Light);
    else if (id == QLatin1String("app:theme-dark")) theme().setMode(Theme::Mode::Dark);
    else if (id == QLatin1String("app:help")) runCommand(QStringLiteral("/help"));
    else if (id == QLatin1String("app:docs")) QDesktopServices::openUrl(QUrl(QString::fromLatin1(kDocsUrl)));
    else if (id == QLatin1String("app:install")) QDesktopServices::openUrl(QUrl(QString::fromLatin1(kInstallUrl)));
    else if (id == QLatin1String("app:shortcuts")) openSettings(QStringLiteral("shortcuts"));
    else if (id == QLatin1String("app:resume-last")) {
        for (QAction *a : m_recentMenu->actions()) {
            const QStringList d = a->data().toStringList();
            if (d.size() == 2) { resumeSession(d[0], d[1]); return; }
        }
    }
    else if (id == QLatin1String("app:about")) {
        const QString agent = m_client.agentVersion().isEmpty()
            ? tr("not connected") : QStringLiteral("%1 %2").arg(m_client.agentName(), m_client.agentVersion());
        QMessageBox box(this);
        box.setWindowTitle(tr("About Consiglio"));
        box.setIconPixmap(QIcon(QStringLiteral(":/logo.svg")).pixmap(theme().px(56), theme().px(56)));
        box.setTextFormat(Qt::RichText);
        box.setText(tr("<h3 style='margin:0'>Consiglio · Hermes</h3><p>Version %1</p>"
                       "<p>A native desktop client for the Hermes Agent, over the Agent Client Protocol.</p>"
                       "<p>Hermes: %2<br>%3 commands available</p>")
                        .arg(QApplication::applicationVersion(), agent.toHtmlEscaped())
                        .arg(m_catalog.count()));
        box.exec();
    }
}

// ─── UI state ─────────────────────────────────────────────────────────────

QString MainWindow::stateText() const { return m_stateLabel ? m_stateLabel->text() : QString(); }

void MainWindow::updateUiState() {
    if (!m_chat) return;
    const bool connected = m_conn == Conn::Connected;
    const bool session = m_client.isReady();
    QString dot, label;
    switch (m_conn) {
    case Conn::Connecting: dot = "busy"; label = tr("Connecting to Hermes…"); break;
    case Conn::Reconnecting: dot = "warn"; label = m_connError.isEmpty() ? tr("Reconnecting…") : m_connError; break;
    case Conn::Failed: dot = "error"; label = tr("Not connected"); break;
    case Conn::Connected:
        if (m_opening) { dot = "busy"; label = tr("Opening session…"); }
        else if (m_busy) { dot = "busy"; label = m_lastCommand.isEmpty() ? tr("Hermes is working…") : tr("Running %1…").arg(m_lastCommand.section(' ', 0, 0)); }
        else if (session) { dot = "ok"; label = tr("Ready"); }
        else { dot = "ok"; label = tr("Connected"); }
        break;
    }
    if (connected && !m_client.agentVersion().isEmpty() && !m_busy && !m_opening)
        label += QStringLiteral(" · Hermes %1").arg(m_client.agentVersion());
    m_stateDot->setState(dot);
    m_stateLabel->setText(label);

    if (m_conn == Conn::Failed) {
        m_chat->showBanner(QStringLiteral("error"), tr("Hermes isn't running"),
                           m_connError.isEmpty() ? tr("Consiglio couldn't connect to the Hermes agent.") : ui::maskSecrets(m_connError),
                           {{QStringLiteral("app:retry"), tr("Retry"), true}, {QStringLiteral("app:agent-settings"), tr("Settings…"), false}});
    } else if (m_conn == Conn::Reconnecting) {
        m_chat->showBanner(QStringLiteral("warning"), tr("Reconnecting to Hermes"),
                           tr("Hermes restarted (an update, or it exited). Your session will be reopened automatically."));
    } else {
        m_chat->hideBanner();
    }

    if (!connected) m_chat->setComposerEnabled(false, m_conn == Conn::Failed ? tr("Hermes isn't connected") : tr("Connecting to Hermes…"));
    else if (m_opening) m_chat->setComposerEnabled(false, tr("Opening session…"));
    else m_chat->setComposerEnabled(true, session ? tr("Message Hermes…   / for commands") : tr("Message Hermes to start a session…   / for commands"));

    m_rail->setConnected(connected);
    updateEmptyState();
    updateGoalButton();
    updateActionStates();
}

void MainWindow::updateEmptyState() {
    if (!m_chat) return;
    using QA = ChatPane::QuickAction;
    const auto sc = [this](const char *id) {
        QAction *a = m_actions.value(QString::fromLatin1(id));
        return a ? a->shortcut().toString(QKeySequence::NativeText) : QString();
    };
    switch (m_conn) {
    case Conn::Connecting:
        m_chat->setEmptyState(tr("Connecting to Hermes…"), tr("Starting the agent. This usually takes a few seconds."), {}, true);
        return;
    case Conn::Reconnecting:
        m_chat->setEmptyState(tr("Reconnecting…"), tr("Hermes restarted. Your session will be reopened automatically."), {}, true);
        return;
    case Conn::Failed:
        m_chat->setEmptyState(tr("Hermes isn't running"),
                              tr("Check that Hermes is installed and on your PATH, or point Consiglio at it in Settings."),
                              {QA{"app:retry", tr("Try Again"), tr("Reconnect to Hermes"), "refresh-cw", sc("app:restart")},
                               QA{"app:agent-settings", tr("Hermes Settings"), tr("Executable, setup and diagnostics"), "settings", sc("app:settings")},
                               QA{"app:install", tr("Install Hermes"), tr("Official installer and instructions"), "download", QString()},
                               QA{"app:docs", tr("Documentation"), tr("Hermes docs in your browser"), "book-open", QString()}});
        return;
    case Conn::Connected:
        break;
    }
    if (m_opening) {
        m_chat->setEmptyState(tr("Opening session…"), QDir::toNativeSeparators(cwd()), {}, true);
        return;
    }
    if (!m_client.isReady()) {
        bool hasRecent = false;
        for (QAction *a : m_recentMenu->actions()) if (a->data().toStringList().size() == 2) { hasRecent = true; break; }
        QList<QA> actions{QA{"app:new-session", tr("New Session"), tr("Start fresh in %1").arg(QDir(cwd()).dirName().isEmpty() ? cwd() : QDir(cwd()).dirName()), "message-square-plus", sc("app:new-session")},
                          QA{"app:goal", tr("Set a Goal"), tr("Hermes works toward it across turns"), "target", sc("app:goal")}};
        if (hasRecent) actions << QA{"app:resume-last", tr("Resume Last"), tr("Pick up where you left off"), "history", QString()};
        else actions << QA{"app:settings", tr("Settings"), tr("Theme, model and Hermes setup"), "settings", sc("app:settings")};
        actions << QA{"app:palette", tr("Browse Commands"), tr("Every Hermes command"), "command", sc("app:palette")};
        m_chat->setEmptyState(tr("What should Hermes work on?"),
                              tr("Type a message to start a session in %1, or pick an action.").arg(QDir::toNativeSeparators(cwd())),
                              actions);
        return;
    }
    QList<QA> actions{QA{"app:goal", tr("Set a Goal"), tr("A standing objective for this session"), "target", sc("app:goal")}};
    if (m_catalog.contains(QStringLiteral("status")))
        actions << QA{"/status", tr("Session Status"), tr("Model, tokens and settings"), "activity", QString()};
    if (m_catalog.contains(QStringLiteral("tools")))
        actions << QA{"/tools", tr("Tools"), tr("What Hermes can use"), "wrench", QString()};
    actions << QA{"app:palette", tr("All Commands"), tr("%n command(s) available", nullptr, m_catalog.count()), "command", sc("app:palette")};
    m_chat->setEmptyState(tr("Hermes is ready"), tr("Ask anything, or give it a goal to pursue."), actions);
}

void MainWindow::updateModelLabel() {
    QString model;
    for (const auto &o : m_client.configOptions()) {
        if (o.category != QLatin1String("model") && o.id != QLatin1String("model")) continue;
        model = o.currentValue;
        for (const auto &c : o.choices) if (c.value == o.currentValue && !c.name.isEmpty()) { model = c.name; break; }
        break;
    }
    if (model.isEmpty() && m_client.isReady() && !m_client.availableModels().isEmpty()) model = m_client.availableModels().first();
    m_modelLabel->setText(model);
    m_modelLabel->setToolTip(model.isEmpty() ? QString() : tr("Model: %1").arg(model));
    m_modelLabel->setVisible(!model.isEmpty());
}

void MainWindow::updateGoalButton() {
    if (!m_goalButton) return;
    Theme &t = theme();
    const QString state = m_goal.known ? m_goal.state : QStringLiteral("none");
    const bool has = state != QLatin1String("none") && !m_goal.goal.isEmpty();
    QString text = tr("Goal"), icon = QStringLiteral("target"), tip;
    if (has) {
        const QString goal = m_goal.goal.simplified();
        if (state == QLatin1String("paused")) { text = tr("Paused: %1").arg(goal); icon = QStringLiteral("circle-pause"); }
        else if (state == QLatin1String("done")) { text = tr("Done: %1").arg(goal); icon = QStringLiteral("circle-check"); }
        else if (state == QLatin1String("parked")) { text = tr("Waiting: %1").arg(goal); icon = QStringLiteral("hourglass"); }
        else text = goal;
        tip = tr("Goal (%1%2): %3\nClick to change it · use the arrow for goal actions (%4)")
                  .arg(state, m_goal.meta.isEmpty() ? QString() : QStringLiteral(", ") + m_goal.meta, goal,
                       m_actions.value("app:goal")->shortcut().toString(QKeySequence::NativeText));
    } else {
        tip = ui::withShortcut(tr("Set a goal Hermes works toward across turns"), m_actions.value("app:goal")->shortcut());
    }
    const QFontMetrics fm(m_goalButton->font());
    m_goalButton->setText(fm.elidedText(text, Qt::ElideRight, t.px(230)));
    const QColor iconColor = state == QLatin1String("paused") && has ? t.tokens().warning
                           : state == QLatin1String("done") && has ? t.tokens().success
                           : t.tokens().accentText;
    m_goalButton->setIcon(t.icon(icon, iconColor));
    ui::describe(m_goalButton, has ? tr("Goal: %1").arg(m_goal.goal) : tr("Set Goal"), tip);
    const QString prop = has ? state : QStringLiteral("none");
    if (m_goalButton->property("goalState").toString() != prop) {
        m_goalButton->setProperty("goalState", prop);
        m_goalButton->setProperty("goalActive", has && state == QLatin1String("active"));
        m_goalButton->style()->unpolish(m_goalButton);
        m_goalButton->style()->polish(m_goalButton);
    }
    m_goalButton->setEnabled(m_conn == Conn::Connected);
}

void MainWindow::updateActionStates() {
    if (m_actions.isEmpty()) return;
    const bool connected = m_conn == Conn::Connected;
    const bool session = m_client.isReady();
    const QString state = m_goal.known ? m_goal.state : QString();
    const bool goalCmd = !m_catalog.count() || m_catalog.contains(QStringLiteral("goal"));
    auto en = [this](const char *id, bool on) { if (QAction *a = m_actions.value(QString::fromLatin1(id))) a->setEnabled(on); };
    en("app:new-session", connected && !m_opening);
    en("app:end-session", session && !m_opening);
    en("app:stop", m_busy);
    en("app:goal", connected && goalCmd);
    en("app:goal-status", connected && goalCmd && !m_busy);
    en("app:goal-show", connected && goalCmd && !m_busy && !state.isEmpty() && state != QLatin1String("none"));
    en("app:goal-pause", connected && !m_busy && (state == QLatin1String("active") || state == QLatin1String("parked")));
    en("app:goal-resume", connected && !m_busy && state == QLatin1String("paused"));
    en("app:goal-clear", connected && !m_busy && !state.isEmpty() && state != QLatin1String("none"));
    en("app:refresh-commands", session && !m_busy);
    en("app:help", connected);
    en("app:configure-provider", true);
    const auto setTree = [connected](QMenu *menu, auto &&self) -> void {
        for (QAction *a : menu->actions()) {
            if (a->property("catalogCommand").isValid() || a->property("catalogGroup").isValid()) a->setEnabled(connected);
            if (a->menu() && a->property("catalogGroup").isValid()) self(a->menu(), self);
        }
    };
    if (m_hermesMenu) setTree(m_hermesMenu, setTree);
    if (m_sessionMenu) setTree(m_sessionMenu, setTree);
    if (m_goalMenu) setTree(m_goalMenu, setTree);
}

void MainWindow::beginTurnUi() {
    m_busy = true;
    m_streamedChars = 0;
    m_turnStartMs = QDateTime::currentMSecsSinceEpoch();
    m_timerLabel->setText(QStringLiteral("0:00"));
    m_tokensLabel->clear();
    m_turnTimer->start();
    m_chat->setBusy(true, m_lastCommand.isEmpty() ? tr("Thinking") : tr("Running %1").arg(m_lastCommand.section(' ', 0, 0)));
    updateUiState();
}

void MainWindow::endTurnUi() {
    m_chat->finalizeTurn();
    m_chat->setBusy(false);
    m_busy = false;
    m_turnTimer->stop();
    onTimerTick();
}

void MainWindow::onTimerTick() {
    if (!m_turnStartMs) return;
    const qint64 elapsed = (QDateTime::currentMSecsSinceEpoch() - m_turnStartMs) / 1000;
    m_timerLabel->setText(QStringLiteral("%1:%2").arg(elapsed / 60).arg(elapsed % 60, 2, 10, QLatin1Char('0')));
    if (m_busy && elapsed >= 1 && m_streamedChars > 0) {
        const double est = m_streamedChars / 4.0;
        m_tokensLabel->setText(tr("↓ ~%1 · ~%2 tok/s").arg(shortCount(qint64(est))).arg(est / elapsed, 0, 'f', 1));
    }
}

void MainWindow::showToast(const QString &text, int ms) { statusBar()->showMessage(text, ms); }

void MainWindow::applyZoom(double delta) {
    theme().setScale(delta == 0 ? 1.0 : theme().scale() + delta);
}

void MainWindow::restyle() {
    Theme &t = theme();
    const auto reicon = [&t](QObject *o) {
        const QString name = o->property("themeIcon").toString();
        if (name.isEmpty()) return;
        if (auto *a = qobject_cast<QAction *>(o)) a->setIcon(t.icon(name));
        else if (auto *m = qobject_cast<QMenu *>(o)) m->setIcon(t.icon(name));
    };
    for (QAction *a : findChildren<QAction *>()) reicon(a);
    for (QMenu *m : findChildren<QMenu *>()) {
        reicon(m);
        for (QAction *a : m->actions()) reicon(a);
    }
    m_rail->setFixedWidth(t.px(268));
    m_toolbar->setIconSize(QSize(t.px(16), t.px(16)));
    m_goalButton->setIconSize(QSize(t.px(16), t.px(16)));
    m_contextBar->setFixedWidth(t.px(72));
    for (const char *id : {"app:theme-system", "app:theme-light", "app:theme-dark"}) {
        const Theme::Mode mode = Theme::modeFromName(QString::fromLatin1(id).mid(10));
        if (QAction *a = m_actions.value(QString::fromLatin1(id))) a->setChecked(t.mode() == mode);
    }
    updateGoalButton();
    rebuildConfigCombos();
}

QList<QPair<QString, QString>> MainWindow::shortcutList() const {
    QList<QPair<QString, QString>> list;
    const char *order[] = {"app:new-session", "app:goal", "app:goal-status", "app:stop", "app:end-session",
                           "app:palette", "app:workspace", "app:sidebar", "app:focus-composer", "app:settings",
                           "app:restart", "app:zoom-in", "app:zoom-out", "app:zoom-reset", "app:help",
                           "app:shortcuts", "app:quit"};
    for (const char *id : order) {
        QAction *a = m_actions.value(QString::fromLatin1(id));
        if (!a || a->shortcut().isEmpty()) continue;
        QString text = a->text().remove(QLatin1Char('&'));
        if (text.endsWith(QStringLiteral("…"))) text.chop(1);
        list << qMakePair(text, a->shortcut().toString(QKeySequence::NativeText));
    }
    list << qMakePair(tr("Command Palette (alternate)"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P).toString(QKeySequence::NativeText));
    list << qMakePair(tr("Send message"), tr("Enter"));
    list << qMakePair(tr("New line"), tr("Shift+Enter"));
    list << qMakePair(tr("Stop response (while answering)"), tr("Esc"));
    list << qMakePair(tr("Previous / next sent message"), tr("Up / Down"));
    list << qMakePair(tr("Commands (in an empty message box)"), QStringLiteral("/"));
    list << qMakePair(tr("Insert a file path (mid-message)"), QStringLiteral("/  or  ./"));
    return list;
}

void MainWindow::closeEvent(QCloseEvent *event) {
    m_client.shutdown();
    event->accept();
}

// ─── File picker ──────────────────────────────────────────────────────────

void MainWindow::onFilePathRequested(int column, bool relative) {
    m_filePickerColumn = column;
    m_filePickerRelative = relative;
    const QString root = relative ? cwd() : QDir::rootPath();
    if (m_filePicker) m_filePicker->open(root);
}

void MainWindow::onPathSelected(const QString &path) {
    QString insert = path;
    if (m_filePickerRelative) {
        const QString rel = relativeToCwd(cwd(), path);
        if (!rel.isEmpty() && !rel.startsWith(QLatin1String(".."))) insert = QStringLiteral("./") + rel;
    }
    m_chat->insertPathAt(m_filePickerColumn, insert, m_filePickerRelative);
}
