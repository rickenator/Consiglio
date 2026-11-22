#include "settingsdialog.h"

#include "acpclient.h"
#include "fontsettings.h"
#include "hermescli.h"
#include "hermesremote.h"
#include "hermessetupdialog.h"
#include "theme.h"
#include "uiutil.h"

#include <QTimer>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTextCursor>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {

Theme &th() { return Theme::instance(); }

QFrame *card(QWidget *parent, QVBoxLayout **layout) {
    auto *c = new QFrame(parent);
    c->setProperty("role", "card");
    auto *l = new QVBoxLayout(c);
    l->setContentsMargins(th().px(16), th().px(14), th().px(16), th().px(14));
    l->setSpacing(th().px(10));
    *layout = l;
    return c;
}

QLabel *label(const QString &text, const QString &role, QWidget *parent) {
    auto *l = new QLabel(text, parent);
    if (!role.isEmpty()) l->setProperty("role", role);
    l->setWordWrap(true);
    return l;
}

// "Title / description" on the left, a control on the right.
QWidget *settingRow(const QString &title, const QString &description, QWidget *control, QWidget *parent) {
    auto *row = new QWidget(parent);
    auto *l = new QHBoxLayout(row);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(th().px(16));
    auto *text = new QVBoxLayout();
    text->setSpacing(th().px(2));
    auto *t = new QLabel(title, row);
    t->setStyleSheet("font-weight: 600;");
    text->addWidget(t);
    if (!description.isEmpty()) text->addWidget(label(description, "muted", row));
    l->addLayout(text, 1);
    if (control) {
        control->setParent(row);
        l->addWidget(control, 0, Qt::AlignVCenter);
        if (control->accessibleName().isEmpty()) control->setAccessibleName(title);
        if (control->toolTip().isEmpty() && !description.isEmpty()) control->setToolTip(description);
    }
    return row;
}

QFrame *divider(QWidget *parent) {
    auto *f = new QFrame(parent);
    f->setFixedHeight(1);
    f->setStyleSheet(QStringLiteral("background: %1;").arg(th().tokens().border.name()));
    return f;
}

QPushButton *button(const QString &text, const QString &icon, const QString &tooltip, QWidget *parent,
                    const QString &role = QString()) {
    auto *b = new QPushButton(text, parent);
    if (!icon.isEmpty()) {
        b->setIcon(th().icon(icon, role == "primary" ? th().tokens().accentText : QColor()));
        b->setIconSize(QSize(th().px(15), th().px(15)));
    }
    if (!role.isEmpty()) b->setProperty("role", role);
    b->setCursor(Qt::PointingHandCursor);
    ui::describe(b, text, tooltip);
    return b;
}

} // namespace

SettingsDialog::SettingsDialog(AcpClient *client, FontSettings *fonts, QWidget *parent)
    : QDialog(parent), m_client(client), m_fonts(fonts) {
    setWindowTitle(tr("Settings"));
    setObjectName("settingsDialog");
    resize(th().px(900), th().px(620));
    setMinimumSize(th().px(680), th().px(460));

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto *body = new QHBoxLayout();
    body->setSpacing(0);
    outer->addLayout(body, 1);

    m_nav = new QListWidget(this);
    m_nav->setObjectName("settingsNav");
    m_nav->setFixedWidth(th().px(208));
    m_nav->setIconSize(QSize(th().px(17), th().px(17)));
    m_nav->setAccessibleName(tr("Settings sections"));
    body->addWidget(m_nav);

    m_pages = new QStackedWidget(this);
    m_pages->setObjectName("settingsPage");
    body->addWidget(m_pages, 1);

    struct P { const char *id; const char *icon; QString title; QWidget *(SettingsDialog::*build)(); };
    const QList<P> pages = {
        {"appearance", "palette", tr("Appearance"), &SettingsDialog::buildAppearance},
        {"session", "sliders-horizontal", tr("Session"), &SettingsDialog::buildSession},
        {"agent", "bot", tr("Hermes Agent"), &SettingsDialog::buildAgent},
        {"connection", "network", tr("Connection"), &SettingsDialog::buildConnection},
        {"config", "file-code", tr("Configuration"), &SettingsDialog::buildConfig},
        {"shortcuts", "keyboard", tr("Shortcuts"), &SettingsDialog::buildShortcuts},
    };
    for (const auto &p : pages) {
        auto *item = new QListWidgetItem(th().icon(p.icon), p.title, m_nav);
        item->setData(Qt::UserRole, QString::fromLatin1(p.id));
        item->setSizeHint(QSize(0, th().px(36)));
        m_pageIds << QString::fromLatin1(p.id);
        m_pages->addWidget((this->*p.build)());
    }
    connect(m_nav, &QListWidget::currentRowChanged, this, [this](int row) {
        m_pages->setCurrentIndex(row);
        if (m_pageIds.value(row) == "config" && !m_configLoaded && !m_configLoading) loadConfiguration();
        if (m_pageIds.value(row) == "agent") refreshAgent();
    });

    auto *footer = new QWidget(this);
    footer->setStyleSheet(QStringLiteral("QWidget#settingsFooter { border-top: 1px solid %1; }")
                              .arg(th().tokens().border.name()));
    footer->setObjectName("settingsFooter");
    auto *fl = new QHBoxLayout(footer);
    fl->setContentsMargins(th().px(16), th().px(10), th().px(16), th().px(12));
    fl->addWidget(label(tr("Changes apply immediately."), "faint", footer), 1);
    auto *close = new QPushButton(tr("Done"), footer);
    close->setProperty("role", "primary");
    close->setDefault(true);
    ui::describe(close, tr("Done"), ui::withShortcut(tr("Close settings"), QKeySequence(Qt::Key_Escape)));
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    fl->addWidget(close);
    outer->addWidget(footer);

    if (m_client) {
        connect(m_client, &AcpClient::configOptionsChanged, this, &SettingsDialog::rebuildSession);
        connect(m_client, &AcpClient::modesChanged, this, &SettingsDialog::rebuildSession);
        connect(m_client, &AcpClient::ready, this, &SettingsDialog::rebuildSession);
        connect(m_client, &AcpClient::sessionStopped, this, &SettingsDialog::rebuildSession);
        connect(m_client, &AcpClient::connected, this, &SettingsDialog::refreshAgent);
        connect(m_client, &AcpClient::processExited, this, &SettingsDialog::refreshAgent);
    }
    m_nav->setCurrentRow(0);
}

