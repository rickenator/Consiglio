#pragma once

#include <QObject>
#include <QSettings>

// Per-balloon font sizes for the chat pane, persisted across launches.
// Three independent controls:
//   chat     — agent answer text + user bubbles
//   thinking — reasoning stream
//   tool     — tool-call title + output
class FontSettings : public QObject {
    Q_OBJECT
public:
    explicit FontSettings(QObject *parent = nullptr);

    int chatPx() const;
    int thinkingPx() const;
    int toolPx() const;

    // Apply a size and persist it. Emits changed() so the pane can restyle.
    void setChatPx(int px);
    void setThinkingPx(int px);
    void setToolPx(int px);

    // Restore the defaults (16 / 15 / 13) without emitting changed().
    void resetToDefaults();

signals:
    void changed();

private:
    int m_chat;
    int m_thinking;
    int m_tool;
};
