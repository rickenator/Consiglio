#include "commandcatalog.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>
#include <algorithm>

namespace {

struct Known {
    const char *group;
    const char *icon;
    const char *label;
};

// Presentation only: where a command Hermes reports goes in the menus, its
// icon and a friendlier label. Whether a command EXISTS always comes from
// Hermes (ACP + /commands); an entry here for a command Hermes doesn't report
// is simply never shown.
const QHash<QString, Known> &knownCommands() {
    static const QHash<QString, Known> table = {
        // Session
        {"new", {"session", "message-square-plus", "New Conversation"}},
        {"reset", {"session", "eraser", "Reset Conversation"}},
        {"clear", {"session", "eraser", "Clear and Start Over"}},
        {"retry", {"session", "rotate-ccw", "Retry Last Message"}},
        {"undo", {"session", "undo-2", "Undo Last Turn"}},
        {"rewind", {"session", "history", "Rewind"}},
        {"title", {"session", "pencil", "Rename Session"}},
        {"branch", {"session", "git-branch", "Branch Session"}},
        {"fork", {"session", "git-fork", "Fork Session"}},
        {"compress", {"session", "minimize-2", "Compress Context"}},
        {"save", {"session", "save", "Export Conversation"}},
        {"rollback", {"session", "archive-restore", "Restore Checkpoint"}},
        {"snapshot", {"session", "archive-restore", "State Snapshot"}},
        {"stop", {"session", "octagon-x", "Stop Background Processes"}},
        {"resume", {"session", "folder-open", "Resume Named Session"}},
        {"sessions", {"session", "library", "Browse Sessions"}},
        {"history", {"session", "history", "Conversation History"}},
        {"discard", {"session", "trash-2", "Discard Lost Turn"}},
        {"queue", {"session", "list-ordered", "Queue a Prompt"}},
        {"steer", {"session", "navigation", "Steer Current Turn"}},
        {"btw", {"session", "message-circle-question", "Ask a Side Question"}},
        {"bg", {"session", "layers", "Run in Background"}},
        {"handoff", {"session", "send", "Hand Off to Messaging"}},
        // Goals & automation
        {"goal", {"automation", "target", "Goal"}},
        {"subgoal", {"automation", "list-checks", "Subgoals"}},
        {"heartbeat", {"automation", "heart-pulse", "Heartbeat"}},
        {"loop", {"automation", "repeat", "Loop a Prompt"}},
        {"plan", {"automation", "clipboard-list", "Write a Plan"}},
        {"review", {"automation", "search-check", "Independent Review"}},
        {"refine", {"automation", "sparkles", "Refine and Learn"}},
        {"moa", {"automation", "network", "Mixture of Agents"}},
        {"kanban", {"automation", "kanban", "Kanban Board"}},
        {"suggestions", {"automation", "lightbulb", "Automation Suggestions"}},
        {"blueprint", {"automation", "layout-template", "Automation Blueprint"}},
        {"cron", {"automation", "timer", "Scheduled Tasks"}},
        {"journey", {"automation", "chart-line", "Learning Journey"}},
        // Model & behavior
        {"model", {"model", "cpu", "Switch Model"}},
        {"reasoning", {"model", "brain", "Reasoning"}},
        {"fast", {"model", "zap", "Fast Mode"}},
        {"personality", {"model", "drama", "Personality"}},
        {"codex-runtime", {"model", "toggle-left", "Codex Runtime"}},
        {"yolo", {"model", "triangle-alert", "Toggle YOLO Mode"}},
        {"approvals", {"model", "shield-check", "Approval Mode"}},
        {"busy", {"model", "hourglass", "Busy Input Mode"}},
        {"voice", {"model", "mic", "Voice Mode"}},
        {"footer", {"model", "panel-bottom", "Runtime Footer"}},
        {"autoprune", {"model", "trash-2", "Auto-prune Sessions"}},
        // Tools, skills & memory
        {"tools", {"tools", "wrench", "Tools"}},
        {"toolsets", {"tools", "hammer", "Toolsets"}},
        {"skills", {"tools", "puzzle", "Skills"}},
        {"bundles", {"tools", "package", "Skill Bundles"}},
        {"memory", {"tools", "brain-circuit", "Memory Review"}},
        {"learn", {"tools", "graduation-cap", "Learn a Skill"}},
        {"init", {"tools", "file-code", "Generate AGENTS.md"}},
        {"curator", {"tools", "wand-sparkles", "Skill Curator"}},
        {"reload-mcp", {"tools", "plug", "Reload MCP Servers"}},
        {"reload-skills", {"tools", "refresh-cw", "Reload Skills"}},
        {"reload", {"tools", "refresh-cw", "Reload Environment"}},
        {"browser", {"tools", "monitor", "Browser Connection"}},
        {"plugins", {"tools", "plug", "Plugins"}},
        // Info & diagnostics
        {"help", {"info", "circle-help", "Hermes Help"}},
        {"commands", {"info", "command", "All Commands"}},
        {"status", {"info", "activity", "Session Status"}},
        {"context", {"info", "gauge", "Context Window"}},
        {"usage", {"info", "chart-column", "Token Usage"}},
        {"insights", {"info", "chart-line", "Usage Insights"}},
        {"version", {"info", "info", "Hermes Version"}},
        {"debug", {"info", "bug", "Debug Report"}},
        {"profile", {"info", "id-card", "Active Profile"}},
        {"whoami", {"info", "user-round", "Access Level"}},
        {"diff", {"info", "file-diff", "Git Changes"}},
        {"agents", {"info", "users", "Running Agents"}},
        {"egress", {"info", "shield", "Egress Proxy"}},
        // Gateway & account
        {"start", {"gateway", "rocket", "Start Ping"}},
        {"topic", {"gateway", "hash", "Topic Sessions"}},
        {"sethome", {"gateway", "house", "Set Home Channel"}},
        {"restart", {"gateway", "power", "Restart Gateway"}},
        {"platform", {"gateway", "radio", "Gateway Platforms"}},
        {"pause", {"gateway", "circle-pause", "Pause All Work"}},
        {"approve", {"gateway", "circle-check", "Approve Pending Command"}},
        {"deny", {"gateway", "circle-x", "Deny Pending Command"}},
        {"login", {"gateway", "log-in", "Sign In to Nous"}},
        {"topup", {"gateway", "wallet", "Nous Balance"}},
        {"update", {"gateway", "download", "Update Hermes"}},
    };
    return table;
}

// Words that name a value in an argument hint rather than a literal choice.
const QSet<QString> &placeholderWords() {
    static const QSet<QString> words = {
        "text", "name", "prompt", "model", "level", "number", "n", "id", "reason", "path",
        "days", "page", "subcommand", "interval", "question", "task", "guidance", "notes",
        "instructions", "file", "filename", "query", "value", "slug", "description",
    };
    return words;
}

// Top-level alternatives inside [...] or <...> groups that contain a "|".
QStringList topLevelAlternatives(const QString &hint) {
    QStringList out;
    int depth = 0;
    int start = -1;
    for (int i = 0; i < hint.size(); ++i) {
        const QChar c = hint.at(i);
        if (c == '[' || c == '<' || c == '(') {
            if (depth == 0) start = i + 1;
            ++depth;
        } else if ((c == ']' || c == '>' || c == ')') && depth > 0) {
            --depth;
            if (depth == 0 && start >= 0) {
                const QString inner = hint.mid(start, i - start);
                if (inner.contains('|')) {
                    // Split on "|" at nesting depth 0 within the group.
                    int d = 0, from = 0;
                    for (int j = 0; j <= inner.size(); ++j) {
                        const QChar ch = j < inner.size() ? inner.at(j) : QChar('|');
                        if (ch == '[' || ch == '<' || ch == '(') ++d;
                        else if (ch == ']' || ch == '>' || ch == ')') --d;
                        else if (ch == '|' && d == 0) {
                            out << inner.mid(from, j - from).trimmed();
                            from = j + 1;
                        }
                    }
                }
                start = -1;
            }
        }
    }
    // Alternatives at the top level too: "<prompt> [opts] | status | pause".
    {
        QStringList top;
        int d = 0, from = 0;
        for (int j = 0; j <= hint.size(); ++j) {
            const QChar ch = j < hint.size() ? hint.at(j) : QChar('|');
            if (ch == '[' || ch == '<' || ch == '(') ++d;
            else if (ch == ']' || ch == '>' || ch == ')') --d;
            else if (ch == '|' && d == 0) {
                top << hint.mid(from, j - from).trimmed();
                from = j + 1;
            }
        }
        if (top.size() > 1) out += top;
    }
    return out;
}

QString humanize(const QString &name) {
    QStringList words = name.split(QRegularExpression(QStringLiteral("[-_:.]+")), Qt::SkipEmptyParts);
    static const QSet<QString> acronyms = {"mcp", "ai", "pdf", "html", "api", "ui", "cli", "acp",
                                           "sdlc", "moa", "gif", "svg", "tts", "url"};
    for (QString &w : words) {
        if (acronyms.contains(w.toLower())) w = w.toUpper();
        else if (!w.isEmpty()) w[0] = w.at(0).toUpper();
    }
    return words.join(' ');
}

} // namespace

