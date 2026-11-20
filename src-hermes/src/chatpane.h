#pragma once

#include <QWidget>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QTextEdit>
#include <QTimer>
#include <QHash>

// A single rendered conversation block. Kinds:
//   user     — right-aligned bubble (the prompt you sent)
//   agent    — left-aligned rich markdown, streams live then renders
//   thinking — collapsible reasoning stream
//   tool     — a tool call: title + status + output, collapsible
class ChatBlock : public QWidget {
    Q_OBJECT
public:
    enum Kind { User, Agent, Thinking, Tool };

    explicit ChatBlock(Kind kind, int chatPx = 16, int thinkingPx = 15,
                       int toolPx = 13, QWidget *parent = nullptr);
    ~ChatBlock() override;

    Kind kind() const;
    QString toolId() const;

    // Re-apply the per-kind font sizes (live restyle of an existing block).
    void applyFontSizes(int chatPx, int thinkingPx, int toolPx);

    // Append a streamed text delta to the block's body.
    void appendText(const QString &text);
    // Finalize: render accumulated agent/thinking text as markdown.
    void finalize();
    // Tool-specific updates.
    void setToolStatus(const QString &status, const QString &output);
    // Grow the thinking/agent body to fit its content (used while streaming).
    void resizeToFit();

    QString plainText() const;

private:
    void build(Kind kind, int chatPx, int thinkingPx, int toolPx);

    Kind m_kind;
    QWidget *m_body = nullptr;        // QLabel (user) or QTextEdit (agent/thinking/tool)
    QPushButton *m_toggleBtn = nullptr;
    bool m_collapsed = false;
    QString m_toolId;
    QString m_accumulated;            // raw text accumulated during streaming
    int m_streamHeight = 0;           // fixed body height while streaming (only grows)
};

// The conversation pane: a scrollable stack of ChatBlocks above a composer.
// It owns the "active block" state machine that routes streamed ACP events
// into the right blocks in reading order (thinking → tool → thinking → … → answer).
class ChatPane : public QWidget {
    Q_OBJECT

public:
    explicit ChatPane(QWidget *parent = nullptr);

    // Apply per-balloon font sizes to every existing block (live restyle).
    void applyFontSizes(int chatPx, int thinkingPx, int toolPx);

signals:
    void promptSubmitted(const QString &text);
    // Emitted when the user types "/" as the FIRST character of an empty
    // composer — opens the command palette.
    void slashCommandRequested();
    // Emitted when the user types "/" at any NON-first position — opens the
    // file-system picker (Hermes CLI behavior). Carries the cursor column where
    // the "/" was typed so the owner can insert the chosen path there. `relative`
    // is true when the user typed "./" (a preceding dot) — meaning "current
    // directory": the picker roots at cwd and the chosen path is inserted as
    // "./rel". A bare "/" roots at the filesystem root and inserts an absolute
    // path.
    void filePathRequested(int column, bool relative);

public slots:
    void appendUserMessage(const QString &text);
    // Insert `path` into the composer at `column` (where the "/" was typed),
    // replacing that character, then return focus to the composer. When
    // `replaceDot` is true the user typed "./" — the dot before the "/" is also
    // replaced (the whole "./" becomes the chosen path).
    void insertPathAt(int column, const QString &path, bool replaceDot = false);
    void appendAgentChunk(const QString &text);
    void appendThoughtChunk(const QString &text);
    void addToolCall(const QString &id, const QString &title, const QString &kind);
    void updateToolCall(const QString &id, const QString &status, const QString &output);
    void finalizeTurn();
    void setBusy(bool busy);
    void clearConversation();

private:
    void setupUI();
    ChatBlock *activeBlock(ChatBlock::Kind kind);
    void closeActiveBlocks();
    void scrollToBottom();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

    QScrollArea *m_scroll = nullptr;
    QWidget *m_host = nullptr;
    QVBoxLayout *m_layout = nullptr;
    QLabel *m_emptyLabel = nullptr;

    int m_chatPx = 16;
    int m_thinkingPx = 15;
    int m_toolPx = 13;

    QLineEdit *m_input = nullptr;
    QPushButton *m_sendBtn = nullptr;
    QLabel *m_statusIndicator = nullptr;
    QTimer *m_thinkTimer = nullptr;
    int m_frame = 0;
    bool m_busy = false;

    ChatBlock *m_activeThinking = nullptr;
    ChatBlock *m_activeAgent = nullptr;
    QHash<QString, ChatBlock *> m_toolBlocks;

    // Sticky auto-scroll: while a turn is streaming we pin the viewport to the
    // bottom so new text appears at the bottom of the panel. If the user scrolls
    // UP to read, we stop yanking them (m_followingBottom = false) and resume
    // following only when they return to the bottom or start a new turn.
    bool m_followingBottom = true;
};
