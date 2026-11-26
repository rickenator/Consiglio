#include "commandlivehosts.h"

#include "theme.h"
#include "uiutil.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

bool textAction(const QString &action) {
    return action == QLatin1String("set") || action == QLatin1String("draft")
        || action == QLatin1String("subgoal-add") || action == QLatin1String("add")
        || action == QLatin1String("create") || action == QLatin1String("search")
        || action == QLatin1String("install") || action == QLatin1String("apply");
}

} // namespace

// ─── LivePanelHost ────────────────────────────────────────────────────────

void LivePanelHost::build(CommandHostDialog &shell) {
    m_shell = &shell;
    auto *root = shell.bodyLayout();
    auto *page = shell.bodyWidget();
    if (!root || !page) return;
    auto &t = Theme::instance();

    m_status = new QLabel(page);
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    m_status->setObjectName(QStringLiteral("commandLiveStatus"));
    m_status->setProperty("role", "card");
    m_status->setProperty("state", QStringLiteral("completed"));
    m_status->setText(CommandHostDialog::tr("Loading…"));
    m_status->setStyleSheet(QStringLiteral("padding: %1px %2px;").arg(t.px(10)).arg(t.px(12)));
    root->addWidget(m_status);

    auto *toolbar = new QHBoxLayout();
    toolbar->setSpacing(t.px(8));

    m_action = new QComboBox(page);
    m_action->setMinimumWidth(180);
    ui::describe(m_action, CommandHostDialog::tr("Action"),
                 CommandHostDialog::tr("Choose what Save will apply"));
    for (const auto &a : actions())
        m_action->addItem(a.second, a.first);
    if (m_action->count() > 0) m_action->setCurrentIndex(0);
    QObject::connect(m_action, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
                     &shell, [this](int) { refreshChrome(); });
    toolbar->addWidget(m_action, 1);

    m_interval = new QLineEdit(page);
    m_interval->setPlaceholderText(CommandHostDialog::tr("Interval (e.g. 10m)"));
    m_interval->setMaximumWidth(140);
    ui::describe(m_interval, CommandHostDialog::tr("Interval"),
                 CommandHostDialog::tr("How often the heartbeat or loop should run"));
    QObject::connect(m_interval, &QLineEdit::textChanged, &shell, [this]() {
        if (this->shell()) this->shell()->refreshSaveEnabled();
    });
    toolbar->addWidget(m_interval);

    m_days = new QSpinBox(page);
    m_days->setRange(1, 365);
    m_days->setValue(7);
    m_days->setSuffix(CommandHostDialog::tr(" days"));
    ui::describe(m_days, CommandHostDialog::tr("Days"),
                 CommandHostDialog::tr("How many days of insights to show"));
    toolbar->addWidget(m_days);

    m_diffMode = new QComboBox(page);
    m_diffMode->addItem(CommandHostDialog::tr("Session (with last)"), QStringLiteral(""));
    m_diffMode->addItem(CommandHostDialog::tr("Branch"), QStringLiteral("--branch"));
    m_diffMode->addItem(CommandHostDialog::tr("All"), QStringLiteral("--all"));
    ui::describe(m_diffMode, CommandHostDialog::tr("Diff mode"),
                 CommandHostDialog::tr("Which files to include in the report"));
    toolbar->addWidget(m_diffMode);

    m_optionStat = new QCheckBox(CommandHostDialog::tr("Summary only"), page);
    m_optionStat->setChecked(true);
    ui::describe(m_optionStat, CommandHostDialog::tr("Summary only"),
                 CommandHostDialog::tr("Show a compact --stat report instead of a full patch"));
    toolbar->addWidget(m_optionStat);

    m_optionAll = new QCheckBox(CommandHostDialog::tr("Restore all files"), page);
    ui::describe(m_optionAll, CommandHostDialog::tr("Restore all files"),
                 CommandHostDialog::tr("Include --all so the rollback is not limited to files touched this session"));
    toolbar->addWidget(m_optionAll);

    toolbar->addStretch(1);
    root->addLayout(toolbar);

    m_prompt = new QPlainTextEdit(page);
    m_prompt->setObjectName(QStringLiteral("commandLivePrompt"));
    m_prompt->setPlaceholderText(CommandHostDialog::tr("Text"));
    m_prompt->setMaximumHeight(t.px(96));
    m_prompt->setTabChangesFocus(true);
    QObject::connect(m_prompt, &QPlainTextEdit::textChanged, &shell, [this]() {
        if (this->shell()) this->shell()->refreshSaveEnabled();
    });
    root->addWidget(m_prompt);

    m_list = new QListWidget(page);
    m_list->setObjectName(QStringLiteral("commandLiveList"));
    m_list->setAlternatingRowColors(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setMinimumHeight(t.px(160));
    ui::describe(m_list, CommandHostDialog::tr("Items"),
                 CommandHostDialog::tr("Select a row to act on. Nothing is changed until you Save."));
    QObject::connect(m_list, &QListWidget::currentItemChanged, &shell, [this]() {
        if (this->shell()) this->shell()->refreshSaveEnabled();
    });
    root->addWidget(m_list, 1);

    if (showQueueEditor()) {
        auto *editRow = new QHBoxLayout();
        editRow->setSpacing(t.px(8));
        m_addItem = new QPushButton(CommandHostDialog::tr("Add"), page);
        m_removeItem = new QPushButton(CommandHostDialog::tr("Remove"), page);
        m_moveUp = new QPushButton(CommandHostDialog::tr("Move up"), page);
        m_moveDown = new QPushButton(CommandHostDialog::tr("Move down"), page);
        ui::describe(m_addItem, CommandHostDialog::tr("Add"),
                     CommandHostDialog::tr("Append the text above as a new queued prompt (applied on Save)"));
        ui::describe(m_removeItem, CommandHostDialog::tr("Remove"),
                     CommandHostDialog::tr("Remove the selected prompt from the list (applied on Save)"));
        ui::describe(m_moveUp, CommandHostDialog::tr("Move up"),
                     CommandHostDialog::tr("Move the selected prompt earlier in the queue"));
        ui::describe(m_moveDown, CommandHostDialog::tr("Move down"),
                     CommandHostDialog::tr("Move the selected prompt later in the queue"));
        QObject::connect(m_addItem, &QPushButton::clicked, &shell, [this]() { addItemFromField(); });
        QObject::connect(m_removeItem, &QPushButton::clicked, &shell, [this]() {
            removeSelectedLiveItem();
            updateStatus();
            if (this->shell()) this->shell()->refreshSaveEnabled();
        });
        QObject::connect(m_moveUp, &QPushButton::clicked, &shell, [this]() {
            moveSelectedLiveItem(-1);
            updateStatus();
            if (this->shell()) this->shell()->refreshSaveEnabled();
        });
        QObject::connect(m_moveDown, &QPushButton::clicked, &shell, [this]() {
            moveSelectedLiveItem(1);
            updateStatus();
            if (this->shell()) this->shell()->refreshSaveEnabled();
        });
        editRow->addWidget(m_addItem);
        editRow->addWidget(m_removeItem);
        editRow->addWidget(m_moveUp);
        editRow->addWidget(m_moveDown);
        editRow->addStretch(1);
        root->addLayout(editRow);
    }

    m_body = new QPlainTextEdit(page);
    m_body->setObjectName(QStringLiteral("commandLiveBody"));
    m_body->setReadOnly(true);
    m_body->setMinimumHeight(t.px(120));
    m_body->setPlaceholderText(CommandHostDialog::tr("Report"));
    m_body->setStyleSheet(QStringLiteral("font-family: \"JetBrains Mono\", \"DejaVu Sans Mono\", monospace;"));
    ui::describe(m_body, CommandHostDialog::tr("Report"),
                 CommandHostDialog::tr("Output from Hermes for this command"));
    root->addWidget(m_body, 1);

    refreshChrome();
}

void LivePanelHost::seedReport(const QString &report) {
    ingestReport(name(), report);
}

void LivePanelHost::refreshChrome() {
    const QString a = liveAction();
    const QString n = name();
    const bool needsText = textAction(a)
        || (showQueueEditor() && a == QLatin1String("edit"))
        || (n == QLatin1String("diff") && a == QLatin1String("refresh"));
    if (m_prompt) {
        m_prompt->setVisible(needsText);
        m_prompt->setPlaceholderText(placeholder(a));
        if (showQueueEditor()) m_prompt->setMaximumHeight(56);
    }
    if (m_interval) m_interval->setVisible(showInterval());
    if (m_days) m_days->setVisible(showDays());
    if (m_diffMode) m_diffMode->setVisible(showDiffOpts());
    if (m_optionStat) m_optionStat->setVisible(showDiffOpts());
    if (m_optionAll) m_optionAll->setVisible(showOptionAll());
    if (m_list) m_list->setVisible(showList());
    const bool bodyOn = showBody() || !showList() || n == QLatin1String("memory");
    if (m_body) m_body->setVisible(bodyOn);
    const bool editorOn = showQueueEditor() && a != QLatin1String("clear");
    if (m_addItem) m_addItem->setVisible(editorOn);
    if (m_removeItem) m_removeItem->setVisible(editorOn);
    if (m_moveUp) m_moveUp->setVisible(editorOn);
    if (m_moveDown) m_moveDown->setVisible(editorOn);

    if (auto *s = this->shell()) {
        const Kind k = kindOf(a);
        if (k == Kind::Never) {
            s->setSaveVisible(false);
        } else if (isDangerous(a)) {
            s->setSaveVisible(true);
            s->setSaveText(CommandHostDialog::tr("Confirm"));
        } else if (n == QLatin1String("insights") || n == QLatin1String("diff")
                   || a == QLatin1String("search") || a == QLatin1String("inspect")
                   || a == QLatin1String("diff-item")) {
            s->setSaveVisible(true);
            s->setSaveText(CommandHostDialog::tr("Run"));
        } else {
            s->setSaveVisible(true);
            s->setSaveText(CommandHostDialog::tr("Save"));
        }
        s->refreshSaveEnabled();
    }
}

void LivePanelHost::updateStatus(const QString &report) {
    if (!report.isEmpty()) {
        const QString status = CommandUi::statusFromReport(report);
        if (!status.isEmpty()) m_statusText = status;
    }
    if (m_status) {
        QString text = m_statusText;
        if (text.isEmpty()) text = CommandHostDialog::tr("Nothing loaded yet. Press Refresh, or Save when you have a change.");
        if (showQueueEditor() && m_items != m_original)
            text += CommandHostDialog::tr("\nEdits are local until you Save.");
        m_status->setText(text);
    }
}

bool LivePanelHost::isDangerous(const QString &action) const {
    return action == QLatin1String("restore") || action == QLatin1String("clear")
        || action == QLatin1String("remove") || action == QLatin1String("prune")
        || action == QLatin1String("upload") || action == QLatin1String("install")
        || action == QLatin1String("stop") || action == QLatin1String("run");
}

bool LivePanelHost::isReportAction(const QString &action) const {
    return action == QLatin1String("refresh") || action == QLatin1String("search")
        || action == QLatin1String("inspect") || action == QLatin1String("diff-item");
}

bool LivePanelHost::keepOpenFor(const QString &action) const {
    return action != QLatin1String("restore") && action != QLatin1String("upload");
}

QString LivePanelHost::placeholder(const QString &action) const {
    Q_UNUSED(action);
    return CommandHostDialog::tr("Text");
}

void LivePanelHost::afterIngest(const QString &command, const QString &text) {
    Q_UNUSED(command);
    Q_UNUSED(text);
}

bool LivePanelHost::dirty() const {
    const QString a = liveAction();
    switch (kindOf(a)) {
    case Kind::Always:
        return true;
    case Kind::NeedsSelection:
        return !selectedValue().isEmpty();
    case Kind::NeedsText:
        return !textArgument().isEmpty();
    case Kind::NeedsIntervalOrText:
        return !textArgument().isEmpty() || !intervalText().isEmpty();
    case Kind::ListEdit:
        return m_items != m_original;
    case Kind::Report:
        if (a == QLatin1String("search")) return !textArgument().isEmpty();
        if (a == QLatin1String("inspect") || a == QLatin1String("diff-item"))
            return !selectedValue().isEmpty();
        return true;
    case Kind::Never:
        return false;
    }
    return false;
}

QString LivePanelHost::line() const {
    const QStringList lines = applyLines();
    return lines.isEmpty() ? QString() : lines.first();
}

CommandApplyRequest LivePanelHost::request() const {
    CommandApplyRequest req = CommandHost::request();
    req.quietLines = applyLines();
    if (req.quietLines.isEmpty() && !req.quietLine.isEmpty())
        req.quietLines << req.quietLine;
    req.keepOpen = keepOpenFor(liveAction());
    req.reseedAfter = req.keepOpen && !isReportAction(liveAction());
    req.value = selectedValue();
    return req;
}

QString LivePanelHost::liveAction() const {
    return m_action ? m_action->currentData().toString() : QString();
}

void LivePanelHost::setLiveAction(const QString &action) {
    if (!m_action) return;
    const int i = m_action->findData(action);
    if (i >= 0) m_action->setCurrentIndex(i);
}

void LivePanelHost::rebuildList() {
    if (!m_list) return;
    const QString keep = selectedValue();
    QSignalBlocker block(m_list);
    m_list->clear();
    for (const auto &it : m_items) {
        auto *row = new QListWidgetItem(it.name.isEmpty() ? it.value : it.name);
        row->setData(Qt::UserRole, it.value);
        if (!it.description.isEmpty()) row->setToolTip(it.description);
        m_list->addItem(row);
    }
    if (!keep.isEmpty()) {
        for (int i = 0; i < m_list->count(); ++i) {
            if (m_list->item(i)->data(Qt::UserRole).toString() == keep) {
                m_list->setCurrentRow(i);
                break;
            }
        }
    }
}

void LivePanelHost::setLiveItems(const QList<CommandPickerItem> &items, bool markClean) {
    m_items = items;
    if (markClean) m_original = items;
    rebuildList();
    updateStatus();
    if (this->shell()) this->shell()->refreshSaveEnabled();
}

void LivePanelHost::addLiveItem(const QString &text) {
    const QString t = text.trimmed();
    if (t.isEmpty()) return;
    m_items.push_back({t, t, QString(), QString()});
    rebuildList();
    if (m_list) m_list->setCurrentRow(m_list->count() - 1);
    updateStatus();
    if (this->shell()) this->shell()->refreshSaveEnabled();
}

void LivePanelHost::addItemFromField() {
    const QString t = textArgument();
    if (t.isEmpty()) return;
    addLiveItem(t);
    setTextArgument(QString());
}

void LivePanelHost::removeSelectedLiveItem() {
    const QString v = selectedValue();
    if (v.isEmpty() && !m_items.isEmpty() && m_list && m_list->currentRow() < 0) {
        // No current row: drop the last item (used by tests after an add).
    }
    int row = m_list ? m_list->currentRow() : -1;
    if (row < 0 || row >= m_items.size()) return;
    m_items.removeAt(row);
    rebuildList();
    if (m_list && m_list->count() > 0)
        m_list->setCurrentRow(qMin(row, m_list->count() - 1));
}

void LivePanelHost::moveSelectedLiveItem(int delta) {
    int row = m_list ? m_list->currentRow() : -1;
    if (row < 0) {
        const QString v = selectedValue();
        for (int i = 0; i < m_items.size(); ++i) {
            if (m_items[i].value == v) { row = i; break; }
        }
    }
    const int dest = row + delta;
    if (row < 0 || dest < 0 || dest >= m_items.size()) return;
    m_items.swapItemsAt(row, dest);
    rebuildList();
    if (m_list) m_list->setCurrentRow(dest);
}

QString LivePanelHost::selectedValue() const {
    if (m_list && m_list->currentItem())
        return m_list->currentItem()->data(Qt::UserRole).toString();
    return {};
}

void LivePanelHost::setSelectedValue(const QString &value) {
    if (!m_list) return;
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(Qt::UserRole).toString() == value) {
            m_list->setCurrentRow(i);
            return;
        }
    }
}

