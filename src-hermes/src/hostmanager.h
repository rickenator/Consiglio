#pragma once

#include <QList>
#include <QString>

// Where Hermes can run: this machine, or one of a list of remote hosts.
//
// One window talks to one host at a time; switching hosts is a reconnect, not a
// second process. That keeps sessions, approvals and restarts unambiguous (a
// session always belongs to the host named in the status bar). Concurrent
// hosts in one window is a different feature and deliberately not here.
struct HostProfile {
    QString id;             // "local" or a generated id
    QString label;          // what the UI shows; defaults to the destination
    QString destination;    // ssh destination (user@host or a Host alias)
    QString options;        // extra ssh options, one line
    QString launcher;       // remote launcher name; empty = "hermes"

    bool isLocal() const { return id == QLatin1String("local"); }
    QString displayName() const;
    bool isValid() const;   // local is always valid; remote needs a destination
};

class HostManager {
public:
    static const char *localId() { return "local"; }
    static HostProfile localProfile();

    // Always includes the local host first, then saved remote hosts in the
    // order they were added.
    static QList<HostProfile> profiles();
    static HostProfile profile(const QString &id);   // empty + invalid if unknown
    static HostProfile active();                     // local when unset/unknown
    static QString activeId();
    static void setActiveId(const QString &id);

    static QString addProfile(const QString &destination, const QString &label = QString());
    static void updateProfile(const HostProfile &profile);
    static void removeProfile(const QString &id);

    // Settings written by the single-connection version (#80): one enabled
    // remote becomes one profile, and is made active. Idempotent.
    static void migrateLegacy();
    static void clearAll();
};
