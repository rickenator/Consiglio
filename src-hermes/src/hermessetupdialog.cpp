#include "hermessetupdialog.h"

#include "hermescli.h"
#include "theme.h"
#include "uiutil.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace {
Theme &th() { return Theme::instance(); }

QLabel *text(const QString &s, const QString &role, QWidget *parent) {
    auto *l = new QLabel(s, parent);
    if (!role.isEmpty()) l->setProperty("role", role);
    l->setWordWrap(true);
    return l;
}

QLabel *mono(const QString &s, QWidget *parent) {
    auto *l = text(s, "mono", parent);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->setMinimumWidth(1);
    l->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    return l;
}

QFrame *card(QWidget *parent, QVBoxLayout **layout) {
    auto *c = new QFrame(parent);
    c->setProperty("role", "card");
    auto *l = new QVBoxLayout(c);
    l->setContentsMargins(th().px(16), th().px(14), th().px(16), th().px(14));
    l->setSpacing(th().px(8));
    *layout = l;
    return c;
}

QWidget *chip(const QString &icon, const QString &label, QWidget *parent) {
    auto *w = new QPushButton(th().icon(icon), label, parent);
    w->setProperty("role", "chip");
    w->setEnabled(true);
    w->setFocusPolicy(Qt::NoFocus);
    w->setIconSize(QSize(th().px(13), th().px(13)));
    w->setAttribute(Qt::WA_TransparentForMouseEvents);
    return w;
}

QWidget *header(const QString &icon, const QString &title, const QString &subtitle, QWidget *parent) {
    auto &t = th();
    auto *w = new QWidget(parent);
    auto *l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(t.px(12));
    auto *tile = new QLabel(w);
    const int s = t.px(40);
    tile->setFixedSize(s, s);
    tile->setAlignment(Qt::AlignCenter);
    const QColor soft = t.tokens().accentSoft;
    tile->setStyleSheet(QStringLiteral("background: rgba(%1,%2,%3,%4); border-radius: %5px;")
                            .arg(soft.red()).arg(soft.green()).arg(soft.blue())
                            .arg(QString::number(soft.alphaF() * 1.6, 'f', 3)).arg(t.px(10)));
    tile->setPixmap(t.pixmap(icon, t.px(20), t.tokens().accent));
    l->addWidget(tile, 0, Qt::AlignTop);
    auto *col = new QVBoxLayout();
    col->setSpacing(t.px(2));
    auto *ttl = new QLabel(title, w);
    ttl->setProperty("role", "heading");
    col->addWidget(ttl);
    col->addWidget(text(subtitle, "muted", w));
    l->addLayout(col, 1);
    return w;
}

bool isExecutable(const QString &p) {
    const QFileInfo fi(p);
    return fi.exists() && fi.isFile() && fi.isExecutable();
}

bool isInside(const QString &child, const QString &parent) {
    if (parent.isEmpty()) return false;
    const QString c = QDir::cleanPath(QFileInfo(child).absoluteFilePath());
    const QString p = QDir::cleanPath(QFileInfo(parent).absoluteFilePath());
    return c == p || c.startsWith(p + QLatin1Char('/'));
}
} // namespace

HermesSetupDialog::HermesSetupDialog(const HermesSetup::Environment &env, const HermesSetup::Detection &detection,
                                     QWidget *parent)
    : QDialog(parent), m_env(env), m_detection(detection) {
    setWindowTitle(tr("Set up Hermes Agent"));
    setObjectName(QStringLiteral("hermesSetupDialog"));
    setModal(true);
    setMinimumWidth(th().px(600));
    m_launcher = [](const HermesSetup::InstallCommand &c, QString *err) { return HermesCli::openInstallerInTerminal(c, err); };
    build();
    ui::fitOnShow(this);
}

void HermesSetupDialog::setInstallerLauncher(std::function<bool(const HermesSetup::InstallCommand &, QString *)> l) {
    m_launcher = std::move(l);
}

QRadioButton *HermesSetupDialog::radio(Option o) const {
    return m_group ? qobject_cast<QRadioButton *>(m_group->button(int(o))) : nullptr;
}