QString LivePanelHost::textArgument() const {
    return m_prompt ? m_prompt->toPlainText().trimmed() : QString();
}

void LivePanelHost::setTextArgument(const QString &text) {
    if (m_prompt) m_prompt->setPlainText(text);
}

QString LivePanelHost::intervalText() const {
    return m_interval ? m_interval->text().trimmed() : QString();
}

void LivePanelHost::setIntervalText(const QString &text) {
    if (m_interval) m_interval->setText(text);
}

int LivePanelHost::insightDays() const {
    return m_days ? m_days->value() : 7;
}

void LivePanelHost::setInsightDays(int n) {
    if (m_days) m_days->setValue(n);
}

QString LivePanelHost::diffMode() const {
    return m_diffMode ? m_diffMode->currentData().toString() : QString();
}

void LivePanelHost::setDiffMode(const QString &mode) {
    if (!m_diffMode) return;
    const int i = m_diffMode->findData(mode);
    if (i >= 0) m_diffMode->setCurrentIndex(i);
}

bool LivePanelHost::optionAll() const {
    return m_optionAll && m_optionAll->isChecked();
}

void LivePanelHost::setOptionAll(bool on) {
    if (m_optionAll) m_optionAll->setChecked(on);
}

bool LivePanelHost::optionStat() const {
    return m_optionStat && m_optionStat->isChecked();
}