void SettingsDialog::showPage(const QString &id) {
    const int idx = m_pageIds.indexOf(id);
    if (idx >= 0) m_nav->setCurrentRow(idx);
}

QString SettingsDialog::currentPage() const { return m_pageIds.value(m_nav->currentRow()); }

QWidget *SettingsDialog::page(const QString &title, const QString &subtitle, QWidget **content,
                              QVBoxLayout **layout) {
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *w = new QWidget(scroll);
    w->setObjectName("settingsPage");
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(th().px(28), th().px(24), th().px(28), th().px(24));
    l->setSpacing(th().px(14));
    auto *t = new QLabel(title, w);
    t->setProperty("role", "title");
    l->addWidget(t);
    if (!subtitle.isEmpty()) l->addWidget(label(subtitle, "muted", w));
    l->addSpacing(th().px(4));
    scroll->setWidget(w);
    *content = w;
    *layout = l;
    return scroll;
}

// ─── Appearance ───────────────────────────────────────────────────────────

QWidget *SettingsDialog::buildAppearance() {
    QWidget *w;
    QVBoxLayout *l;
    QWidget *pageWidget = page(tr("Appearance"), tr("How Consiglio looks. Theme and size follow you across launches."), &w, &l);

    QVBoxLayout *cl;
    auto *themeCard = card(w, &cl);
    auto *themeRow = new QWidget(themeCard);
    auto *tr_ = new QHBoxLayout(themeRow);
    tr_->setContentsMargins(0, 0, 0, 0);
    tr_->setSpacing(th().px(6));
    m_themeGroup = new QButtonGroup(this);
    struct T { Theme::Mode mode; const char *icon; QString text; QString tip; };
    const QList<T> modes = {
        {Theme::Mode::System, "monitor", tr("System"), tr("Follow the desktop's light/dark setting")},
        {Theme::Mode::Light, "sun", tr("Light"), tr("Always use the light theme")},
        {Theme::Mode::Dark, "moon", tr("Dark"), tr("Always use the dark theme")},
    };
    for (const auto &m : modes) {
        auto *b = new QPushButton(th().icon(m.icon), m.text, themeRow);
        b->setCheckable(true);
        b->setProperty("role", "chip");
        b->setCursor(Qt::PointingHandCursor);
        b->setChecked(th().mode() == m.mode);
        ui::describe(b, tr("%1 theme").arg(m.text), m.tip);
        m_themeGroup->addButton(b, int(m.mode));
        tr_->addWidget(b);
    }
    connect(m_themeGroup, QOverload<int>::of(&QButtonGroup::buttonClicked), this,
            [](int id) { th().setMode(Theme::Mode(id)); });
    cl->addWidget(settingRow(tr("Theme"), tr("Light, dark, or match your desktop."), themeRow, themeCard));
    cl->addWidget(divider(themeCard));

    auto *scaleBox = new QWidget(themeCard);
    auto *sl = new QHBoxLayout(scaleBox);
    sl->setContentsMargins(0, 0, 0, 0);
    m_scale = new QSlider(Qt::Horizontal, scaleBox);
    m_scale->setRange(85, 160);
    m_scale->setSingleStep(5);
    m_scale->setPageStep(10);
    m_scale->setValue(qRound(th().scale() * 100));
    m_scale->setFixedWidth(th().px(200));
    ui::describe(m_scale, tr("Interface size"), ui::withShortcut(tr("Zoom in"), QKeySequence::ZoomIn));
    m_scaleLabel = new QLabel(QStringLiteral("%1%").arg(m_scale->value()), scaleBox);
    m_scaleLabel->setMinimumWidth(th().px(44));
    m_scaleLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    sl->addWidget(m_scale);
    sl->addWidget(m_scaleLabel);
    connect(m_scale, &QSlider::valueChanged, this, [this](int v) {
        m_scaleLabel->setText(QStringLiteral("%1%").arg(v));
    });
    connect(m_scale, &QSlider::sliderReleased, this, [this]() { th().setScale(m_scale->value() / 100.0); });
    connect(m_scale, &QSlider::actionTriggered, this, [this](int action) {
        if (action != QAbstractSlider::SliderMove) {
            QTimer::singleShot(0, this, [this]() { th().setScale(m_scale->value() / 100.0); });
        }
    });
    cl->addWidget(settingRow(tr("Interface size"), tr("Scales all text and controls (Ctrl + / Ctrl − / Ctrl 0)."),
                             scaleBox, themeCard));
    l->addWidget(themeCard);

    l->addWidget(label(tr("CONVERSATION TEXT"), "section", w));
    auto *textCard = card(w, &cl);
    auto spin = [this](int value, const QString &name) {
        auto *s = new QSpinBox(this);
        s->setRange(10, 32);
        s->setSuffix(tr(" px"));
        s->setValue(value);
        s->setFixedWidth(th().px(96));
        s->setAccessibleName(name);
        return s;
    };
    m_chatPx = spin(m_fonts ? m_fonts->chatPx() : 16, tr("Conversation text size"));
    m_thinkingPx = spin(m_fonts ? m_fonts->thinkingPx() : 15, tr("Reasoning text size"));
    m_toolPx = spin(m_fonts ? m_fonts->toolPx() : 13, tr("Tool output text size"));
    cl->addWidget(settingRow(tr("Messages"), tr("Your prompts and Hermes's answers."), m_chatPx, textCard));
    cl->addWidget(divider(textCard));
    cl->addWidget(settingRow(tr("Reasoning"), tr("The collapsible “Thinking” stream."), m_thinkingPx, textCard));
    cl->addWidget(divider(textCard));
    cl->addWidget(settingRow(tr("Tool output"), tr("Commands Hermes runs and their output."), m_toolPx, textCard));
    if (m_fonts) {
        connect(m_chatPx, QOverload<int>::of(&QSpinBox::valueChanged), m_fonts, &FontSettings::setChatPx);
        connect(m_thinkingPx, QOverload<int>::of(&QSpinBox::valueChanged), m_fonts, &FontSettings::setThinkingPx);
        connect(m_toolPx, QOverload<int>::of(&QSpinBox::valueChanged), m_fonts, &FontSettings::setToolPx);
    }
    auto *resetRow = new QHBoxLayout();
    resetRow->addStretch();
    auto *reset = button(tr("Restore Defaults"), "rotate-ccw", tr("Reset theme, size and text sizes"), textCard, "ghost");
    connect(reset, &QPushButton::clicked, this, [this]() {
        m_chatPx->setValue(16);
        m_thinkingPx->setValue(15);
        m_toolPx->setValue(13);
        m_scale->setValue(100);
        th().setScale(1.0);
    });
    resetRow->addWidget(reset);
    cl->addLayout(resetRow);
    l->addWidget(textCard);
    l->addStretch(1);
    return pageWidget;
}

