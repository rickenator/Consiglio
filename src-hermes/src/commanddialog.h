#pragma once

#include "commandcatalog.h"

#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

// Runs one Hermes slash command that takes arguments: shows what it does,
// its syntax (exactly as Hermes reports it), one-click choices parsed from
// that syntax, and a live preview of the line that will be sent.
class CommandDialog : public QDialog {
    Q_OBJECT
public:
    explicit CommandDialog(const CommandCatalog::Command &command, QWidget *parent = nullptr);

    QString arguments() const;
    void setArguments(const QString &args);
    QString commandLine() const;          // "/name args"
    bool canRun() const;

private:
    void updatePreview();

    CommandCatalog::Command m_command;
    QLineEdit *m_args = nullptr;
    QLabel *m_preview = nullptr;
    QPushButton *m_run = nullptr;
};