void LivePanelHost::setOptionStat(bool on) {
    if (m_optionStat) m_optionStat->setChecked(on);
}

void LivePanelHost::ingestReport(const QString &commandName, const QString &text) {
    const QString n = name();
    if (m_body && (commandName == QLatin1String("debug") || commandName == QLatin1String("insights")
                   || commandName == QLatin1String("diff") || commandName == QLatin1String("agents")
                   || commandName == QLatin1String("rollback") || commandName == QLatin1String("skills")
                   || commandName.startsWith(QLatin1String("rollback ")))) {
        if (m_body->isVisible() || commandName == QLatin1String("debug")
            || commandName == QLatin1String("insights") || commandName == QLatin1String("diff"))
            m_body->setPlainText(text);
    }
    const QString seedName = (n == QLatin1String("subgoal") || commandName == QLatin1String("subgoal")
                              || commandName == QLatin1String("goal"))
                             ? (commandName == QLatin1String("subgoal") ? QStringLiteral("goal") : n)
                             : (commandName.isEmpty() ? n : commandName);
    if (commandName == QLatin1String("goal") || commandName == QLatin1String("heartbeat")
        || commandName == QLatin1String("loop") || commandName == QLatin1String("queue")
        || commandName == QLatin1String("memory") || commandName == QLatin1String("cron")
        || commandName == QLatin1String("snapshot") || commandName == QLatin1String("suggestions")
        || commandName == QLatin1String("blueprint") || commandName == QLatin1String("agents")
        || commandName == QLatin1String("skills") || commandName == QLatin1String("rollback")
        || commandName == QLatin1String("usage") || commandName == QLatin1String("debug")
        || commandName == QLatin1String("insights") || commandName == QLatin1String("diff")
        || commandName.isEmpty()) {
        const QString status = CommandUi::statusFromReport(text);
        if (!status.isEmpty()) m_statusText = status;
    }
    const QList<CommandPickerItem> parsed = CommandUi::itemsFromLiveReport(seedName, text);
    const bool isListSeed = commandName == QLatin1String("subgoal")
        || commandName == QLatin1String("queue") || commandName == QLatin1String("memory")
        || commandName == QLatin1String("skills") || commandName == QLatin1String("rollback")
        || commandName == QLatin1String("snapshot") || commandName == QLatin1String("cron")
        || commandName == QLatin1String("suggestions") || commandName == QLatin1String("blueprint")
        || commandName == QLatin1String("agents")
        || ((n == QLatin1String("goal") || n == QLatin1String("subgoal"))
            && commandName == QLatin1String("subgoal"));
    if (isListSeed) {
        setLiveItems(parsed, true);
    } else if (commandName != QLatin1String("goal") && commandName != QLatin1String("heartbeat")
               && commandName != QLatin1String("loop") && !parsed.isEmpty()
               && m_items.isEmpty()) {
        setLiveItems(parsed, true);
    }
    if (m_body && (commandName == QLatin1String("memory") || commandName == QLatin1String("queue")
                   || commandName == QLatin1String("cron") || commandName == QLatin1String("snapshot")
                   || commandName == QLatin1String("suggestions") || commandName == QLatin1String("blueprint"))) {
        if (m_body->toPlainText().trimmed().isEmpty()) m_body->setPlainText(text);
    }
    afterIngest(commandName, text);
    updateStatus();
    if (this->shell()) this->shell()->refreshSaveEnabled();
}