// ─── Session ──────────────────────────────────────────────────────────────

QWidget *SettingsDialog::buildSession() {
    QWidget *w;
    QVBoxLayout *l;
    QWidget *pageWidget = page(tr("Session"),
                               tr("Options Hermes offers for the current session. The list comes from Hermes "
                                  "itself, so new options appear here after an update."),
                               &w, &l);
    m_sessionContent = new QWidget(w);
    m_sessionLayout = new QVBoxLayout(m_sessionContent);
    m_sessionLayout->setContentsMargins(0, 0, 0, 0);
    m_sessionLayout->setSpacing(th().px(12));
    l->addWidget(m_sessionContent);
    l->addStretch(1);
    rebuildSession();
    return pageWidget;
}

void SettingsDialog::rebuildSession() {
    if (!m_sessionLayout) return;
    while (QLayoutItem *item = m_sessionLayout->takeAt(0)) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    QVBoxLayout *cl;
    auto *c = card(m_sessionContent, &cl);
    if (!m_client || !m_client->isReady()) {
        auto *row = new QHBoxLayout();
        auto *icon = new QLabel(c);
        icon->setPixmap(th().pixmap("message-square", th().px(20)));
        row->addWidget(icon, 0, Qt::AlignTop);
        auto *text = new QVBoxLayout();
        auto *t = new QLabel(tr("No active session"), c);
        t->setStyleSheet("font-weight: 600;");
        text->addWidget(t);
        text->addWidget(label(tr("Start a session to choose the model, reasoning effort and approval mode."),
                              "muted", c));
        row->addLayout(text, 1);
        cl->addLayout(row);
        m_sessionLayout->addWidget(c);
        return;
    }
    const auto options = m_client->configOptions();
    bool first = true;
    for (const auto &opt : options) {
        if (opt.type != "select" || opt.choices.isEmpty()) continue;
        if (!first) cl->addWidget(divider(c));
        first = false;
        auto *combo = new ui::ElidedCombo(c);
        combo->setMinimumWidth(th().px(200));
        combo->setMaximumWidth(th().px(280));
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(14);
        QString lastGroup;
        for (const auto &choice : opt.choices) {
            if (!choice.group.isEmpty() && choice.group != lastGroup) {
                combo->insertSeparator(combo->count());
                lastGroup = choice.group;
            }
            const QString text = ui::displayChoice(choice.name.isEmpty() ? choice.value : choice.name);
            combo->addItem(text, choice.value);
            const QString tip = choice.description.isEmpty() ? text : text + QStringLiteral(" — ") + choice.description;
            combo->setItemData(combo->count() - 1, tip, Qt::ToolTipRole);
        }
        combo->setCurrentIndex(qMax(0, combo->findData(opt.currentValue)));
        combo->setAccessibleName(opt.name);
        combo->setToolTip(combo->currentText());
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), combo,
                [combo]() { combo->setToolTip(combo->currentText()); });
        const QString id = opt.id;
        connect(combo, QOverload<int>::of(&QComboBox::activated), this, [this, combo, id](int idx) {
            m_client->setConfigOption(id, combo->itemData(idx).toString());
        });
        cl->addWidget(settingRow(opt.name.isEmpty() ? opt.id : opt.name, opt.description, combo, c));
    }
    // Legacy modes, only when no config option covers the mode.
    bool hasModeOption = false;
    for (const auto &opt : options) hasModeOption |= opt.category == "mode";
    const auto modes = m_client->sessionModes();
    if (!hasModeOption && !modes.isEmpty()) {
        if (!first) cl->addWidget(divider(c));
        first = false;
        auto *combo = new ui::ElidedCombo(c);
        combo->setMaximumWidth(th().px(280));
        combo->setAccessibleName(tr("Mode"));
        for (const auto &m : modes) combo->addItem(ui::displayChoice(m.name.isEmpty() ? m.id : m.name), m.id);
        combo->setCurrentIndex(qMax(0, combo->findData(m_client->currentModeId())));
        connect(combo, QOverload<int>::of(&QComboBox::activated), this,
                [this, combo](int idx) { m_client->setMode(combo->itemData(idx).toString()); });
        cl->addWidget(settingRow(tr("Mode"), tr("How Hermes handles edits and approvals."), combo, c));
    }
    if (first) cl->addWidget(label(tr("This Hermes version doesn't offer session options over ACP."), "muted", c));
    m_sessionLayout->addWidget(c);

    QVBoxLayout *il;
    auto *info = card(m_sessionContent, &il);
    auto *grid = new QFormLayout();
    grid->setHorizontalSpacing(th().px(16));
    grid->setVerticalSpacing(th().px(6));
    auto mono = [info](const QString &text) {
        auto *v = new QLabel(ui::softWrap(text), info);
        v->setToolTip(text);   // soft-wrapped for display; the tooltip has the exact value
        v->setProperty("role", "mono");
        v->setWordWrap(true);
        return v;
    };
    grid->addRow(label(tr("Session ID"), "muted", info), mono(m_client->sessionId()));
    grid->addRow(label(tr("Workspace"), "muted", info), mono(m_client->sessionCwd()));
    il->addLayout(grid);
    m_sessionLayout->addWidget(info);
}

