#pragma once

#include <QElapsedTimer>
#include <QFrame>
#include <QHash>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace ui { class Spinner; class ElidedLabel; }

// A single rendered conversation block. Kinds:
//   User     — right-aligned bubble (the prompt you sent); slash commands are
//              shown as a monospace command pill instead
//   Agent    — Hermes's answer: streams as plain text, then renders markdown
//   Thinking — collapsible reasoning stream
//   Tool     — a tool call: icon, title, status badge, collapsible output
//   Notice   — a centred one-line system note (restored, failed, updated…)
class ChatBlock : public QWidget {
    Q_OBJECT
public:
    enum Kind { User, Agent, Thinking, Tool, Notice };
    enum Level { Info, Success, Warning, Error };

    explicit ChatBlock(Kind kind, int chatPx = 16, int thinkingPx = 15,
                       int toolPx = 13, QWidget *parent = nullptr);
    ~ChatBlock() override;

    Kind kind() const;
    QString toolId() const;

    // Re-apply the per-kind font sizes (live restyle of an existing block).
    void applyFontSizes(int chatPx, int thinkingPx, int toolPx);
    // Re-apply theme colours/icons (after a light/dark switch).
    void restyle();

    // Append a streamed text delta to the block's body.
    void appendText(const QString &text);
    // Finalize: render accumulated agent/thinking text as markdown.
    void finalize();
    bool isFinalized() const { return m_finalized; }
    // Tool-specific updates.
    void setToolInfo(const QString &title, const QString &kind);
    void setToolStatus(const QString &status, const QString &output);
    QString toolStatus() const { return m_toolStatus; }
    bool isExpanded() const { return !m_collapsed; }
    void setExpanded(bool expanded);
    // Notice level.
    void setLevel(Level level);
    // Grow the thinking/agent body to fit its content (used while streaming).
    void resizeToFit();

    QString plainText() const;

protected:
    void resizeEvent(QResizeEvent *e) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void build();
    void styleMarkdown();
    void fitBody(bool growOnly);
    void updateToggle();

    Kind m_kind;
    int m_chatPx, m_thinkingPx, m_toolPx;
    QWidget *m_body = nullptr;        // QLabel (user/notice) or QTextEdit (others)
    QFrame *m_card = nullptr;
    QLabel *m_icon = nullptr;
    QToolButton *m_toggleBtn = nullptr;
    QToolButton *m_copyBtn = nullptr;
    ui::ElidedLabel *m_toolTitle = nullptr;
    QLabel *m_badge = nullptr;
    QLabel *m_avatar = nullptr;
    QFrame *m_bubble = nullptr;
    QWidget *m_header = nullptr;
    QLabel *m_time = nullptr;
    bool m_collapsed = false;
    bool m_finalized = false;
    bool m_isCommand = false;
    Level m_level = Info;
    QString m_toolId;
    QString m_toolKind;
    QString m_toolStatus;
    QString m_accumulated;            // raw text accumulated during streaming
    int m_streamHeight = 0;           // fixed body height while streaming (only grows)
    QElapsedTimer m_started;
    int m_thoughtSecs = -1;

    friend class ChatPane;
};

// The conversation pane: an optional banner, a scrollable column of
// ChatBlocks (or an empty state), and the composer. It owns the "active
// block" state machine that routes streamed ACP events into the right blocks
// in reading order (thinking → tool → thinking → … → answer).
class ChatPane : public QWidget {
    Q_OBJECT

public:
    explicit ChatPane(QWidget *parent = nullptr);

    // Apply per-balloon font sizes to every existing block (live restyle).
    void applyFontSizes(int chatPx, int thinkingPx, int toolPx);

    struct QuickAction {
        QString id;
        QString label;
        QString description;
        QString icon;
        QString shortcut;     // shown in the tooltip
        bool enabled = true;
    };
    // The empty state shown before the first message.
    void setEmptyState(const QString &title, const QString &subtitle,
                       const QList<QuickAction> &actions, bool spinner = false);

    struct BannerAction { QString id; QString label; bool primary = false; };
    // A persistent banner above the conversation (connection lost, setup
    // needed…). level: "info" | "warning" | "error".
    void showBanner(const QString &level, const QString &title, const QString &message,
                    const QList<BannerAction> &actions = {});
    void hideBanner();
    bool isBannerVisible() const;
    QString bannerTitle() const;

    // Composer availability (no session yet, disconnected…).
    void setComposerEnabled(bool enabled, const QString &placeholder = QString());
    void focusComposer();
    QString composerText() const;
    void setComposerText(const QString &text);
    bool hasConversation() const;
    int blockCount() const;
    QList<ChatBlock *> blocks() const;

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
    void stopRequested();
    void quickActionTriggered(const QString &id);
    void bannerActionTriggered(const QString &id);
    void commandsButtonClicked();

public slots:
    void appendUserMessage(const QString &text);
    void appendNotice(const QString &text, ChatBlock::Level level = ChatBlock::Info);
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
    void setBusy(bool busy, const QString &label = QString());
    void clearConversation();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void setupUI();
    void addBlock(ChatBlock *block);
    ChatBlock *activeBlock(ChatBlock::Kind kind);
    void closeActiveBlocks();
    void scrollToBottom();
    void submit();
    void updateComposerHeight();
    void updateSendButton();
    void updateEmptyVisibility();
    void restyle();

    QFrame *m_banner = nullptr;
    QLabel *m_bannerIcon = nullptr;
    QLabel *m_bannerTitle = nullptr;
    QLabel *m_bannerText = nullptr;
    QWidget *m_bannerActions = nullptr;
    QString m_bannerLevel;

    QScrollArea *m_scroll = nullptr;
    QWidget *m_host = nullptr;
    QWidget *m_column = nullptr;
    QVBoxLayout *m_layout = nullptr;

    QWidget *m_empty = nullptr;
    QLabel *m_emptyLogo = nullptr;
    ui::Spinner *m_emptySpinner = nullptr;
    QLabel *m_emptyTitle = nullptr;
    QLabel *m_emptySubtitle = nullptr;
    QWidget *m_emptyActions = nullptr;
    QList<QuickAction> m_quickActions;

    int m_chatPx = 16;
    int m_thinkingPx = 15;
    int m_toolPx = 13;

    QFrame *m_composerCard = nullptr;
    QPlainTextEdit *m_input = nullptr;
    QToolButton *m_sendBtn = nullptr;
    QToolButton *m_commandsBtn = nullptr;
    QLabel *m_hint = nullptr;
    ui::Spinner *m_spinner = nullptr;
    QLabel *m_busyLabel = nullptr;
    QString m_busyText;
    QTimer *m_busyTimer = nullptr;
    QElapsedTimer m_busyClock;
    bool m_busy = false;
    bool m_composerEnabled = true;
    QString m_placeholder;
    QStringList m_history;            // sent prompts, for Up/Down recall
    int m_historyPos = -1;
    QWidget *m_stackEmptyPage = nullptr;
    QStackedWidget *m_stack = nullptr;

    ChatBlock *m_activeThinking = nullptr;
    ChatBlock *m_activeAgent = nullptr;
    QHash<QString, ChatBlock *> m_toolBlocks;

    // Sticky auto-scroll: while a turn is streaming we pin the viewport to the
    // bottom so new text appears at the bottom of the panel. If the user scrolls
    // UP to read, we stop yanking them (m_followingBottom = false) and resume
    // following only when they return to the bottom or start a new turn.
    bool m_followingBottom = true;
};