// ─── Per-command hosts ────────────────────────────────────────────────────

QList<QPair<QString, QString>> GoalHost::actions() const {
    return {
        {QStringLiteral("set"), CommandHostDialog::tr("Set goal")},
        {QStringLiteral("draft"), CommandHostDialog::tr("Draft from instruction")},
        {QStringLiteral("pause"), CommandHostDialog::tr("Pause")},
        {QStringLiteral("resume"), CommandHostDialog::tr("Resume")},
        {QStringLiteral("clear"), CommandHostDialog::tr("Clear goal")},
        {QStringLiteral("subgoal-add"), CommandHostDialog::tr("Add subgoal")},
        {QStringLiteral("subgoal-remove"), CommandHostDialog::tr("Remove selected")},
        {QStringLiteral("subgoal-clear"), CommandHostDialog::tr("Clear subgoals")},
    };
}

LivePanelHost::Kind GoalHost::kindOf(const QString &action) const {
    if (action == QLatin1String("pause") || action == QLatin1String("resume")
        || action == QLatin1String("clear") || action == QLatin1String("subgoal-clear"))
        return Kind::Always;
    if (action == QLatin1String("subgoal-remove")) return Kind::NeedsSelection;
    if (action == QLatin1String("set") || action == QLatin1String("draft")
        || action == QLatin1String("subgoal-add"))
        return Kind::NeedsText;
    return Kind::Never;
}