// ─── Hermes Agent ─────────────────────────────────────────────────────────

QWidget *SettingsDialog::buildAgent() {
    QWidget *w;
    QVBoxLayout *l;
    QWidget *pageWidget = page(tr("Hermes Agent"),
                               tr("The Hermes install Consiglio talks to. Hermes updates itself; Consiglio "
                                  "finds it again on every launch and reconnects after an update."),
                               &w, &l);
    QVBoxLayout *cl;
    auto *status = card(w, &cl);
    auto *top = new QHBoxLayout();
    top->setSpacing(th().px(10));
    m_agentDot = new ui::StatusDot(status);
    top->addWidget(m_agentDot);
    m_agentStatus = new QLabel(status);
    m_agentStatus->setProperty("role", "heading");
    m_agentStatus->setWordWrap(true);
    top->addWidget(m_agentStatus, 1);
    auto *restart = button(tr("Restart Agent"), "refresh-cw",
                           tr("Relaunch Hermes (picks up a new version) and reopen the session"), status);
    connect(restart, &QPushButton::clicked, this, [this]() { emit restartRequested(); });
    top->addWidget(restart);
    cl->addLayout(top);
    m_agentDetail = label(QString(), "muted", status);
    cl->addWidget(m_agentDetail);
    m_capsRow = new QWidget(status);
    new ui::FlowLayout(m_capsRow, th().px(6), th().px(6));
    cl->addWidget(m_capsRow);
    l->addWidget(status);

    l->addWidget(label(tr("UPDATES & HEALTH"), "section", w));
    auto *health = card(w, &cl);
    auto *hrowBox = new QWidget(health);
    auto *hrow = new ui::FlowLayout(hrowBox, th().px(8), th().px(8));
    hrow->setContentsMargins(0, 0, 0, 0);
    auto *check = button(tr("Check for Updates"), "download", tr("hermes update --check"), health);
    auto *update = button(tr("Update Hermes…"), "rocket", tr("Run hermes update, then restart the agent"), health);
    auto *doctor = button(tr("Run Doctor"), "heart-pulse", tr("hermes doctor — diagnose the install"), health);
    auto *statusBtn = button(tr("Status"), "activity", tr("hermes status"), health);
    for (auto *b : {check, update, doctor, statusBtn}) {
        hrow->addWidget(b);
        m_cliButtons << b;
    }
    cl->addWidget(hrowBox);
    connect(check, &QPushButton::clicked, this, [this]() {
        runToOutput(tr("Checking for updates…"), {"update", "--check"}, 120000);
    });
    connect(doctor, &QPushButton::clicked, this, [this]() { runToOutput(tr("Running hermes doctor…"), {"doctor"}); });
    connect(statusBtn, &QPushButton::clicked, this, [this]() { runToOutput(tr("hermes status"), {"status"}); });
    connect(update, &QPushButton::clicked, this, [this]() {
        const auto answer = QMessageBox::question(
            this, tr("Update Hermes"),
            tr("Run “hermes update” now? Hermes pulls the latest version and reinstalls its dependencies. "
               "Consiglio restarts the agent afterwards and reopens your session."),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes);
        if (answer != QMessageBox::Yes) return;
        runToOutput(tr("Updating Hermes…"), {"update", "--yes"}, 20 * 60 * 1000, [this](int code) {
            if (code == 0) emit restartRequested();
        });
    });

    m_outputCard = new QWidget(health);
    auto *ol = new QVBoxLayout(m_outputCard);
    ol->setContentsMargins(0, th().px(4), 0, 0);
    ol->setSpacing(th().px(6));
    auto *orow = new QHBoxLayout();
    m_outputSpinner = new ui::Spinner(m_outputCard, 14);
    orow->addWidget(m_outputSpinner);
    m_outputTitle = new QLabel(m_outputCard);
    m_outputTitle->setProperty("role", "muted");
    orow->addWidget(m_outputTitle, 1);
    ol->addLayout(orow);
    m_output = new QPlainTextEdit(m_outputCard);
    m_output->setReadOnly(true);
    m_output->setMinimumHeight(th().px(150));
    m_output->setAccessibleName(tr("Command output"));
    QFont mono(QStringLiteral("monospace"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPixelSize(th().px(12));
    m_output->setFont(mono);
    ol->addWidget(m_output);
    m_outputCard->setVisible(false);
    cl->addWidget(m_outputCard);
    l->addWidget(health);

    l->addWidget(label(tr("PROVIDER & MODEL"), "section", w));
    auto *prov = card(w, &cl);
    m_authLabel = label(QString(), "muted", prov);
    cl->addWidget(m_authLabel);
    auto *prowBox = new QWidget(prov);
    auto *prow = new ui::FlowLayout(prowBox, th().px(8), th().px(8));
    prow->setContentsMargins(0, 0, 0, 0);
    auto *setup = button(tr("Configure Provider…"), "log-in",
                         tr("Open Hermes's provider setup in a terminal"), prov, "primary");
    auto *model = button(tr("Choose Default Model…"), "cpu", tr("hermes model, in a terminal"), prov);
    auto *wizard = button(tr("Setup Wizard…"), "wand-sparkles", tr("hermes setup, in a terminal"), prov);
    prow->addWidget(setup);
    prow->addWidget(model);
    prow->addWidget(wizard);
    cl->addWidget(prowBox);
    connect(setup, &QPushButton::clicked, this, [this]() {
        QStringList args{"setup"};
        if (m_client) {
            for (const auto &m : m_client->authMethods()) {
                if (m.type == "terminal" && !m.args.isEmpty()) { args = m.args; break; }
            }
        }
        openTerminal(args);
    });
    connect(model, &QPushButton::clicked, this, [this]() { openTerminal({"model"}); });
    connect(wizard, &QPushButton::clicked, this, [this]() { openTerminal({"setup"}); });
    l->addWidget(prov);

    l->addWidget(label(tr("EXECUTABLE"), "section", w));
    auto *exe = card(w, &cl);
    m_resolved = new QLabel(exe);
    m_resolved->setProperty("role", "mono");
    m_resolved->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_resolved->setWordWrap(true);
    m_resolved->setMinimumWidth(1);   // long paths have no break points; let the label wrap anywhere
    m_resolved->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    cl->addWidget(settingRow(tr("In use"), QString(), nullptr, exe));
    cl->addWidget(m_resolved);
    cl->addWidget(divider(exe));
    auto *overrideRow = new QWidget(exe);
    auto *orl = new QHBoxLayout(overrideRow);
    orl->setContentsMargins(0, 0, 0, 0);
    orl->setSpacing(th().px(6));
    m_override = new QLineEdit(HermesCli::executableOverride(), overrideRow);
    m_override->setPlaceholderText(tr("Automatic — $HERMES_BIN, then PATH (recommended)"));
    m_override->setClearButtonEnabled(true);
    ui::describe(m_override, tr("Hermes executable override"),
                 tr("Leave empty so Consiglio follows `hermes update` automatically"));
    auto *browse = button(tr("Browse…"), "folder-open", tr("Choose the hermes executable"), overrideRow);
    orl->addWidget(m_override, 1);
    orl->addWidget(browse);
    cl->addWidget(label(tr("Override (optional)"), QString(), exe));
    cl->addWidget(overrideRow);
    cl->addWidget(label(tr("Pinning a path stops Consiglio from following updates that move Hermes. "
                           "Takes effect on the next agent restart."), "faint", exe));
    cl->addWidget(divider(exe));
    auto *redetect = button(tr("Detect Hermes again…"), "search", tr("Show the first-run Hermes setup again: "
                            "find an existing Hermes and choose how to use it"), exe);
    cl->addWidget(settingRow(tr("First-run setup"),
                             tr("Finds an existing Hermes and asks before installing or changing anything."),
                             redetect, exe));
    connect(redetect, &QPushButton::clicked, this, [this]() {
        const auto env = HermesSetup::Environment::current();
        HermesSetupDialog dlg(env, HermesSetup::detect(env), this);
        if (dlg.exec() == QDialog::Accepted || dlg.savedChoice() != HermesSetup::Choice::None) {
            m_override->setText(HermesCli::executableOverride());
            refreshAgent();
            emit executableChanged();
        }
    });
    connect(browse, &QPushButton::clicked, this, [this]() {
        const QString f = QFileDialog::getOpenFileName(this, tr("Choose the hermes executable"),
                                                       QFileInfo(HermesCli::executable()).absolutePath());
        if (!f.isEmpty()) {
            m_override->setText(f);
            emit m_override->editingFinished();
        }
    });
    connect(m_override, &QLineEdit::editingFinished, this, [this]() {
        if (m_override->text().trimmed() == HermesCli::executableOverride()) return;
        HermesCli::setExecutableOverride(m_override->text());
        refreshAgent();
        emit executableChanged();
    });
    cl->addWidget(divider(exe));
    m_autoRestart = new QCheckBox(tr("Reconnect automatically"), exe);
    m_autoRestart->setChecked(QSettings().value("hermes/autoRestart", true).toBool());
    ui::describe(m_autoRestart, tr("Reconnect automatically"),
                 tr("Relaunch Hermes with backoff and reopen the session if it exits unexpectedly"));
    connect(m_autoRestart, &QCheckBox::toggled, this, [this](bool on) {
        QSettings().setValue("hermes/autoRestart", on);
        emit autoRestartChanged(on);
    });
    cl->addWidget(m_autoRestart);
    cl->addWidget(label(tr("If Hermes exits (for example while it updates itself), Consiglio relaunches it "
                           "and reopens your session."), "faint", exe));
    l->addWidget(exe);
    l->addStretch(1);
    refreshAgent();
    return pageWidget;
}

void SettingsDialog::refreshAgent() {
    if (!m_agentStatus) return;
    const bool connected = m_client && m_client->isConnected();
    m_agentDot->setState(connected ? "ok" : "error");
    if (connected) {
        const QString name = m_client->agentName().isEmpty() ? tr("Hermes") : m_client->agentName();
        m_agentStatus->setText(m_client->agentVersion().isEmpty()
                                   ? tr("Connected to %1").arg(name)
                                   : tr("Connected to %1 %2").arg(name, m_client->agentVersion()));
        m_agentDetail->setText(tr("ACP protocol v%1 · %2")
                                   .arg(m_client->agentProtocolVersion())
                                   .arg(m_client->isReady() ? tr("session active") : tr("no session")));
    } else {
        m_agentStatus->setText(tr("Not connected"));
        m_agentDetail->setText(tr("Consiglio couldn't reach Hermes. Check that it's installed, then restart the agent."));
    }
    // Capabilities as badges.
    auto *flow = m_capsRow->layout();
    while (QLayoutItem *item = flow->takeAt(0)) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    if (connected) {
        const QJsonObject caps = m_client->agentCapabilities();
        QStringList names;
        if (caps.value("loadSession").toBool()) names << tr("Load sessions");
        const QJsonObject sc = caps.value("sessionCapabilities").toObject();
        if (sc.contains("resume")) names << tr("Resume");
        if (sc.contains("list")) names << tr("Session list");
        if (sc.contains("fork")) names << tr("Fork");
        if (caps.value("promptCapabilities").toObject().value("image").toBool()) names << tr("Images");
        for (const QString &n : names) {
            auto *b = new QLabel(n, m_capsRow);
            b->setProperty("role", "badge");
            flow->addWidget(b);
        }
    }
    const QString resolvedPath = m_client && !m_client->resolvedExecutable().isEmpty() ? m_client->resolvedExecutable()
                                                                                       : HermesCli::executable();
    // Zero-width spaces after separators let a long path wrap instead of
    // widening the page.
    m_resolved->setText(QString(resolvedPath).replace(QLatin1Char('/'), QStringLiteral("/\u200B")));
    m_resolved->setToolTip(resolvedPath);
    QString auth;
    if (m_client) {
        for (const auto &m : m_client->authMethods()) {
            auth = m.description.isEmpty() ? m.name : m.name + QStringLiteral(" — ") + m.description;
            break;
        }
    }
    m_authLabel->setText(auth.isEmpty()
                             ? tr("Hermes needs a model provider (an API key, a provider login, or a local "
                                  "OpenAI-compatible server). Setup runs in a terminal.")
                             : auth);
}

void SettingsDialog::runToOutput(const QString &title, const QStringList &args, int timeoutMs,
                                 std::function<void(int)> after) {
    m_outputCard->setVisible(true);
    m_outputTitle->setText(title);
    m_outputSpinner->setVisible(true);
    m_outputSpinner->setRunning(true);
    m_output->setPlainText(QStringLiteral("$ hermes %1\n").arg(args.join(' ')));
    for (auto *b : m_cliButtons) b->setEnabled(false);
    QPointer<SettingsDialog> self(this);
    HermesCli::run(args, this,
        [self, title, after](int code, const QString &) {
            if (!self) return;
            self->m_outputSpinner->setRunning(false);
            self->m_outputSpinner->setVisible(false);
            self->m_outputTitle->setText(code == 0 ? tr("%1 — finished").arg(title.chopped(title.endsWith("…") ? 1 : 0))
                                                   : tr("%1 — exited with code %2")
                                                         .arg(title.chopped(title.endsWith("…") ? 1 : 0)).arg(code));
            for (auto *b : self->m_cliButtons) b->setEnabled(true);
            if (after) after(code);
        },
        timeoutMs,
        [self](const QString &chunk) {
            if (!self) return;
            self->m_output->moveCursor(QTextCursor::End);
            self->m_output->insertPlainText(ui::maskSecrets(chunk));
            self->m_output->moveCursor(QTextCursor::End);
        });
}

void SettingsDialog::openTerminal(const QStringList &args) {
    QString error;
    if (!HermesCli::openInTerminal(args, &error)) {
        QMessageBox box(QMessageBox::Information, tr("Open a terminal"), error, QMessageBox::Ok, this);
        box.setTextInteractionFlags(Qt::TextSelectableByMouse);
        box.exec();
    }
}

// ─── Connection (remote Hermes over SSH) ─────────────────────────────────

QWidget *SettingsDialog::buildConnection() {
    QWidget *w;
    QVBoxLayout *l;
    QWidget *pageWidget = page(tr("Connection"),
                               tr("Where Hermes runs. A local install is the default; point Consiglio at "
                                  "another machine to use that machine's Hermes over SSH."),
                               &w, &l);

    QVBoxLayout *cl;
    auto *c = card(w, &cl);
    m_remoteEnabled = new QCheckBox(tr("Run Hermes on another machine (SSH)"), c);
    m_remoteEnabled->setChecked(HermesRemote::enabled());
    ui::describe(m_remoteEnabled, tr("Remote Hermes"),
                 tr("Connect to `hermes acp` on a remote host instead of a local install"));
    cl->addWidget(m_remoteEnabled);
    cl->addWidget(divider(c));

    m_remoteDestination = new QLineEdit(HermesRemote::destination(), c);
    m_remoteDestination->setPlaceholderText(tr("user@host, or a host from ~/.ssh/config"));
    m_remoteDestination->setClearButtonEnabled(true);
    ui::describe(m_remoteDestination, tr("SSH destination"),
                 tr("Anything ssh accepts as a host: user@host, a Host alias, or an address"));
    cl->addWidget(settingRow(tr("Destination"), tr("The machine that runs Hermes."), m_remoteDestination, c));

    m_remoteOptions = new QLineEdit(HermesRemote::optionsLine(), c);
    m_remoteOptions->setPlaceholderText(tr("-p 2222 -i ~/.ssh/id_ed25519"));
    m_remoteOptions->setClearButtonEnabled(true);
    ui::describe(m_remoteOptions, tr("Extra ssh options"),
                 tr("Optional ssh arguments, exactly as you would type them after `ssh`"));
    cl->addWidget(settingRow(tr("SSH options"), tr("Optional: port, identity file, jump host."),
                             m_remoteOptions, c));

    m_remoteLauncher = new QLineEdit(HermesRemote::launcher(), c);
    m_remoteLauncher->setPlaceholderText(QStringLiteral("hermes"));
    m_remoteLauncher->setClearButtonEnabled(true);
    ui::describe(m_remoteLauncher, tr("Remote launcher"),
                 tr("The command to look up in the remote login shell"));
    cl->addWidget(settingRow(tr("Launcher"), tr("Consiglio looks it up on every connection; nothing is pinned."),
                             m_remoteLauncher, c));

    auto *row = new QWidget(c);
    auto *rl = new QHBoxLayout(row);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(th().px(8));
    auto *test = button(tr("Test Connection"), "network",
                        tr("Run the lookup over SSH and report what it found"), row);
    m_remoteSpinner = new ui::Spinner(row, 14);
    m_remoteSpinner->setVisible(false);
    m_remoteResult = label(QString(), "faint", row);
    m_remoteResult->setWordWrap(true);
    m_remoteResult->setTextInteractionFlags(Qt::TextSelectableByMouse);
    rl->addWidget(test);
    rl->addWidget(m_remoteSpinner);
    rl->addWidget(m_remoteResult, 1);
    cl->addWidget(row);
    l->addWidget(c);

    l->addWidget(label(tr("Hermes keeps its own configuration, keys and sessions on the machine it runs on; "
                          "Consiglio never copies them. Approvals and sessions behave the same way one hop away. "
                          "SSH must already work non-interactively (key-based, no password prompt)."),
                       "faint", w));
    l->addStretch(1);

    auto applyState = [this, test]() {
        const bool on = m_remoteEnabled->isChecked();
        m_remoteDestination->setEnabled(on);
        m_remoteOptions->setEnabled(on);
        m_remoteLauncher->setEnabled(on);
        test->setEnabled(on && !m_remoteDestination->text().trimmed().isEmpty());
    };
    applyState();

    connect(m_remoteEnabled, &QCheckBox::toggled, this, [this, applyState](bool on) {
        HermesRemote::setEnabled(on);
        applyState();
        emit connectionChanged();
    });
    auto persist = [this]() {
        HermesRemote::setDestination(m_remoteDestination->text());
        HermesRemote::setOptions(m_remoteOptions->text());
        HermesRemote::setLauncher(m_remoteLauncher->text());
        emit connectionChanged();
    };
    connect(m_remoteDestination, &QLineEdit::editingFinished, this, persist);
    connect(m_remoteOptions, &QLineEdit::editingFinished, this, persist);
    connect(m_remoteLauncher, &QLineEdit::editingFinished, this, persist);
    connect(m_remoteDestination, &QLineEdit::textChanged, this, [applyState]() { applyState(); });
    connect(test, &QPushButton::clicked, this, &SettingsDialog::testConnection);

    return pageWidget;
}

void SettingsDialog::testConnection() {
    if (!m_remoteResult) return;
    const QString dest = m_remoteDestination->text().trimmed();
    if (dest.isEmpty()) {
        m_remoteResult->setText(tr("Enter a destination first (user@host)."));
        return;
    }
    // Persist what is on screen before probing, so a passing test is the saved one.
    HermesRemote::setDestination(dest);
    HermesRemote::setOptions(m_remoteOptions->text());
    HermesRemote::setLauncher(m_remoteLauncher->text());

    m_remoteResult->setText(tr("Looking for hermes on %1…").arg(dest));
    m_remoteSpinner->setVisible(true);
    m_remoteSpinner->setRunning(true);
    const QStringList opts = HermesRemote::options();
    HermesRemote::probeAsync(this, dest, opts, [this](const RemoteProbe &probe) {
        if (!m_remoteResult) return;
        m_remoteSpinner->setRunning(false);
        m_remoteSpinner->setVisible(false);
        m_remoteResult->setText(probe.ok ? tr("Found: %1").arg(probe.text())
                                         : tr("Not usable: %1").arg(probe.text()));
    });
}

// ─── Configuration ────────────────────────────────────────────────────────

QWidget *SettingsDialog::buildConfig() {
    QWidget *w;
    QVBoxLayout *l;
    QWidget *pageWidget = page(tr("Configuration"),
                               tr("Hermes's own settings (config.yaml), as reported by “hermes config show”. "
                                  "API keys are never displayed."),
                               &w, &l);
    auto *bar = new QHBoxLayout();
    bar->setSpacing(th().px(8));
    m_configState = label(QString(), "faint", w);
    bar->addWidget(m_configState, 1);
    auto *refresh = button(tr("Refresh"), "refresh-cw", tr("Reload hermes config show"), w);
    auto *open = button(tr("Open config.yaml"), "external-link", tr("Open Hermes's config file in your editor"), w);
    bar->addWidget(refresh);
    bar->addWidget(open);
    l->addLayout(bar);
    connect(refresh, &QPushButton::clicked, this, &SettingsDialog::loadConfiguration);
    connect(open, &QPushButton::clicked, this, [this]() {
        HermesCli::run({"config", "path"}, this, [this](int code, const QString &out) {
            const QString path = out.trimmed().section('\n', -1).trimmed();
            if (code == 0 && QFileInfo::exists(path)) QDesktopServices::openUrl(QUrl::fromLocalFile(path));
            else QMessageBox::information(this, tr("Config file"), tr("Couldn't locate config.yaml:\n%1").arg(out));
        }, 20000);
    });

    QVBoxLayout *sl;
    auto *setCard = card(w, &sl);
    sl->addWidget(label(tr("Change a setting"), "heading", setCard));
    sl->addWidget(label(tr("Uses “hermes config set”. Keys are dotted paths, e.g. model.default, "
                           "display.personality, terminal.timeout."), "muted", setCard));
    auto *row = new QHBoxLayout();
    row->setSpacing(th().px(8));
    m_setKey = new QLineEdit(setCard);
    m_setKey->setPlaceholderText(tr("Key, e.g. model.default"));
    ui::describe(m_setKey, tr("Setting key"));
    m_setValue = new QLineEdit(setCard);
    m_setValue->setPlaceholderText(tr("Value"));
    ui::describe(m_setValue, tr("Setting value"));
    auto *apply = button(tr("Set"), "check", tr("Run hermes config set <key> <value>"), setCard, "primary");
    row->addWidget(m_setKey, 2);
    row->addWidget(m_setValue, 3);
    row->addWidget(apply);
    sl->addLayout(row);
    m_setResult = label(QString(), "faint", setCard);
    m_setResult->setVisible(false);
    sl->addWidget(m_setResult);
    auto doSet = [this]() {
        const QString key = m_setKey->text().trimmed();
        if (key.isEmpty()) {
            m_setKey->setFocus();
            return;
        }
        if (key.contains(QRegularExpression(QStringLiteral("(?i)(api[_-]?key|token|secret|password)")))) {
            m_setResult->setText(tr("Secrets aren't set from here — use Configure Provider… on the Hermes "
                                    "Agent page so keys never pass through this window."));
            m_setResult->setVisible(true);
            return;
        }
        m_setResult->setText(tr("Saving…"));
        m_setResult->setVisible(true);
        HermesCli::run({"config", "set", key, m_setValue->text()}, this, [this, key](int code, const QString &out) {
            m_setResult->setText(code == 0 ? tr("✓ Saved %1. New sessions use it; some settings need an agent restart.").arg(key)
                                           : tr("Couldn't set %1: %2").arg(key, out.trimmed().right(300)));
            if (code == 0) loadConfiguration();
        }, 30000);
    };
    connect(apply, &QPushButton::clicked, this, doSet);
    connect(m_setValue, &QLineEdit::returnPressed, this, doSet);
    l->addWidget(setCard);

    m_configSections = new QWidget(w);
    m_configLayout = new QVBoxLayout(m_configSections);
    m_configLayout->setContentsMargins(0, 0, 0, 0);
    m_configLayout->setSpacing(th().px(12));
    l->addWidget(m_configSections);
    l->addStretch(1);
    return pageWidget;
}

void SettingsDialog::loadConfiguration() {
    if (m_configLoading) return;
    m_configLoading = true;
    m_configState->setText(tr("Loading…"));
    QPointer<SettingsDialog> self(this);
    HermesCli::run({"config", "show"}, this, [self](int code, const QString &out) {
        if (!self) return;
        self->m_configLoading = false;
        self->m_configLoaded = true;
        if (code != 0) {
            self->m_configState->setText(tr("Couldn't run hermes config show (exit %1).").arg(code));
            self->renderConfig(QString());
            return;
        }
        self->m_configState->setText(tr("Loaded from %1").arg(HermesCli::executable()));
        self->renderConfig(out);
    }, 30000);
}

void SettingsDialog::renderConfig(const QString &output) {
    while (QLayoutItem *item = m_configLayout->takeAt(0)) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    const auto sections = HermesCli::parseConfigShow(output);
    for (const auto &s : sections) {
        if (s.rows.isEmpty()) continue;
        m_configLayout->addWidget(label(s.title.toUpper(), "section", m_configSections));
        QVBoxLayout *cl;
        auto *c = card(m_configSections, &cl);
        auto *grid = new QGridLayout();
        grid->setHorizontalSpacing(th().px(18));
        grid->setVerticalSpacing(th().px(8));
        grid->setColumnStretch(1, 1);
        int r = 0;
        for (const auto &row : s.rows) {
            auto *k = new QLabel(row.key, c);
            k->setProperty("role", "muted");
            grid->addWidget(k, r, 0, Qt::AlignTop);
            if (row.secret || !row.set) {
                auto *badge = new QLabel(row.value, c);
                badge->setProperty("role", "badge");
                if (!row.set) badge->setStyleSheet(QStringLiteral("background: %1; color: %2;")
                                                       .arg(th().tokens().hover.name(), th().tokens().textMuted.name()));
                grid->addWidget(badge, r, 1, Qt::AlignLeft | Qt::AlignTop);
            } else {
                auto *v = new QLabel(row.value, c);
                v->setWordWrap(true);
                v->setTextInteractionFlags(Qt::TextSelectableByMouse);
                grid->addWidget(v, r, 1);
            }
            ++r;
        }
        cl->addLayout(grid);
        m_configLayout->addWidget(c);
    }
    if (sections.isEmpty()) {
        QVBoxLayout *cl;
        auto *c = card(m_configSections, &cl);
        cl->addWidget(label(output.trimmed().isEmpty() ? tr("No configuration to show.")
                                                       : ui::maskSecrets(output.trimmed()), "mono", c));
        m_configLayout->addWidget(c);
    }
}

// ─── Shortcuts ────────────────────────────────────────────────────────────

QWidget *SettingsDialog::buildShortcuts() {
    QWidget *w;
    QVBoxLayout *l;
    QWidget *pageWidget = page(tr("Keyboard Shortcuts"), tr("Everything in Consiglio is reachable from the keyboard."),
                               &w, &l);
    auto *holder = new QWidget(w);
    m_shortcutsLayout = new QVBoxLayout(holder);
    m_shortcutsLayout->setContentsMargins(0, 0, 0, 0);
    l->addWidget(holder);
    l->addStretch(1);
    return pageWidget;
}

void SettingsDialog::setShortcuts(const QList<QPair<QString, QString>> &shortcuts) {
    while (QLayoutItem *item = m_shortcutsLayout->takeAt(0)) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    QVBoxLayout *cl;
    auto *c = card(m_shortcutsLayout->parentWidget(), &cl);
    cl->setSpacing(th().px(6));
    bool first = true;
    for (const auto &s : shortcuts) {
        if (!first) cl->addWidget(divider(c));
        first = false;
        auto *row = new QHBoxLayout();
        row->addWidget(new QLabel(s.first, c), 1);
        for (const QString &part : s.second.split(QStringLiteral(", "))) {
            auto *k = new QLabel(part, c);
            k->setProperty("role", "kbd");
            row->addWidget(k);
        }
        cl->addLayout(row);
    }
    m_shortcutsLayout->addWidget(c);
}
