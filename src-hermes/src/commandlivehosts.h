#pragma once

#include "commandhost.h"

#include <QHash>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

// Shared chrome for live-panel commands (status, optional list, action,
// text, then Save). Each command still has its own host class — this base
// only holds the widgets and the apply-on-Save / keep-open machinery.
class LivePanelHost : public CommandHost {
public:
    void build(CommandHostDialog &shell) override;
    void seedReport(const QString &report) override;
    bool dirty() const override;
    bool canApply() const override { return dirty(); }
    QString line() const override;
    CommandApplyRequest request() const override;

    QString liveAction() const;
    void setLiveAction(const QString &action);
    QList<CommandPickerItem> liveItems() const { return m_items; }
    void setLiveItems(const QList<CommandPickerItem> &items, bool markClean = false);
    void addLiveItem(const QString &text);
    void removeSelectedLiveItem();
    void moveSelectedLiveItem(int delta);
    QString selectedValue() const;
    void setSelectedValue(const QString &value);
    QString textArgument() const;
    void setTextArgument(const QString &text);
    QString intervalText() const;
    void setIntervalText(const QString &text);
    int insightDays() const;
    void setInsightDays(int n);
    QString diffMode() const;
    void setDiffMode(const QString &mode);
    bool optionAll() const;
    void setOptionAll(bool on);
    bool optionStat() const;
    void setOptionStat(bool on);
    void ingestReport(const QString &command, const QString &text);
    virtual QStringList applyLines() const = 0;

protected:
    enum class Kind {
        Always,           // pause, clear, …
        NeedsSelection,   // restore, approve, …
        NeedsText,        // set, draft, install, …
        NeedsIntervalOrText,
        ListEdit,         // queue local edits
        Report,           // run/search/refresh a report
        Never
    };

    LivePanelHost(const CommandPlan &plan, const CommandCatalog::Command &command)
        : CommandHost(plan, command) {}

    virtual QList<QPair<QString, QString>> actions() const = 0;
    virtual Kind kindOf(const QString &action) const = 0;
    virtual bool showList() const { return true; }
    virtual bool showBody() const { return false; }
    virtual bool showQueueEditor() const { return false; }
    virtual bool showInterval() const { return false; }
    virtual bool showDays() const { return false; }
    virtual bool showDiffOpts() const { return false; }
    virtual bool showOptionAll() const { return false; }
    virtual bool isDangerous(const QString &action) const;
    virtual bool isReportAction(const QString &action) const;
    virtual QString placeholder(const QString &action) const;
    virtual void afterIngest(const QString &command, const QString &text);
    virtual bool keepOpenFor(const QString &action) const;

    void refreshChrome();
    void updateStatus(const QString &report = QString());
    QString statusText() const { return m_statusText; }

    QList<CommandPickerItem> m_items;
    QList<CommandPickerItem> m_original;

private:
    void addItemFromField();
    void rebuildList();

    QLabel *m_status = nullptr;
    QPlainTextEdit *m_body = nullptr;
    QListWidget *m_list = nullptr;
    QComboBox *m_action = nullptr;
    QPlainTextEdit *m_prompt = nullptr;
    QLineEdit *m_interval = nullptr;
    QSpinBox *m_days = nullptr;
    QComboBox *m_diffMode = nullptr;
    QCheckBox *m_optionAll = nullptr;
    QCheckBox *m_optionStat = nullptr;
    QPushButton *m_addItem = nullptr;
    QPushButton *m_removeItem = nullptr;
    QPushButton *m_moveUp = nullptr;
    QPushButton *m_moveDown = nullptr;
    QString m_statusText;
};

class GoalHost : public LivePanelHost {
public:
    GoalHost(const CommandPlan &p, const CommandCatalog::Command &c) : LivePanelHost(p, c) {}
    QList<QPair<QString, QString>> actions() const override;
    Kind kindOf(const QString &action) const override;
    QStringList applyLines() const override;
    QString placeholder(const QString &action) const override;
    void afterIngest(const QString &command, const QString &text) override;
};

class HeartbeatHost : public LivePanelHost {
public:
    HeartbeatHost(const CommandPlan &p, const CommandCatalog::Command &c) : LivePanelHost(p, c) {}
    QList<QPair<QString, QString>> actions() const override;
    Kind kindOf(const QString &action) const override;
    bool showList() const override { return false; }
    bool showInterval() const override { return liveAction() == QLatin1String("set"); }
    QStringList applyLines() const override;
    QString placeholder(const QString &action) const override;
};

