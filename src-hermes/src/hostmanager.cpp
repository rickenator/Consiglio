#include "hostmanager.h"

#include "hermesremote.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QUuid>

namespace {
const char *kProfiles = "hosts/profiles";
const char *kActive = "hosts/active";
const char *kMigrated = "hosts/migratedFromSingle";

QJsonObject toJson(const HostProfile &p) {
    QJsonObject o;
    o[QStringLiteral("id")] = p.id;
    o[QStringLiteral("label")] = p.label;
    o[QStringLiteral("destination")] = p.destination;
    o[QStringLiteral("options")] = p.options;
    o[QStringLiteral("launcher")] = p.launcher;
    return o;
}

HostProfile fromJson(const QJsonObject &o) {
    HostProfile p;
    p.id = o.value(QStringLiteral("id")).toString();
    p.label = o.value(QStringLiteral("label")).toString();
    p.destination = o.value(QStringLiteral("destination")).toString();
    p.options = o.value(QStringLiteral("options")).toString();
    p.launcher = o.value(QStringLiteral("launcher")).toString();
    return p;
}
}  // namespace

QString HostProfile::displayName() const {
    if (isLocal()) return QStringLiteral("This machine");
    if (!label.trimmed().isEmpty()) return label.trimmed();
    return destination;
}

bool HostProfile::isValid() const {
    if (isLocal()) return true;
    return !destination.trimmed().isEmpty();
}

HostProfile HostManager::localProfile() {
    HostProfile p;
    p.id = QLatin1String(localId());
    p.label = QStringLiteral("This machine");
    return p;
}

QList<HostProfile> HostManager::profiles() {
    QList<HostProfile> out;
    out << localProfile();
    const QSettings settings;
    const QJsonDocument doc = QJsonDocument::fromJson(
        settings.value(QLatin1String(kProfiles)).toByteArray());
    if (!doc.isArray()) return out;
    for (const auto &value : doc.array()) {
        const HostProfile p = fromJson(value.toObject());
        if (p.id.isEmpty() || p.isLocal()) continue;   // local is synthesised
        out << p;
    }
    return out;
}

HostProfile HostManager::profile(const QString &id) {
    if (id.isEmpty() || id == QLatin1String(localId())) return localProfile();
    for (const HostProfile &p : profiles()) {
        if (p.id == id) return p;
    }
    return HostProfile();
}

HostProfile HostManager::active() {
    return profile(activeId());
}

QString HostManager::activeId() {
    const QString id = QSettings().value(QLatin1String(kActive)).toString();
    if (id.isEmpty()) return QLatin1String(localId());
    // A host that was removed (or a stale id) falls back to local rather than
    // leaving the window pointing at nothing.
    return profile(id).isValid() ? id : QLatin1String(localId());
}

void HostManager::setActiveId(const QString &id) {
    QSettings settings;
    if (id.isEmpty() || id == QLatin1String(localId())) settings.remove(QLatin1String(kActive));
    else settings.setValue(QLatin1String(kActive), id);
}

QString HostManager::addProfile(const QString &destination, const QString &label) {
    HostProfile p;
    p.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    p.destination = destination.trimmed();
    p.label = label.trimmed();
    if (p.label.isEmpty()) p.label = p.destination;
    QJsonArray arr = QJsonDocument::fromJson(
                         QSettings().value(QLatin1String(kProfiles)).toByteArray())
                         .array();
    arr.append(toJson(p));
    QSettings().setValue(QLatin1String(kProfiles),
                         QJsonDocument(arr).toJson(QJsonDocument::Compact));
    return p.id;
}

void HostManager::updateProfile(const HostProfile &profile) {
    if (profile.isLocal() || profile.id.isEmpty()) return;   // local is not stored
    QJsonArray arr = QJsonDocument::fromJson(
                         QSettings().value(QLatin1String(kProfiles)).toByteArray())
                         .array();
    bool found = false;
    for (int i = 0; i < arr.size(); ++i) {
        if (arr.at(i).toObject().value(QStringLiteral("id")).toString() == profile.id) {
            arr.replace(i, toJson(profile));
            found = true;
            break;
        }
    }
    if (!found) arr.append(toJson(profile));
    QSettings().setValue(QLatin1String(kProfiles),
                         QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

void HostManager::removeProfile(const QString &id) {
    if (id.isEmpty() || id == QLatin1String(localId())) return;
    QJsonArray arr = QJsonDocument::fromJson(
                         QSettings().value(QLatin1String(kProfiles)).toByteArray())
                         .array();
    QJsonArray kept;
    for (const auto &value : arr) {
        if (value.toObject().value(QStringLiteral("id")).toString() != id) kept.append(value);
    }
    QSettings().setValue(QLatin1String(kProfiles),
                         QJsonDocument(kept).toJson(QJsonDocument::Compact));
    if (activeId() == id) setActiveId(QLatin1String(localId()));
}

void HostManager::migrateLegacy() {
    QSettings settings;
    if (settings.value(QLatin1String(kMigrated), false).toBool()) return;
    settings.setValue(QLatin1String(kMigrated), true);
    // The single-connection version stored one enabled remote under remote/*.
    if (!HermesRemote::enabled()) return;
    const QString destination = HermesRemote::destination();
    if (destination.isEmpty()) return;
    const QString id = addProfile(destination);
    HostProfile p = profile(id);
    p.options = HermesRemote::optionsLine();
    p.launcher = HermesRemote::launcher() == QLatin1String("hermes") ? QString()
                                                                     : HermesRemote::launcher();
    updateProfile(p);
    setActiveId(id);
}

void HostManager::clearAll() {
    QSettings settings;
    settings.remove(QLatin1String(kProfiles));
    settings.remove(QLatin1String(kActive));
    settings.remove(QLatin1String(kMigrated));
}