// ─── Command ──────────────────────────────────────────────────────────────

bool CommandCatalog::Command::requiresArgs() const {
    return argsHint.trimmed().startsWith('<');
}

QStringList CommandCatalog::Command::choices() const {
    QStringList out;
    static const QRegularExpression bareWord(QStringLiteral("^[a-z][a-z0-9-]*$"));
    for (const QString &alt : topLevelAlternatives(argsHint)) {
        if (!bareWord.match(alt).hasMatch()) continue;
        if (placeholderWords().contains(alt)) continue;
        if (!out.contains(alt)) out << alt;
    }
    return out;
}

// ─── Catalog ──────────────────────────────────────────────────────────────

CommandCatalog::CommandCatalog(QObject *parent) : QObject(parent) {}

QList<CommandCatalog::Group> CommandCatalog::groups() {
    return {
        {"session", tr("Session"), "messages-square"},
        {"model", tr("Model & Behavior"), "cpu"},
        {"tools", tr("Tools, Skills & Memory"), "wrench"},
        {"automation", tr("Planning & Automation"), "list-checks"},
        {"info", tr("Info & Diagnostics"), "activity"},
        {"gateway", tr("Gateway & Account"), "radio"},
        {"skills", tr("Skills"), "puzzle"},
        {"more", tr("More"), "sparkle"},
    };
}