class LoopHost : public LivePanelHost {
public:
    LoopHost(const CommandPlan &p, const CommandCatalog::Command &c) : LivePanelHost(p, c) {}
    QList<QPair<QString, QString>> actions() const override;
    Kind kindOf(const QString &action) const override;
    bool showList() const override { return false; }
    bool showInterval() const override { return liveAction() == QLatin1String("set"); }
    QStringList applyLines() const override;
    QString placeholder(const QString &action) const override;
};

class QueueHost : public LivePanelHost {
public:
    QueueHost(const CommandPlan &p, const CommandCatalog::Command &c) : LivePanelHost(p, c) {}
    QList<QPair<QString, QString>> actions() const override;
    Kind kindOf(const QString &action) const override;
    bool showQueueEditor() const override { return true; }
    QStringList applyLines() const override;
    QString placeholder(const QString &action) const override;
};

class AgentsHost : public LivePanelHost {
public:
    AgentsHost(const CommandPlan &p, const CommandCatalog::Command &c) : LivePanelHost(p, c) {}
    QList<QPair<QString, QString>> actions() const override;
    Kind kindOf(const QString &action) const override;
    bool showBody() const override { return true; }
    QStringList applyLines() const override { return {}; }
};

class MemoryHost : public LivePanelHost {
public:
    MemoryHost(const CommandPlan &p, const CommandCatalog::Command &c) : LivePanelHost(p, c) {}
    QList<QPair<QString, QString>> actions() const override;
    Kind kindOf(const QString &action) const override;
    bool showBody() const override { return true; }
    QStringList applyLines() const override;
};

class SkillsHost : public LivePanelHost {
public:
    SkillsHost(const CommandPlan &p, const CommandCatalog::Command &c) : LivePanelHost(p, c) {}
    QList<QPair<QString, QString>> actions() const override;
    Kind kindOf(const QString &action) const override;
    bool showBody() const override { return true; }
    QStringList applyLines() const override;
    QString placeholder(const QString &action) const override;
};

class RollbackHost : public LivePanelHost {
public:
    RollbackHost(const CommandPlan &p, const CommandCatalog::Command &c) : LivePanelHost(p, c) {}
    QList<QPair<QString, QString>> actions() const override;
    Kind kindOf(const QString &action) const override;
    bool showBody() const override { return true; }
    bool showOptionAll() const override { return liveAction() == QLatin1String("restore"); }
    QStringList applyLines() const override;
    bool keepOpenFor(const QString &action) const override;
};

class SnapshotHost : public LivePanelHost {
public:
    SnapshotHost(const CommandPlan &p, const CommandCatalog::Command &c) : LivePanelHost(p, c) {}
    QList<QPair<QString, QString>> actions() const override;
    Kind kindOf(const QString &action) const override;
    QStringList applyLines() const override;
    QString placeholder(const QString &action) const override;
    bool keepOpenFor(const QString &action) const override;
};

class DebugHost : public LivePanelHost {
public:
    DebugHost(const CommandPlan &p, const CommandCatalog::Command &c) : LivePanelHost(p, c) {}
    QList<QPair<QString, QString>> actions() const override;
    Kind kindOf(const QString &action) const override;
    bool showList() const override { return false; }
    bool showBody() const override { return true; }
    QStringList applyLines() const override;
    bool keepOpenFor(const QString &action) const override;
};

class InsightsHost : public LivePanelHost {
public:
    InsightsHost(const CommandPlan &p, const CommandCatalog::Command &c) : LivePanelHost(p, c) {}
    QList<QPair<QString, QString>> actions() const override;
    Kind kindOf(const QString &action) const override;
    bool showList() const override { return false; }
    bool showBody() const override { return true; }
    bool showDays() const override { return true; }
    QStringList applyLines() const override;
};

class DiffHost : public LivePanelHost {
public:
    DiffHost(const CommandPlan &p, const CommandCatalog::Command &c) : LivePanelHost(p, c) {}
    QList<QPair<QString, QString>> actions() const override;
    Kind kindOf(const QString &action) const override;
    bool showList() const override { return false; }
    bool showBody() const override { return true; }
    bool showDiffOpts() const override { return true; }
    QStringList applyLines() const override;
    QString placeholder(const QString &action) const override;
};