QString GoalHost::placeholder(const QString &action) const {
    if (action == QLatin1String("subgoal-add")) return CommandHostDialog::tr("Subgoal text");
    return CommandHostDialog::tr("Goal text");
}

void GoalHost::afterIngest(const QString &command, const QString &text) {
    Q_UNUSED(command);
    Q_UNUSED(text);
}

QStringList GoalHost::applyLines() const {
    const QString a = liveAction();
    const QString text = textArgument();
    const QString selected = selectedValue();
    if (a == QLatin1String("set")) return {QStringLiteral("/goal set ") + text};
    if (a == QLatin1String("draft")) return {QStringLiteral("/goal ") + text};
    if (a == QLatin1String("pause")) return {QStringLiteral("/goal pause")};
    if (a == QLatin1String("resume")) return {QStringLiteral("/goal resume")};
    if (a == QLatin1String("clear")) return {QStringLiteral("/goal clear")};
    if (a == QLatin1String("subgoal-add")) return {QStringLiteral("/subgoal add ") + text};
    if (a == QLatin1String("subgoal-remove") && !selected.isEmpty())
        return {QStringLiteral("/subgoal rm ") + selected};
    if (a == QLatin1String("subgoal-clear")) return {QStringLiteral("/subgoal clear")};
    return {};
}

QList<QPair<QString, QString>> HeartbeatHost::actions() const {
    return {
        {QStringLiteral("set"), CommandHostDialog::tr("Set")},
        {QStringLiteral("pause"), CommandHostDialog::tr("Pause")},
        {QStringLiteral("resume"), CommandHostDialog::tr("Resume")},
        {QStringLiteral("clear"), CommandHostDialog::tr("Clear")},
    };
}

LivePanelHost::Kind HeartbeatHost::kindOf(const QString &action) const {
    if (action == QLatin1String("set")) return Kind::NeedsIntervalOrText;
    if (action == QLatin1String("pause") || action == QLatin1String("resume")
        || action == QLatin1String("clear"))
        return Kind::Always;
    return Kind::Never;
}

QString HeartbeatHost::placeholder(const QString &action) const {
    Q_UNUSED(action);
    return CommandHostDialog::tr("Heartbeat prompt");
}

QStringList HeartbeatHost::applyLines() const {
    const QString a = liveAction();
    if (a == QLatin1String("set")) {
        QString line = QStringLiteral("/heartbeat set");
        if (!intervalText().isEmpty()) line += QLatin1Char(' ') + intervalText();
        if (!textArgument().isEmpty()) line += QLatin1Char(' ') + textArgument();
        return {line};
    }
    if (a == QLatin1String("pause")) return {QStringLiteral("/heartbeat pause")};
    if (a == QLatin1String("resume")) return {QStringLiteral("/heartbeat resume")};
    if (a == QLatin1String("clear")) return {QStringLiteral("/heartbeat clear")};
    return {};
}

QList<QPair<QString, QString>> LoopHost::actions() const {
    return {
        {QStringLiteral("set"), CommandHostDialog::tr("Set")},
        {QStringLiteral("stop"), CommandHostDialog::tr("Stop")},
    };
}

