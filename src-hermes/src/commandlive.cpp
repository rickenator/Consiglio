#include "commandui.h"

#include <QRegularExpression>

// Parsers and seed lists for live-panel commands. Output formats follow
// Hermes 0.23.3 but are matched loosely so a later wording change still
// fills the list when rows look numbered or id-tagged.

namespace {

QString firstMeaningfulLine(const QString &text) {
    for (QString line : text.split(QLatin1Char('\n'))) {
        line = line.trimmed();
        if (line.isEmpty()) continue;
        if (line.startsWith(QLatin1Char('-')) && line.contains(QLatin1Char('-')) && line.size() < 8)
            continue;
        return line;
    }
    return {};
}

bool looksLikeUsageOrHint(const QString &line) {
    const QString t = line.trimmed();
    if (t.startsWith(QLatin1Char('/'))) return true;
    if (t.startsWith(QStringLiteral("Apply:"), Qt::CaseInsensitive)) return true;
    if (t.startsWith(QStringLiteral("Review pending"), Qt::CaseInsensitive)) return true;
    if (t.startsWith(QStringLiteral("Usage:"), Qt::CaseInsensitive)) return true;
    return false;
}

CommandPickerItem item(const QString &value, const QString &name, const QString &desc = QString()) {
    return {value, name, desc, QString()};
}

} // namespace

QStringList CommandUi::seedCommands(const CommandPlan &plan) {
    const QString n = plan.name;
    if (n == QLatin1String("goal") || n == QLatin1String("subgoal"))
        return {QStringLiteral("/goal status"), QStringLiteral("/subgoal")};
    if (n == QLatin1String("heartbeat")) return {QStringLiteral("/heartbeat status")};
    if (n == QLatin1String("loop")) return {QStringLiteral("/loop status")};
    if (n == QLatin1String("queue")) return {QStringLiteral("/queue list")};
    if (n == QLatin1String("agents")) return {QStringLiteral("/agents")};
    if (n == QLatin1String("memory")) return {QStringLiteral("/memory pending")};
    if (n == QLatin1String("skills")) return {QStringLiteral("/skills")};
    if (n == QLatin1String("rollback")) return {QStringLiteral("/rollback")};
    if (n == QLatin1String("snapshot")) return {QStringLiteral("/snapshot list")};
    if (n == QLatin1String("debug")) return {QStringLiteral("/debug")};
    if (n == QLatin1String("insights")) return {QStringLiteral("/insights")};
    if (n == QLatin1String("diff")) return {QStringLiteral("/diff --stat")};
    if (n == QLatin1String("cron")) return {QStringLiteral("/cron")};
    if (n == QLatin1String("suggestions")) return {QStringLiteral("/suggestions")};
    if (n == QLatin1String("blueprint")) return {QStringLiteral("/blueprint")};
    if (n == QLatin1String("usage")) return {QStringLiteral("/usage")};
    if (n == QLatin1String("tools")) return {QStringLiteral("/tools")};
    if (n == QLatin1String("personality")) return {QStringLiteral("/personality")};
    if (n == QLatin1String("approvals")) return {QStringLiteral("/approvals")};
    if (plan.archetype == CommandArchetype::Info)
        return {QLatin1Char('/') + n};
    if (plan.archetype == CommandArchetype::LivePanel)
        return {QLatin1Char('/') + n};
    return {};
}

QString CommandUi::statusFromReport(const QString &text) {
    return firstMeaningfulLine(text);
}

QList<CommandPickerItem> CommandUi::itemsFromLiveReport(const QString &commandName, const QString &text) {
    QList<CommandPickerItem> items;
    QStringList seen;
    auto add = [&](QString value, QString name, QString desc) {
        value = value.trimmed();
        name = name.trimmed();
        if (name.isEmpty()) name = value;
        if (value.isEmpty()) value = name;
        if (value.isEmpty() || looksLikeUsageOrHint(name)) return;
        const QString key = value.toLower();
        if (seen.contains(key)) return;
        seen << key;
        items.push_back(item(value, name, desc.trimmed()));
    };

    const QString name = commandName.toLower();

    // Memory / skills pending: "  abc12  summary" or "  abc12 [auto]  summary"
    if (name == QLatin1String("memory") || name == QLatin1String("skills")) {
        const QRegularExpression pending(
            QStringLiteral("^\\s*([A-Za-z0-9_-]{3,})(?:\\s*\\[[^\\]]+\\])?\\s{2,}(.+)$"));
        for (const QString &raw : text.split(QLatin1Char('\n'))) {
            const auto m = pending.match(raw);
            if (m.hasMatch()) add(m.captured(1), m.captured(1), m.captured(2));
        }
        if (!items.isEmpty()) return items;
    }

    // Numbered rows: "1. text", "1) text", "  1. hash  rest", "1. [id] text"
    const QRegularExpression numbered(
        QStringLiteral("^\\s*(?:[-*•]\\s+|((?:\\d+)[\\.\\):]|\\[(?:\\d+|[A-Za-z0-9_-]+)\\])\\s+)"
                       "(?:\\[([^\\]]+)\\]\\s*)?(.+?)\\s*$"));
    for (QString raw : text.split(QLatin1Char('\n'))) {
        if (looksLikeUsageOrHint(raw)) continue;
        const auto m = numbered.match(raw);
        if (!m.hasMatch()) continue;
        const QString marker = m.captured(1);
        QString id = m.captured(2);
        QString rest = m.captured(3);
        // "1. short_hash  timestamp  reason"
        if (id.isEmpty() && (name == QLatin1String("rollback") || name == QLatin1String("snapshot")
                             || name == QLatin1String("cron") || name == QLatin1String("blueprint")
                             || name == QLatin1String("agents"))) {
            const QStringList bits = rest.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
            if (!bits.isEmpty() && bits[0].size() >= 2)
                id = bits[0];
        }
        QString value = id;
        if (value.isEmpty()) {
            // Use the index from the marker when present.
            QString mark = marker;
            mark.remove(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")));
            value = mark.isEmpty() ? rest : mark;
        }
        add(value, rest, QString());
    }

    if (!items.isEmpty()) return items;

    // Queue / generic "N. prompt" already handled; try "prompt" lines after a "queued" header.
    bool inList = false;
    for (QString raw : text.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.contains(QLatin1String("queued"), Qt::CaseInsensitive)
            || line.contains(QLatin1String("pending"), Qt::CaseInsensitive)
            || line.endsWith(QLatin1Char(':'))) {
            inList = true;
            continue;
        }
        if (inList && !line.isEmpty() && !looksLikeUsageOrHint(line))
            add(QString::number(items.size() + 1), line, QString());
    }
    return items;
}