void HermesSetupDialog::build() {
    auto &t = th();
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(t.px(24), t.px(22), t.px(24), t.px(18));
    l->setSpacing(t.px(12));

    const auto &d = m_detection;
    QString subtitle;
    if (d.executableFound())
        subtitle = tr("Consiglio found a Hermes Agent on this computer. Choose how to use it — nothing is "
                      "installed or changed until you continue.");
    else if (d.dataExists)
        subtitle = tr("Consiglio found Hermes data but no hermes executable. Nothing is installed or changed "
                      "until you continue.");
    else
        subtitle = tr("Consiglio talks to Hermes Agent, which isn't installed yet.");
    l->addWidget(header(d.anythingFound() ? "shield-check" : "download",
                        d.executableFound() ? tr("Existing Hermes found") : tr("Set up Hermes Agent"),
                        subtitle, this));

    // What was found.
    m_details = new QWidget(this);
    auto *dl = new QVBoxLayout(m_details);
    dl->setContentsMargins(0, 0, 0, 0);
    l->addWidget(m_details);
    rebuildDetails();

    // Choices.
    m_group = new QButtonGroup(this);
    auto *choices = new QWidget(this);
    auto *cl = new QVBoxLayout(choices);
    cl->setContentsMargins(0, t.px(2), 0, 0);
    cl->setSpacing(t.px(6));
    auto addOption = [&](Option o, const QString &title, const QString &desc) {
        auto *r = new QRadioButton(title, choices);
        r->setStyleSheet("font-weight: 600;");
        ui::describe(r, title, desc);
        m_group->addButton(r, int(o));
        cl->addWidget(r);
        auto *dlab = text(desc, "muted", choices);
        dlab->setContentsMargins(t.px(26), 0, 0, t.px(2));
        cl->addWidget(dlab);
        return r;
    };

    const bool sepOk = HermesSetup::separateInstallSupported(m_env);
    const QString sepDesc = sepOk
        ? tr("Installs a second Hermes in its own folder with its own home, so your existing Hermes, "
             "its launcher and your shell profile are left alone.")
        : tr("Not available on Windows: the Hermes installer always adds itself to your user PATH, which "
             "would change which hermes your terminals run.");

    if (d.executableFound()) {
        addOption(Option::UseExisting, tr("Use this Hermes as it is (recommended)"),
                  tr("Consiglio won't modify, update, reinstall or migrate its configuration, environment or "
                     "data. It keeps finding hermes the usual way, so `hermes update` keeps working."));
    }
    if (!d.executableFound() && !d.dataExists) {
        addOption(Option::InstallOfficial, tr("Install Hermes with the official installer"),
                  tr("Opens a terminal running the installer from hermes-agent.nousresearch.com, so you can "
                     "see and answer every step."));
    }
    addOption(Option::ChooseExecutable, tr("Use a different hermes executable…"),
              tr("Point Consiglio at a specific hermes. That path is used as given."));
    m_exePath = new QLineEdit(choices);
    m_exePath->setPlaceholderText(tr("Path to hermes"));
    m_exePath->setClearButtonEnabled(true);
    ui::describe(m_exePath, tr("hermes executable"), tr("The hermes executable to use"));
    auto *exeRow = new QWidget(choices);
    auto *erl = new QHBoxLayout(exeRow);
    erl->setContentsMargins(t.px(26), 0, 0, 0);
    erl->addWidget(m_exePath, 1);
    auto *browseExe = new QPushButton(t.icon("folder-open"), tr("Browse…"), exeRow);
    ui::describe(browseExe, tr("Browse…"), tr("Choose the hermes executable"));
    erl->addWidget(browseExe);
    cl->addWidget(exeRow);
    connect(browseExe, &QPushButton::clicked, this, [this]() {
        const QString f = QFileDialog::getOpenFileName(this, tr("Choose the hermes executable"), m_env.home);
        if (!f.isEmpty()) { m_exePath->setText(f); selectOption(Option::ChooseExecutable); }
    });

    if (d.anythingFound()) {
        auto *r = addOption(Option::InstallSeparate, tr("Install a separate, fresh copy"), sepDesc);
        r->setEnabled(sepOk);
        m_rootPath = new QLineEdit(HermesSetup::defaultSeparateRoot(), choices);
        m_rootPath->setEnabled(sepOk);
        ui::describe(m_rootPath, tr("Folder for the separate copy"),
                     tr("Everything the separate copy installs goes under this folder"));
        auto *rootRow = new QWidget(choices);
        auto *rrl = new QHBoxLayout(rootRow);
        rrl->setContentsMargins(t.px(26), 0, 0, 0);
        rrl->addWidget(m_rootPath, 1);
        auto *browseRoot = new QPushButton(t.icon("folder-open"), tr("Browse…"), rootRow);
        browseRoot->setEnabled(sepOk);
        ui::describe(browseRoot, tr("Browse…"), tr("Choose a folder for the separate copy"));
        rrl->addWidget(browseRoot);
        cl->addWidget(rootRow);
        connect(browseRoot, &QPushButton::clicked, this, [this]() {
            const QString f = QFileDialog::getExistingDirectory(this, tr("Folder for the separate Hermes"), m_env.home);
            if (!f.isEmpty()) { m_rootPath->setText(f); selectOption(Option::InstallSeparate); }
        });
        connect(m_rootPath, &QLineEdit::textChanged, this, [this]() { updateState(); });
    }
    if (!d.executableFound() && d.dataExists) {
        addOption(Option::InstallIntoExistingData, tr("Run the official installer with my existing data"),
                  tr("The installer will use, and may update, the Hermes data folder shown above."));
        auto *consentRow = new QWidget(choices);
        auto *crl = new QHBoxLayout(consentRow);
        crl->setContentsMargins(t.px(26), 0, 0, 0);
        m_consent = new QCheckBox(tr("I understand the installer will write into this data folder"), consentRow);
        m_consent->setObjectName(QStringLiteral("setupConsent"));
        crl->addWidget(m_consent, 1);
        ui::describe(m_consent, tr("Allow writing into the existing Hermes folder"),
                     tr("Required before the installer may touch %1").arg(d.dataDir));
        cl->addWidget(consentRow);
        connect(m_consent, &QCheckBox::toggled, this, [this]() { updateState(); });
    }
    l->addWidget(choices);

    // What will happen.
    QVBoxLayout *pl = nullptr;
    auto *previewCard = card(this, &pl);
    pl->setSpacing(t.px(4));
    pl->addWidget(text(tr("WHAT HAPPENS"), "section", previewCard));
    m_preview = mono(QString(), previewCard);
    m_preview->setAccessibleName(tr("What happens"));
    pl->addWidget(m_preview);
    l->addWidget(previewCard);

    m_status = text(QString(), "faint", this);
    m_status->setVisible(false);
    l->addWidget(m_status);

    auto *buttons = new QDialogButtonBox(this);
    m_skip = buttons->addButton(tr("Skip for now"), QDialogButtonBox::RejectRole);
    ui::describe(m_skip, tr("Skip for now"),
                 tr("Don't set anything up now; choose later in Settings → Hermes Agent"));
    m_continue = buttons->addButton(tr("Continue"), QDialogButtonBox::AcceptRole);
    m_continue->setProperty("role", "primary");
    m_continue->setObjectName(QStringLiteral("setupContinue"));
    m_skip->setObjectName(QStringLiteral("setupSkip"));
    m_continue->setDefault(true);
    ui::describe(m_continue, tr("Continue"), tr("Use the selected option"));
    l->addWidget(buttons);
    connect(m_continue, &QPushButton::clicked, this, &HermesSetupDialog::onContinue);
    connect(m_skip, &QPushButton::clicked, this, &HermesSetupDialog::onSkip);
    connect(m_group, QOverload<int>::of(&QButtonGroup::buttonClicked), this, [this](int) { updateState(); });
    connect(m_exePath, &QLineEdit::textChanged, this, [this]() { updateState(); });

    // Defaults: an existing Hermes is used as-is.
    if (d.executableFound()) selectOption(Option::UseExisting);
    else if (!d.dataExists) selectOption(Option::InstallOfficial);
    else selectOption(Option::ChooseExecutable);
}