LivePanelHost::Kind LoopHost::kindOf(const QString &action) const {
    if (action == QLatin1String("set")) return Kind::NeedsIntervalOrText;
    if (action == QLatin1String("stop")) return Kind::Always;
    return Kind::Never;
}

QString LoopHost::placeholder(const QString &action) const {
    Q_UNUSED(action);
    return CommandHostDialog::tr("Loop instruction");
}

QStringList LoopHost::applyLines() const {
    const QString a = liveAction();
    if (a == QLatin1String("set")) {
        QString line = QStringLiteral("/loop");
        if (!intervalText().isEmpty()) line += QLatin1Char(' ') + intervalText();
        if (!textArgument().isEmpty()) line += QLatin1Char(' ') + textArgument();
        return {line};
    }
    if (a == QLatin1String("stop")) return {QStringLiteral("/loop stop")};
    return {};
}

QList<QPair<QString, QString>> QueueHost::actions() const {
    return {
        {QStringLiteral("edit"), CommandHostDialog::tr("Edit list")},
        {QStringLiteral("clear"), CommandHostDialog::tr("Clear queue")},
    };
}

LivePanelHost::Kind QueueHost::kindOf(const QString &action) const {
    if (action == QLatin1String("clear")) return Kind::Always;
    return Kind::ListEdit;
}

QString QueueHost::placeholder(const QString &action) const {
    Q_UNUSED(action);
    return CommandHostDialog::tr("New queued prompt");
}

QStringList QueueHost::applyLines() const {
    const QString a = liveAction();
    if (a == QLatin1String("clear")) return {QStringLiteral("/queue clear")};
    bool prefixSame = m_items.size() >= m_original.size();
    if (prefixSame) {
        for (int i = 0; i < m_original.size(); ++i) {
            if (m_items[i].name != m_original[i].name) { prefixSame = false; break; }
        }
    }
    if (m_items == m_original) return {};
    QStringList lines;
    if (prefixSame && m_items.size() > m_original.size() && !m_original.isEmpty()) {
        for (int i = m_original.size(); i < m_items.size(); ++i)
            lines << QStringLiteral("/queue add ") + m_items[i].name;
    } else if (prefixSame && m_original.isEmpty()) {
        for (const auto &it : m_items) lines << QStringLiteral("/queue add ") + it.name;
    } else {
        lines << QStringLiteral("/queue clear");
        for (const auto &it : m_items) lines << QStringLiteral("/queue add ") + it.name;
    }
    return lines;
}

QList<QPair<QString, QString>> AgentsHost::actions() const {
    return {{QStringLiteral("refresh"), CommandHostDialog::tr("View only")}};
}

LivePanelHost::Kind AgentsHost::kindOf(const QString &action) const {
    Q_UNUSED(action);
    return Kind::Never;
}

QList<QPair<QString, QString>> MemoryHost::actions() const {
    return {
        {QStringLiteral("approve"), CommandHostDialog::tr("Approve selected")},
        {QStringLiteral("reject"), CommandHostDialog::tr("Reject selected")},
        {QStringLiteral("gate-on"), CommandHostDialog::tr("Require write approval")},
        {QStringLiteral("gate-off"), CommandHostDialog::tr("Auto-apply writes")},
    };
}

LivePanelHost::Kind MemoryHost::kindOf(const QString &action) const {
    if (action == QLatin1String("approve") || action == QLatin1String("reject"))
        return Kind::NeedsSelection;
    if (action == QLatin1String("gate-on") || action == QLatin1String("gate-off"))
        return Kind::Always;
    return Kind::Never;
}

QStringList MemoryHost::applyLines() const {
    const QString a = liveAction();
    const QString selected = selectedValue();
    if (a == QLatin1String("approve") && !selected.isEmpty())
        return {QStringLiteral("/memory approve ") + selected};
    if (a == QLatin1String("reject") && !selected.isEmpty())
        return {QStringLiteral("/memory reject ") + selected};
    if (a == QLatin1String("gate-on")) return {QStringLiteral("/memory write-approval on")};
    if (a == QLatin1String("gate-off")) return {QStringLiteral("/memory write-approval off")};
    return {};
}

QList<QPair<QString, QString>> SkillsHost::actions() const {
    return {
        {QStringLiteral("search"), CommandHostDialog::tr("Search")},
        {QStringLiteral("inspect"), CommandHostDialog::tr("Inspect selected")},
        {QStringLiteral("install"), CommandHostDialog::tr("Install…")},
        {QStringLiteral("approve"), CommandHostDialog::tr("Approve selected")},
        {QStringLiteral("reject"), CommandHostDialog::tr("Reject selected")},
        {QStringLiteral("gate-on"), CommandHostDialog::tr("Require write approval")},
        {QStringLiteral("gate-off"), CommandHostDialog::tr("Auto-apply skill writes")},
    };
}

LivePanelHost::Kind SkillsHost::kindOf(const QString &action) const {
    if (action == QLatin1String("search") || action == QLatin1String("install"))
        return action == QLatin1String("search") ? Kind::Report : Kind::NeedsText;
    if (action == QLatin1String("inspect") || action == QLatin1String("approve")
        || action == QLatin1String("reject"))
        return action == QLatin1String("inspect") ? Kind::Report : Kind::NeedsSelection;
    if (action == QLatin1String("gate-on") || action == QLatin1String("gate-off"))
        return Kind::Always;
    return Kind::Never;
}