CommandCatalog::Group CommandCatalog::group(const QString &id) {
    for (const Group &g : groups()) {
        if (g.id == id) return g;
    }
    return groups().last();
}

QString CommandCatalog::groupFor(const QString &name, bool skill) {
    const auto it = knownCommands().constFind(name);
    if (it != knownCommands().constEnd()) return QString::fromLatin1(it->group);
    return skill ? QStringLiteral("skills") : QStringLiteral("more");
}

QString CommandCatalog::iconFor(const QString &name, const QString &groupId) {
    const auto it = knownCommands().constFind(name);
    if (it != knownCommands().constEnd()) return QString::fromLatin1(it->icon);
    return group(groupId).icon;
}

QString CommandCatalog::labelFor(const QString &name) {
    const auto it = knownCommands().constFind(name);
    if (it != knownCommands().constEnd()) return QString::fromLatin1(it->label);
    return humanize(name);
}

void CommandCatalog::decorate(Command &cmd) const {
    cmd.group = groupFor(cmd.name, cmd.isSkill());
    cmd.icon = iconFor(cmd.name, cmd.group);
    cmd.label = labelFor(cmd.name);
}

void CommandCatalog::upsert(Command cmd) {
    auto it = m_commands.find(cmd.name);
    if (it == m_commands.end()) {
        decorate(cmd);
        m_commands.insert(cmd.name, cmd);
    } else {
        Command &existing = it.value();
        // The registry listing is the richer description; ACP's input hint is
        // kept alongside it.
        if (!cmd.description.isEmpty() && (cmd.sources & Listed || existing.description.isEmpty())) {
            existing.description = cmd.description;
        }
        if (!cmd.argsHint.isEmpty()) existing.argsHint = cmd.argsHint;
        if (!cmd.inputHint.isEmpty()) existing.inputHint = cmd.inputHint;
        if (!cmd.aliases.isEmpty()) existing.aliases = cmd.aliases;
        existing.sources |= cmd.sources;
        decorate(existing);
    }
    for (const QString &alias : cmd.aliases) m_aliases.insert(alias, cmd.name);
}