void HermesSetupDialog::rebuildDetails() {
    auto &t = th();
    auto *dl = qobject_cast<QVBoxLayout *>(m_details->layout());
    while (QLayoutItem *it = dl->takeAt(0)) { if (it->widget()) it->widget()->deleteLater(); delete it; }
    const auto &d = m_detection;
    if (!d.anythingFound() && d.hermesBinProblem.isEmpty()) return;
    QVBoxLayout *cl = nullptr;
    auto *c = card(m_details, &cl);
    auto *grid = new QGridLayout();
    grid->setHorizontalSpacing(t.px(14));
    grid->setVerticalSpacing(t.px(6));
    grid->setColumnStretch(1, 1);
    int row = 0;
    auto addRow = [&](const QString &k, QWidget *v) {
        auto *kl = text(k, "muted", c);
        kl->setWordWrap(false);
        grid->addWidget(kl, row, 0, Qt::AlignTop);
        grid->addWidget(v, row, 1);
        ++row;
    };
    if (d.executableFound()) {
        addRow(tr("Version"), mono(d.version.isEmpty() ? tr("unknown (hermes --version gave no answer)") : d.version, c));
        const QString src = d.source == QLatin1String("HERMES_BIN") ? tr("from $HERMES_BIN")
                          : d.source == QLatin1String("PATH") ? tr("on PATH") : tr("in a standard install folder");
        addRow(tr("Executable"), mono(QStringLiteral("%1\n(%2)").arg(d.executable, src), c));
    }
    if (!d.hermesBinProblem.isEmpty()) addRow(tr("Note"), text(d.hermesBinProblem, "faint", c));
    if (d.dataExists) {
        addRow(tr("Data"), mono(d.dataDir, c));
        auto *chips = new QWidget(c);
        auto *flow = new ui::FlowLayout(chips, t.px(6), t.px(6));
        if (d.hasConfig) flow->addWidget(chip("file-code", tr("config.yaml"), chips));
        if (d.hasKeys) flow->addWidget(chip("shield", tr("API keys (.env, not read)"), chips));
        if (d.hasSessions)
            flow->addWidget(chip("history", tr("sessions (%1)").arg(QLocale().formattedDataSize(d.sessionsBytes)), chips));
        if (d.skillCount > 0) flow->addWidget(chip("sparkles", d.skillCount == 1 ? tr("1 skill") : tr("%1 skills").arg(d.skillCount), chips));
        if (d.hasCheckout) flow->addWidget(chip("package", tr("install (hermes-agent)"), chips));
        addRow(QString(), chips);
    }
    cl->addLayout(grid);
    dl->addWidget(c);
}