QString SkillsHost::placeholder(const QString &action) const {
    if (action == QLatin1String("search")) return CommandHostDialog::tr("Skill search");
    if (action == QLatin1String("install")) return CommandHostDialog::tr("Skill name or source to install");
    return CommandHostDialog::tr("Text");
}

QStringList SkillsHost::applyLines() const {
    const QString a = liveAction();
    const QString text = textArgument();
    const QString selected = selectedValue();
    if (a == QLatin1String("approve") && !selected.isEmpty())
        return {QStringLiteral("/skills approve ") + selected};
    if (a == QLatin1String("reject") && !selected.isEmpty())
        return {QStringLiteral("/skills reject ") + selected};
    if (a == QLatin1String("gate-on")) return {QStringLiteral("/skills write-approval on")};
    if (a == QLatin1String("gate-off")) return {QStringLiteral("/skills write-approval off")};
    if (a == QLatin1String("search")) return {QStringLiteral("/skills search ") + text};
    if (a == QLatin1String("inspect") && !selected.isEmpty())
        return {QStringLiteral("/skills inspect ") + selected};
    if (a == QLatin1String("install") && !text.isEmpty())
        return {QStringLiteral("/skills install ") + text};
    return {};
}

QList<QPair<QString, QString>> RollbackHost::actions() const {
    return {
        {QStringLiteral("restore"), CommandHostDialog::tr("Restore selected")},
        {QStringLiteral("diff-item"), CommandHostDialog::tr("Diff selected")},
    };
}

LivePanelHost::Kind RollbackHost::kindOf(const QString &action) const {
    if (action == QLatin1String("restore") || action == QLatin1String("diff-item"))
        return action == QLatin1String("diff-item") ? Kind::Report : Kind::NeedsSelection;
    return Kind::Never;
}

bool RollbackHost::keepOpenFor(const QString &action) const {
    return action != QLatin1String("restore");
}

QStringList RollbackHost::applyLines() const {
    const QString a = liveAction();
    const QString selected = selectedValue();
    if (a == QLatin1String("restore") && !selected.isEmpty()) {
        QString line = QStringLiteral("/rollback ") + selected;
        if (optionAll()) line += QStringLiteral(" --all");
        return {line};
    }
    if (a == QLatin1String("diff-item") && !selected.isEmpty())
        return {QStringLiteral("/rollback --diff ") + selected};
    return {};
}

QList<QPair<QString, QString>> SnapshotHost::actions() const {
    return {
        {QStringLiteral("create"), CommandHostDialog::tr("Create snapshot")},
        {QStringLiteral("restore"), CommandHostDialog::tr("Restore selected")},
        {QStringLiteral("prune"), CommandHostDialog::tr("Prune")},
    };
}

LivePanelHost::Kind SnapshotHost::kindOf(const QString &action) const {
    if (action == QLatin1String("create") || action == QLatin1String("prune"))
        return Kind::Always; // label on create is optional
    if (action == QLatin1String("restore")) return Kind::NeedsSelection;
    return Kind::Never;
}

QString SnapshotHost::placeholder(const QString &action) const {
    Q_UNUSED(action);
    return CommandHostDialog::tr("Optional snapshot label");
}

bool SnapshotHost::keepOpenFor(const QString &action) const {
    return action != QLatin1String("restore");
}

QStringList SnapshotHost::applyLines() const {
    const QString a = liveAction();
    if (a == QLatin1String("create")) {
        QString line = QStringLiteral("/snapshot");
        if (!textArgument().isEmpty()) line += QLatin1Char(' ') + textArgument();
        return {line};
    }
    if (a == QLatin1String("restore") && !selectedValue().isEmpty())
        return {QStringLiteral("/snapshot restore ") + selectedValue()};
    if (a == QLatin1String("prune")) return {QStringLiteral("/snapshot prune")};
    return {};
}

QList<QPair<QString, QString>> DebugHost::actions() const {
    return {
        {QStringLiteral("refresh"), CommandHostDialog::tr("View report")},
        {QStringLiteral("upload"), CommandHostDialog::tr("Upload report")},
    };
}

LivePanelHost::Kind DebugHost::kindOf(const QString &action) const {
    if (action == QLatin1String("upload")) return Kind::Always;
    return Kind::Never; // refresh is the Refresh button
}

bool DebugHost::keepOpenFor(const QString &action) const {
    return action != QLatin1String("upload");
}

QStringList DebugHost::applyLines() const {
    if (liveAction() == QLatin1String("upload")) return {QStringLiteral("/debug --upload")};
    return {};
}

QList<QPair<QString, QString>> InsightsHost::actions() const {
    return {{QStringLiteral("refresh"), CommandHostDialog::tr("Refresh report")}};
}

LivePanelHost::Kind InsightsHost::kindOf(const QString &action) const {
    Q_UNUSED(action);
    return Kind::Report;
}

QStringList InsightsHost::applyLines() const {
    return {QStringLiteral("/insights %1").arg(insightDays())};
}