void CommandCatalog::setAdvertised(const QList<AdvertisedCommand> &advertised) {
    for (auto &cmd : m_commands) cmd.sources &= ~Advertised;
    for (const auto &a : advertised) {
        QString name = a.name.trimmed();
        if (name.startsWith('/')) name.remove(0, 1);
        if (name.isEmpty()) continue;
        Command cmd;
        cmd.name = name;
        cmd.description = a.description;
        cmd.inputHint = a.hint;
        cmd.sources = Advertised;
        upsert(cmd);
    }
    // Drop commands no source vouches for any more.
    for (auto it = m_commands.begin(); it != m_commands.end();) {
        if (it->sources == 0) it = m_commands.erase(it);
        else ++it;
    }
    emit changed();
}

void CommandCatalog::beginListing() {
    m_listedThisPass.clear();
    m_listingComplete = false;
}

int CommandCatalog::addListedPage(const Page &page) {
    if (!page.valid) return 0;
    for (Command cmd : page.commands) {
        cmd.sources |= Listed;
        m_listedThisPass.insert(cmd.name);
        upsert(cmd);
    }
    const bool more = page.page > 0 && page.pageCount > page.page;
    if (!more) {
        // A full pass completed: whatever Hermes no longer lists (and ACP no
        // longer advertises) was removed by an update — drop it, and drop the
        // cached-only flag from everything Hermes confirmed.
        for (auto it = m_commands.begin(); it != m_commands.end();) {
            if (!m_listedThisPass.contains(it->name)) it->sources &= ~(Listed | Skill | Cached);
            else it->sources &= ~Cached;
            if (it->sources == 0) it = m_commands.erase(it);
            else ++it;
        }
        QHash<QString, QString> aliases;
        for (const auto &cmd : qAsConst(m_commands)) {
            for (const QString &a : cmd.aliases) aliases.insert(a, cmd.name);
        }
        m_aliases = aliases;
        m_listingComplete = true;
    }
    emit changed();
    return more ? page.page + 1 : 0;
}