HermesSetupDialog::Option HermesSetupDialog::selectedOption() const {
    return Option(m_group->checkedId());
}

void HermesSetupDialog::selectOption(Option option) {
    if (QRadioButton *r = radio(option)) r->setChecked(true);
    updateState();
}

bool HermesSetupDialog::isOptionAvailable(Option option) const { return radio(option) != nullptr; }
bool HermesSetupDialog::isOptionEnabled(Option option) const { return radio(option) && radio(option)->isEnabled(); }
QString HermesSetupDialog::commandPreview() const { return m_preview ? m_preview->text() : QString(); }
QString HermesSetupDialog::executablePath() const { return m_exePath->text().trimmed(); }
void HermesSetupDialog::setExecutablePath(const QString &p) { m_exePath->setText(p); }
QString HermesSetupDialog::separateRoot() const { return m_rootPath ? m_rootPath->text().trimmed() : QString(); }
void HermesSetupDialog::setSeparateRoot(const QString &p) { if (m_rootPath) m_rootPath->setText(p); }

void HermesSetupDialog::updateState() {
    if (!m_group || m_group->checkedId() < 0 || !m_preview) return;
    const Option o = selectedOption();
    QString preview;
    bool ok = true;
    switch (o) {
    case Option::UseExisting:
        preview = tr("Run %1 acp\nNothing is installed, updated or changed.").arg(m_detection.executable);
        break;
    case Option::ChooseExecutable:
        ok = isExecutable(executablePath());
        preview = ok ? tr("Run %1 acp").arg(executablePath()) : tr("Choose a hermes executable.");
        break;
    case Option::InstallSeparate: {
        const QString root = separateRoot();
        const bool clash = root.isEmpty() || isInside(m_detection.dataDir, root) || isInside(root, m_detection.dataDir)
                        || QDir::cleanPath(root) == QDir::cleanPath(m_env.home);
        ok = HermesSetup::separateInstallSupported(m_env) && !clash;
        preview = clash ? tr("Choose a folder that isn't your home folder and doesn't contain or sit inside %1.").arg(m_detection.dataDir)
                        : HermesSetup::separateInstall(m_env, root).display;
        break;
    }
    case Option::InstallOfficial:
        preview = HermesSetup::officialInstall(m_env).display;
        break;
    case Option::InstallIntoExistingData:
        ok = m_consent && m_consent->isChecked();
        preview = HermesSetup::officialInstall(m_env).display;
        break;
    }
    m_preview->setText(preview);
    m_continue->setEnabled(ok);
    if (!m_installerStarted)
        m_continue->setText(o == Option::InstallOfficial || o == Option::InstallSeparate || o == Option::InstallIntoExistingData
                                ? tr("Open installer") : tr("Continue"));
}

