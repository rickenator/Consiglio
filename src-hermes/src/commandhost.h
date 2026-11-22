#pragma once

#include "commandui.h"

#include <QList>
#include <QString>

class CommandHostDialog;
class QVBoxLayout;
class QWidget;

// One command's UI lives in one CommandHost.
//
// The dialog shell (CommandHostDialog) owns the chrome every command shares:
// header, description, error banner, busy row, Save/Cancel and the apply
// lifecycle. A CommandHost owns the part that is specific to its command: the
// body widgets, what counts as a change, the exact line Hermes receives when
// no structured ACP method applies, and how the change is requested.
//
// The point of the split is that adding a native surface means adding a host
// and one row in createCommandHost() — not another branch in a switch, and not
// another shared member widget in the shell. Unknown commands still work: the
// factory falls back to the generic host for the plan's archetype, and names
// with no plan at all never reach a dialog (they use the Command console).
class CommandHost {
public:
    virtual ~CommandHost() = default;

    // Build the body. Called once by the shell right after its chrome exists;
    // the shell outlives the host.
    virtual void build(CommandHostDialog &shell) = 0;

    virtual CommandArchetype archetype() const { return m_plan.archetype; }

    // Seed from a quiet `/command` report. Default: ignore.
    virtual void seedReport(const QString &report) { Q_UNUSED(report) }
    // Seed a picker from ACP session configuration. Default: ignore.
    virtual void seedConfig(const QList<AcpClient::ConfigOption> &options, const QStringList &models) {
        Q_UNUSED(options)
        Q_UNUSED(models)
    }
    // Seed command-specific state (the current session title, say). Default: ignore.
    virtual void seedSessionTitle(const QString &title) { Q_UNUSED(title) }

    // Does the body hold a change worth applying?
    virtual bool dirty() const = 0;

    // The exact line Hermes receives for this command. Never shown to the user
    // as a CLI string: it is the transport, not the interface.
    virtual QString line() const = 0;

    // How the change is applied. The default maps the plan onto one request
    // (method + config id + line); hosts override to add their own arguments.
    virtual CommandApplyRequest request() const;

    virtual bool canApply() const { return dirty(); }

protected:
    CommandHost(const CommandPlan &plan, const CommandCatalog::Command &command)
        : m_plan(plan), m_command(command) {}

    const CommandPlan &plan() const { return m_plan; }
    const CommandCatalog::Command &command() const { return m_command; }
    // "name" without the leading slash; falls back to the plan's name.
    QString name() const;
    QString slash() const;
    CommandHostDialog *shell() const { return m_shell; }

    CommandPlan m_plan;
    CommandCatalog::Command m_command;
    CommandHostDialog *m_shell = nullptr;
};

// Build the host for a discovered (or typed) command. Returns nullptr for
// archetypes that have no dialog at all (AppLink, Passthrough) — those are
// routed to an existing surface or the Command console by MainWindow.
CommandHost *createCommandHost(const CommandPlan &plan, const CommandCatalog::Command &command);
