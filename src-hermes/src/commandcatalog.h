#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

// Everything Hermes can do from a slash command, discovered from Hermes
// itself — never a hard-coded list:
//
//   1. ACP `available_commands_update` — the commands the ACP adapter
//      advertises for the session (name, description, input hint).
//   2. Hermes's own catalog, `/commands [page]`, answered by the running
//      agent: every registry command (with its argument syntax and aliases)
//      plus the installed skill commands. CommandCatalog parses those pages;
//      MainWindow fetches them quietly after a session opens.
//
// The two are merged by name. New commands a `hermes update` adds therefore
// show up on their own; commands it removes disappear on the next refresh.
//
// The only static knowledge here is presentation: which menu group a known
// command belongs to, its icon, and a friendlier label. Commands without an
// entry fall into a sensible group (skills under Skills, the rest under
// "More") with a generic icon, so nothing is ever hidden.
class CommandCatalog : public QObject {
    Q_OBJECT
public:
    enum Source { Advertised = 0x1, Listed = 0x2, Skill = 0x4, Cached = 0x8 };

    struct Command {
        QString name;          // without the leading "/"
        QString description;
        QString argsHint;      // e.g. "[on|off|status]", "<prompt>", "" for none
        QString inputHint;     // ACP input.hint, prose ("model name to switch to")
        QStringList aliases;   // without "/"
        int sources = 0;

        QString group;         // group id (see groups())
        QString icon;          // icon name in :/icons
        QString label;         // human label for menus ("Undo Last Turn")

        bool takesArgs() const { return !argsHint.isEmpty() || !inputHint.isEmpty(); }
        // "<…>" up front means Hermes needs an argument; "[…]" is optional.
        bool requiresArgs() const;
        // Bare-word alternatives from the hint, e.g. "[on|off|status]" →
        // {on, off, status}; "[text | pause | resume]" → {pause, resume}.
        QStringList choices() const;
        bool isSkill() const { return sources & Skill; }
    };

    struct Group {
        QString id;
        QString title;
        QString icon;
    };

    struct Page {
        QList<Command> commands;
        int page = 0;
        int pageCount = 0;
        int total = 0;
        bool valid = false;    // it looked like a /commands page at all
    };

    explicit CommandCatalog(QObject *parent = nullptr);

    // ── Sources ──────────────────────────────────────────────────────────
    struct AdvertisedCommand {
        QString name;
        QString description;
        QString hint;
    };
    void setAdvertised(const QList<AdvertisedCommand> &commands);
    // Merge one parsed /commands page. Returns the next page to fetch, or 0
    // when the catalog is complete.
    int addListedPage(const Page &page);
    // Start a fresh discovery pass (commands not re-listed are dropped when it
    // completes, so removals after an update take effect).
    void beginListing();
    bool listingComplete() const { return m_listingComplete; }

    // Parse Hermes's "/commands [page]" reply (markdown).
    static Page parsePage(const QString &text);

    // ── Cache (so menus are populated instantly on the next launch) ──────
    void saveCache(const QString &agentVersion) const;
    // Loads the cached catalog. Returns true if one existed; `cachedVersion`
    // tells the caller whether it came from the running Hermes version.
    bool loadCache(QString *cachedVersion = nullptr);
    static void clearCache();

    // ── Queries ──────────────────────────────────────────────────────────
    QList<Command> commands() const;              // all, sorted by group order then name
    QList<Command> commandsInGroup(const QString &groupId) const;
    bool contains(const QString &name) const;
    Command command(const QString &name) const;   // resolves aliases too
    int count() const { return m_commands.size(); }
    static QList<Group> groups();                 // ordered menu groups
    static Group group(const QString &id);

    // Static presentation knowledge (exposed for tests).
    static QString groupFor(const QString &name, bool skill);
    static QString iconFor(const QString &name, const QString &group);
    static QString labelFor(const QString &name);

signals:
    void changed();

private:
    void upsert(Command cmd);
    void decorate(Command &cmd) const;

    QHash<QString, Command> m_commands;           // by name
    QHash<QString, QString> m_aliases;            // alias -> name
    QSet<QString> m_listedThisPass;
    bool m_listingComplete = false;
};