bool HermesSetupDialog::startInstaller(const HermesSetup::InstallCommand &cmd) {
    QString error;
    if (!m_launcher || !m_launcher(cmd, &error)) {
        m_status->setText(error.isEmpty() ? tr("Couldn't start the installer.") : error);
        m_status->setVisible(true);
        return false;
    }
    m_installerStarted = true;
    m_status->setText(tr("The installer is running in a terminal. When it has finished, click Check again."));
    m_status->setVisible(true);
    m_continue->setText(tr("Check again"));
    return true;
}

void HermesSetupDialog::onContinue() {
    const Option o = selectedOption();
    if (m_installerStarted) { recheck(); return; }
    switch (o) {
    case Option::UseExisting:
        HermesSetup::saveChoice(HermesSetup::Choice::UseExisting);
        m_saved = HermesSetup::Choice::UseExisting;
        accept();
        return;
    case Option::ChooseExecutable:
        if (!isExecutable(executablePath())) return;
        HermesSetup::saveChoice(HermesSetup::Choice::Custom, executablePath());
        m_saved = HermesSetup::Choice::Custom;
        accept();
        return;
    case Option::InstallSeparate:
        m_pendingInstall = o;
        startInstaller(HermesSetup::separateInstall(m_env, separateRoot()));
        return;
    case Option::InstallOfficial:
    case Option::InstallIntoExistingData:
        if (o == Option::InstallIntoExistingData && !(m_consent && m_consent->isChecked())) return;
        m_pendingInstall = o;
        startInstaller(HermesSetup::officialInstall(m_env));
        return;
    }
}

void HermesSetupDialog::recheck() {
    if (m_pendingInstall == Option::InstallSeparate) {
        for (const QString &p : HermesSetup::separateExecutableCandidates(separateRoot(), m_env.windows)) {
            if (isExecutable(p)) {
                HermesSetup::saveChoice(HermesSetup::Choice::Separate, QString(), separateRoot());
                m_saved = HermesSetup::Choice::Separate;
                accept();
                return;
            }
        }
    } else {
        m_detection = HermesSetup::detect(m_env, 3000);
        if (m_detection.executableFound()) {
            HermesSetup::saveChoice(HermesSetup::Choice::Installed);
            m_saved = HermesSetup::Choice::Installed;
            accept();
            return;
        }
    }
    m_status->setText(tr("Hermes isn't there yet. Let the installer finish, then click Check again."));
    m_status->setVisible(true);
}

void HermesSetupDialog::onSkip() {
    HermesSetup::saveChoice(HermesSetup::Choice::Skipped);
    m_saved = HermesSetup::Choice::Skipped;
    reject();
}

HermesSetup::Choice HermesSetupDialog::runIfNeeded(QWidget *parent) {
    if (!HermesSetup::needsSetup()) return HermesSetup::savedChoice();
    const auto env = HermesSetup::Environment::current();
    HermesSetupDialog dlg(env, HermesSetup::detect(env), parent);
    dlg.exec();
    return dlg.savedChoice();
}
