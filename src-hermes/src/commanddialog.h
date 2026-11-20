#pragma once

#include "commandcatalog.h"

#include <QCheckBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
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

// The Goal dialog behind the toolbar's Goal button: set (or replace) the
// standing goal Hermes works toward across turns.
class GoalDialog : public QDialog {
    Q_OBJECT
public:
    GoalDialog(const CommandCatalog::GoalStatus &current, bool draftAvailable, QWidget *parent = nullptr);

    QString goalText() const;
    void setGoalText(const QString &text);
    bool draft() const;
    void setDraft(bool draft);
    QString commandLine() const;          // "/goal <text>" or "/goal draft <text>"

private:
    void updateState();

    QPlainTextEdit *m_text = nullptr;
    QCheckBox *m_draft = nullptr;
    QPushButton *m_set = nullptr;
    QLabel *m_preview = nullptr;
};