CommandCatalog::Page CommandCatalog::parsePage(const QString &text) {
    Page page;
    static const QRegularExpression header(
        QStringLiteral("\\*\\*Commands\\*\\*\\s*\\((\\d+)\\s+total,\\s*page\\s+(\\d+)\\s*/\\s*(\\d+)\\)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression line(
        QStringLiteral("^\\s*[-*•]?\\s*`/([A-Za-z0-9][\\w:.+-]*)(?:\\s+([^`]*?))?\\s*`\\s*(?:--|—|–|:)\\s*(.*?)\\s*$"));
    static const QRegularExpression aliasTail(
        QStringLiteral("\\s*\\((?:alias|aliases):\\s*([^)]*)\\)\\s*$"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression aliasName(QStringLiteral("`/([^`\\s]+)`"));

    const auto h = header.match(text);
    if (h.hasMatch()) {
        page.total = h.captured(1).toInt();
        page.page = h.captured(2).toInt();
        page.pageCount = h.captured(3).toInt();
        page.valid = true;
    }
    bool skills = false;
    for (const QString &raw : text.split('\n')) {
        if (raw.contains(QLatin1String("Skill Commands"), Qt::CaseInsensitive)) {
            skills = true;
            continue;
        }
        const auto m = line.match(raw);
        if (!m.hasMatch()) continue;
        Command cmd;
        cmd.name = m.captured(1);
        cmd.argsHint = m.captured(2).trimmed();
        QString description = m.captured(3);
        const auto a = aliasTail.match(description);
        if (a.hasMatch()) {
            auto names = aliasName.globalMatch(a.captured(1));
            while (names.hasNext()) cmd.aliases << names.next().captured(1);
            description = description.left(a.capturedStart()).trimmed();
        }
        cmd.description = description;
        cmd.sources = skills ? Skill : 0;
        page.commands.append(cmd);
    }
    if (!page.commands.isEmpty()) page.valid = true;
    return page;
}

// ─── Queries ──────────────────────────────────────────────────────────────

QList<CommandCatalog::Command> CommandCatalog::commands() const {
    QList<Command> out = m_commands.values();
    QHash<QString, int> order;
    const auto gs = groups();
    for (int i = 0; i < gs.size(); ++i) order.insert(gs[i].id, i);
    std::sort(out.begin(), out.end(), [&](const Command &a, const Command &b) {
        const int ga = order.value(a.group, 99), gb = order.value(b.group, 99);
        if (ga != gb) return ga < gb;
        return a.label.compare(b.label, Qt::CaseInsensitive) < 0;
    });
    return out;
}

QList<CommandCatalog::Command> CommandCatalog::commandsInGroup(const QString &groupId) const {
    QList<Command> out;
    for (const auto &cmd : commands()) {
        if (cmd.group == groupId) out.append(cmd);
    }
    return out;
}

bool CommandCatalog::contains(const QString &name) const {
    QString n = name;
    if (n.startsWith('/')) n.remove(0, 1);
    return m_commands.contains(n) || m_aliases.contains(n);
}

CommandCatalog::Command CommandCatalog::command(const QString &name) const {
    QString n = name;
    if (n.startsWith('/')) n.remove(0, 1);
    if (m_commands.contains(n)) return m_commands.value(n);
    if (m_aliases.contains(n)) return m_commands.value(m_aliases.value(n));
    return Command();
}

// ─── Cache ────────────────────────────────────────────────────────────────

void CommandCatalog::saveCache(const QString &agentVersion) const {
    QJsonArray arr;
    for (const auto &cmd : m_commands) {
        QJsonObject o;
        o["name"] = cmd.name;
        o["description"] = cmd.description;
        o["argsHint"] = cmd.argsHint;
        o["inputHint"] = cmd.inputHint;
        o["aliases"] = QJsonArray::fromStringList(cmd.aliases);
        o["skill"] = cmd.isSkill();
        arr.append(o);
    }
    QSettings settings;
    settings.setValue("hermes/commandCatalog", QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
    settings.setValue("hermes/commandCatalogVersion", agentVersion);
}

bool CommandCatalog::loadCache(QString *cachedVersion) {
    QSettings settings;
    const QByteArray json = settings.value("hermes/commandCatalog").toString().toUtf8();
    if (json.isEmpty()) return false;
    const QJsonArray arr = QJsonDocument::fromJson(json).array();
    if (arr.isEmpty()) return false;
    for (const auto &v : arr) {
        const QJsonObject o = v.toObject();
        Command cmd;
        cmd.name = o.value("name").toString();
        if (cmd.name.isEmpty()) continue;
        cmd.description = o.value("description").toString();
        cmd.argsHint = o.value("argsHint").toString();
        cmd.inputHint = o.value("inputHint").toString();
        for (const auto &a : o.value("aliases").toArray()) cmd.aliases << a.toString();
        cmd.sources = Cached | (o.value("skill").toBool() ? Skill : 0);
        upsert(cmd);
    }
    if (cachedVersion) *cachedVersion = settings.value("hermes/commandCatalogVersion").toString();
    emit changed();
    return true;
}

void CommandCatalog::clearCache() {
    QSettings settings;
    settings.remove("hermes/commandCatalog");
    settings.remove("hermes/commandCatalogVersion");
}
