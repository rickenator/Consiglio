#pragma once

#include "commandcatalog.h"

#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

// Auxiliary Command console: send a raw /command to the chat. Used for
// unmapped commands, commands a Hermes update adds, and anything Consiglio
// does not yet wrap in a native dialog. Native surfaces live in CommandHostDialog.
class CommandDialog : public QDialog {
    Q_OBJECT
public:
    explicit CommandDialog(const CommandCatalog::Command &command, QWidget *parent = nullptr);
    // Empty console, optionally prefilled with a / line the user can edit.
    static CommandDialog *console(QWidget *parent, const QString &prefill = QString());

    QString arguments() const;
    void setArguments(const QString &args);
    QString commandLine() const;          // "/name args" (or the typed line in freeform)
    bool canRun() const;
    bool isFreeform() const { return m_freeform; }

private:
    void updatePreview();

    CommandCatalog::Command m_command;
    bool m_freeform = false;
    QLineEdit *m_args = nullptr;
    QLabel *m_preview = nullptr;
    QPushButton *m_run = nullptr;
};