QList<QPair<QString, QString>> DiffHost::actions() const {
    return {{QStringLiteral("refresh"), CommandHostDialog::tr("Refresh diff")}};
}

LivePanelHost::Kind DiffHost::kindOf(const QString &action) const {
    Q_UNUSED(action);
    return Kind::Report;
}

QString DiffHost::placeholder(const QString &action) const {
    Q_UNUSED(action);
    return CommandHostDialog::tr("Optional path");
}

QStringList DiffHost::applyLines() const {
    QString line = QStringLiteral("/diff");
    if (!diffMode().isEmpty()) line += QLatin1Char(' ') + diffMode();
    if (optionStat()) line += QStringLiteral(" --stat");
    if (!textArgument().isEmpty()) line += QLatin1Char(' ') + textArgument();
    return {line};
}

QList<QPair<QString, QString>> CronHost::actions() const {
    return {
        {QStringLiteral("pause"), CommandHostDialog::tr("Pause selected")},
        {QStringLiteral("resume"), CommandHostDialog::tr("Resume selected")},
        {QStringLiteral("run"), CommandHostDialog::tr("Run selected now")},
        {QStringLiteral("remove"), CommandHostDialog::tr("Remove selected")},
    };
}

LivePanelHost::Kind CronHost::kindOf(const QString &action) const {
    Q_UNUSED(action);
    return Kind::NeedsSelection;
}

QStringList CronHost::applyLines() const {
    const QString a = liveAction();
    const QString selected = selectedValue();
    if (selected.isEmpty()) return {};
    if (a == QLatin1String("pause")) return {QStringLiteral("/cron pause ") + selected};
    if (a == QLatin1String("resume")) return {QStringLiteral("/cron resume ") + selected};
    if (a == QLatin1String("run")) return {QStringLiteral("/cron run ") + selected};
    if (a == QLatin1String("remove")) return {QStringLiteral("/cron remove ") + selected};
    return {};
}

QList<QPair<QString, QString>> SuggestionsHost::actions() const {
    return {
        {QStringLiteral("accept"), CommandHostDialog::tr("Accept selected")},
        {QStringLiteral("dismiss"), CommandHostDialog::tr("Dismiss selected")},
    };
}

LivePanelHost::Kind SuggestionsHost::kindOf(const QString &action) const {
    Q_UNUSED(action);
    return Kind::NeedsSelection;
}

QStringList SuggestionsHost::applyLines() const {
    const QString a = liveAction();
    const QString selected = selectedValue();
    if (selected.isEmpty()) return {};
    if (a == QLatin1String("accept")) return {QStringLiteral("/suggestions accept ") + selected};
    if (a == QLatin1String("dismiss")) return {QStringLiteral("/suggestions dismiss ") + selected};
    return {};
}

QList<QPair<QString, QString>> BlueprintHost::actions() const {
    return {{QStringLiteral("apply"), CommandHostDialog::tr("Create from template")}};
}

LivePanelHost::Kind BlueprintHost::kindOf(const QString &action) const {
    Q_UNUSED(action);
    return Kind::NeedsSelection;
}

QString BlueprintHost::placeholder(const QString &action) const {
    Q_UNUSED(action);
    return CommandHostDialog::tr("Optional slot=value pairs");
}

QStringList BlueprintHost::applyLines() const {
    if (liveAction() != QLatin1String("apply") || selectedValue().isEmpty()) return {};
    QString line = QStringLiteral("/blueprint ") + selectedValue();
    if (!textArgument().isEmpty()) line += QLatin1Char(' ') + textArgument();
    return {line};
}

// ─── Usage (Info + confirm reset) ─────────────────────────────────────────

void UsageHost::build(CommandHostDialog &shell) {
    m_shell = &shell;
    shell.buildInfoBody();
    m_reset = shell.addChromeButton(CommandHostDialog::tr("Reset…"));
    if (!m_reset) return;
    ui::describe(m_reset, CommandHostDialog::tr("Reset"),
                 CommandHostDialog::tr("Redeem a banked Codex usage limit (asks for confirmation)"));
    QObject::connect(m_reset, &QPushButton::clicked, &shell, [this]() {
        if (!m_reset || !this->shell()) return;
        if (m_reset->property("armed").toBool()) {
            const CommandApplyRequest req = request();
            m_reset->setProperty("armed", false);
            m_reset->setText(CommandHostDialog::tr("Reset…"));
            this->shell()->submitApply(req);
        } else {
            m_reset->setProperty("armed", true);
            m_reset->setText(CommandHostDialog::tr("Confirm reset"));
        }
    });
}

QString UsageHost::line() const {
    if (m_reset && m_reset->property("armed").toBool())
        return QStringLiteral("/usage reset --force");
    return slash();
}

CommandApplyRequest UsageHost::request() const {
    CommandApplyRequest req = CommandHost::request();
    if (m_reset && m_reset->property("armed").toBool()) {
        req.quietLine = QStringLiteral("/usage reset --force");
        req.quietLines = QStringList{req.quietLine};
        req.keepOpen = true;
        req.reseedAfter = true;
    }
    return req;
}