class CronHost : public LivePanelHost {
public:
    CronHost(const CommandPlan &p, const CommandCatalog::Command &c) : LivePanelHost(p, c) {}
    QList<QPair<QString, QString>> actions() const override;
    Kind kindOf(const QString &action) const override;
    QStringList applyLines() const override;
};

class SuggestionsHost : public LivePanelHost {
public:
    SuggestionsHost(const CommandPlan &p, const CommandCatalog::Command &c) : LivePanelHost(p, c) {}
    QList<QPair<QString, QString>> actions() const override;
    Kind kindOf(const QString &action) const override;
    QStringList applyLines() const override;
};

class BlueprintHost : public LivePanelHost {
public:
    BlueprintHost(const CommandPlan &p, const CommandCatalog::Command &c) : LivePanelHost(p, c) {}
    QList<QPair<QString, QString>> actions() const override;
    Kind kindOf(const QString &action) const override;
    QStringList applyLines() const override;
    QString placeholder(const QString &action) const override;
};

// View-only live report (agents, bundles, plugins, toolsets, curator, kanban).
// bundles / plugins / toolsets / curator / kanban — a report plus Refresh/Close.
class ViewReportHost : public CommandHost {
public:
    ViewReportHost(const CommandPlan &p, const CommandCatalog::Command &c) : CommandHost(p, c) {}
    void build(CommandHostDialog &shell) override;
    void seedReport(const QString &report) override;
    bool dirty() const override { return true; }
    QString line() const override { return slash(); }
    bool canApply() const override { return true; }
};

// Fixed-choice picker (busy, autoprune, codex-runtime). Seed highlights the current value.
class ChoicePickerHost : public CommandHost {
public:
    ChoicePickerHost(const CommandPlan &plan, const CommandCatalog::Command &command,
                     QList<CommandPickerItem> choices)
        : CommandHost(plan, command), m_choices(std::move(choices)) {}
    void build(CommandHostDialog &shell) override;
    void seedReport(const QString &report) override;
    bool dirty() const override;
    QString line() const override;
    CommandApplyRequest request() const override;

private:
    QList<CommandPickerItem> m_choices;
};

// /subscription and /topup: report plus an Open portal button when a URL is present.
class BillingHost : public CommandHost {
public:
    BillingHost(const CommandPlan &plan, const CommandCatalog::Command &command)
        : CommandHost(plan, command) {}
    void build(CommandHostDialog &shell) override;
    void seedReport(const QString &report) override;
    bool dirty() const override { return true; }
    QString line() const override { return slash(); }
    bool canApply() const override { return true; }

private:
    void setPortal(const QString &report);
    QPushButton *m_open = nullptr;
    QString m_portalUrl;
};

// /init [notes] — optional notes, apply on Save, stay open with the report.
class InitHost : public CommandHost {
public:
    InitHost(const CommandPlan &plan, const CommandCatalog::Command &command)
        : CommandHost(plan, command) {}
    void build(CommandHostDialog &shell) override;
    void seedReport(const QString &report) override;
    bool dirty() const override { return true; }
    QString line() const override;
    CommandApplyRequest request() const override;
};

// /resume — picker of sessions from ACP listSessions, or a typed name.
class ResumeHost : public CommandHost {
public:
    ResumeHost(const CommandPlan &plan, const CommandCatalog::Command &command)
        : CommandHost(plan, command) {}
    void build(CommandHostDialog &shell) override;
    void setSessions(const QList<AcpClient::SessionInfo> &sessions);
    bool dirty() const override;
    QString line() const override;
    CommandApplyRequest request() const override;

private:
    QHash<QString, QString> m_cwdById;
};

// /usage is an Info report plus an in-dialog confirm for `reset --force`.
class UsageHost : public CommandHost {
public:
    UsageHost(const CommandPlan &plan, const CommandCatalog::Command &command)
        : CommandHost(plan, command) {}
    void build(CommandHostDialog &shell) override;
    bool dirty() const override { return true; }
    QString line() const override;
    CommandApplyRequest request() const override;
    bool canApply() const override { return true; }

private:
    QPushButton *m_reset = nullptr;
};
