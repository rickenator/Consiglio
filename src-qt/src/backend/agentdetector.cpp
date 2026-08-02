#include "agentdetector.h"
#include <QProcess>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkInterface>
#include <QNetworkReply>
#include <QSet>
#include <QTcpSocket>
#include <QTimer>
#include <functional>

AgentDetector::AgentDetector(QObject *parent) : QObject(parent) {}

QString AgentDetector::findExecutable(const QString &name) const {
    // Check PATH first
    auto pathEnv = qgetenv("PATH");
    for (const auto &dir : pathEnv.split(':')) {
        QFileInfo fi(dir + "/" + name);
        if (fi.isExecutable()) return fi.absoluteFilePath();
    }

    // Check common installation locations
    auto candidates = QStringList()
        << "/usr/local/bin/" + name
        << "/usr/bin/" + name
        << QDir::homePath() + "/.local/bin/" + name;

#ifdef Q_OS_MAC
    candidates << "/Applications/Codex.app/Contents/MacOS/codex"
               << "/opt/homebrew/bin/" + name;
#endif

#ifdef Q_OS_WIN
    candidates << QDir::toNativeSeparators(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + name);
#endif

    for (const auto &c : candidates) {
        QFileInfo fi(c);
        if (fi.isExecutable()) return fi.absoluteFilePath();
    }
    return {};
}

QString AgentDetector::getVersion(const QString &path) const {
    if (path.isEmpty()) return {};
    QProcess proc;
    proc.start(path, {"--version"});
    if (proc.waitForFinished(3000)) {
        auto output = proc.readAllStandardOutput().trimmed();
        if (!output.isEmpty()) return output.split('\n').first().left(50);
    }
    return {};
}

void AgentDetector::detectCodex() {
    AgentInfo info;
    info.id = "codex";
    info.name = "Codex CLI";
    info.installed = false;

    auto path = findExecutable("codex");
    if (path.isEmpty()) {
        // Check macOS GUI installation
#ifdef Q_OS_MAC
        auto candidates = QStringList()
            << "/Applications/Codex.app/Contents/MacOS/codex"
            << QDir::homePath() + "/Library/Application Support/Codex/codex";
        for (const auto &c : candidates) {
            if (QFileInfo(c).exists()) { path = c; break; }
        }
#endif
    }

    if (!path.isEmpty()) {
        info.installed = true;
        info.version = getVersion(path);
        // Check auth status
        QProcess authCheck;
        authCheck.start(path, {"auth", "status"});
        if (authCheck.waitForFinished(5000)) {
            auto output = authCheck.readAllStandardOutput();
            info.authenticated = !output.contains("not authenticated") && !output.contains("unauthenticated");
        }
    }

    info.diagnostic = info.installed
        ? (info.authenticated ? "Installed and signed in" : "Installed; ready for local providers")
        : "Not installed";

    m_agents.append(info);
}

void AgentDetector::detectOllama() {
    AgentInfo info;
    info.id = "ollama";
    info.name = "Ollama";
    info.installed = false;

    auto path = findExecutable("ollama");
    if (!path.isEmpty()) {
        info.installed = true;
        info.version = getVersion(path);

        // Check if Ollama server is running and has models
        QProcess check;
        check.start("curl", {"-s", "http://localhost:11434/api/tags"});
        if (check.waitForFinished(3000)) {
            auto output = check.readAllStandardOutput();
            info.authenticated = !output.isEmpty() && output.contains("\"models\"");
            if (info.authenticated) {
                // Extract model count
                QRegularExpression re("\"models\":\\s*(\\d+)");
                auto match = re.match(output);
                if (match.hasMatch()) {
                    info.diagnostic = QString("%1 local model(s) detected").arg(match.captured(1));
                } else {
                    info.diagnostic = "Ollama running with models";
                }
            }
        }

        if (!info.authenticated) {
            info.diagnostic = info.installed ? "Ollama not running or no models" : "Not installed";
        }
    } else {
        info.diagnostic = "Not installed";
    }

    m_agents.append(info);
}

void AgentDetector::detectLlamaCpp() {
    AgentInfo info;
    info.id = "llama-cpp";
    info.name = "llama.cpp";
    info.installed = false;

    // Check for server binary
    auto path = findExecutable("server");
    if (path.isEmpty()) {
        path = findExecutable("llama-server");
    }

    if (!path.isEmpty()) {
        info.installed = true;
        info.version = getVersion(path);

        // Check if server is running
        QProcess check;
        check.start("curl", {"-s", "http://localhost:8081/v1/models"});
        if (check.waitForFinished(3000)) {
            auto output = check.readAllStandardOutput();
            info.authenticated = !output.isEmpty() && output.contains("\"data\"");
        }

        info.diagnostic = info.authenticated ? "Server running on localhost:8081" : "Binary found; server not running";
    } else {
        info.diagnostic = "Not installed (no server binary found)";
    }

    m_agents.append(info);
}

void AgentDetector::detectOpenInterpreter() {
    AgentInfo info;
    info.id = "open-interpreter";
    info.name = "Open Interpreter";
    info.installed = false;

    auto path = findExecutable("interpreter");
    if (path.isEmpty()) {
        // Check in user data directory (Consiglio-managed install)
        auto userData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        path = userData + "/open-interpreter/bin/interpreter";
        if (!QFileInfo(path).exists()) {
            path.clear();
        }
    }

    if (!path.isEmpty()) {
        info.installed = true;
        info.version = getVersion(path);
        info.diagnostic = "Installed in user data directory";
    } else {
        // Check if Python is available for installation
        auto python = findExecutable("python3");
        if (python.isEmpty()) {
            python = findExecutable("python");
        }
        info.diagnostic = python.isEmpty() ? "Python not found; cannot install" : "Not installed (Python available)";
    }

    m_agents.append(info);
}

void AgentDetector::detectNetworkEndpoints() {
    struct Target { QString host; quint16 port; };
    QList<Target> targets;
    QSet<QString> seenTargets;
    const QList<quint16> ports = {11434, 8081, 1234, 8000};

    for (const auto &interface : QNetworkInterface::allInterfaces()) {
        if (!(interface.flags() & QNetworkInterface::IsUp)
            || !(interface.flags() & QNetworkInterface::IsRunning)
            || (interface.flags() & QNetworkInterface::IsLoopBack)) {
            continue;
        }
        for (const auto &entry : interface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol) continue;
            const quint32 ownAddress = entry.ip().toIPv4Address();
            const quint32 subnet = ownAddress & 0xffffff00u;
            const int ownHost = static_cast<int>(ownAddress & 0xffu);

            // Probe nearby addresses first, then wrap around the /24. This
            // makes useful endpoints appear quickly while retaining complete
            // subnet coverage under the bounded concurrency limit.
            for (int distance = 0; distance < 254; ++distance) {
                const int hostPart = ((ownHost - 1 + distance) % 254) + 1;
                const QString host = QHostAddress(subnet | static_cast<quint32>(hostPart)).toString();
                for (quint16 port : ports) {
                    const QString key = QString("%1:%2").arg(host).arg(port);
                    if (seenTargets.contains(key)) continue;
                    seenTargets.insert(key);
                    targets.append({host, port});
                }
            }
        }
    }

    if (targets.isEmpty()) return;

    QList<Target> openTargets;
    QEventLoop socketLoop;
    int nextTarget = 0;
    int active = 0;
    constexpr int concurrency = 256;
    std::function<void()> launchMore;

    auto complete = [&](QTcpSocket *socket, const Target &target, bool open) {
        if (socket->property("consiglioDone").toBool()) return;
        socket->setProperty("consiglioDone", true);
        if (open) openTargets.append(target);
        socket->abort();
        socket->deleteLater();
        --active;
        if (nextTarget >= targets.size() && active == 0) socketLoop.quit();
        else QTimer::singleShot(0, &socketLoop, launchMore);
    };

    launchMore = [&]() {
        while (active < concurrency && nextTarget < targets.size()) {
            const Target target = targets.at(nextTarget++);
            auto *socket = new QTcpSocket(&socketLoop);
            ++active;
            connect(socket, &QTcpSocket::connected, &socketLoop,
                    [&, socket, target]() { complete(socket, target, true); });
            connect(socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
                    &socketLoop, [&, socket, target](QAbstractSocket::SocketError) {
                        complete(socket, target, false);
                    });
            QTimer::singleShot(750, socket, [&, socket, target]() {
                complete(socket, target, false);
            });
            socket->connectToHost(target.host, target.port);
        }
    };

    launchMore();
    socketLoop.exec();
    if (openTargets.isEmpty()) return;

    QNetworkAccessManager manager;
    QEventLoop httpLoop;
    QList<QNetworkReply *> replies;
    int pending = openTargets.size();
    for (const auto &target : openTargets) {
        const bool ollama = target.port == 11434;
        const QString baseUrl = QString("http://%1:%2").arg(target.host).arg(target.port);
        QNetworkRequest request(QUrl(baseUrl + (ollama ? "/api/tags" : "/v1/models")));
        auto *reply = manager.get(request);
        replies.append(reply);
        connect(reply, &QNetworkReply::finished, &httpLoop, [&, reply, target, ollama, baseUrl]() {
            if (reply->error() == QNetworkReply::NoError) {
                const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());
                QString model;
                if (document.isObject()) {
                    const QJsonArray models = document.object().value(ollama ? "models" : "data").toArray();
                    if (!models.isEmpty()) {
                        const QJsonObject first = models.first().toObject();
                        model = first.value(ollama ? "name" : "id").toString();
                    }
                }
                if (!model.isEmpty()) {
                    AgentInfo info;
                    info.id = QString("lan:%1:%2").arg(target.host).arg(target.port);
                    info.name = ollama
                        ? QString("Ollama on %1").arg(target.host)
                        : QString("OpenAI-compatible endpoint on %1").arg(target.host);
                    info.installed = true;
                    info.authenticated = true;
                    info.provider = ollama ? "ollama" : "remote_llamacpp";
                    info.endpoint = baseUrl;
                    info.model = model;
                    info.diagnostic = QString("%1 · model %2").arg(baseUrl, model);
                    m_agents.append(info);
                }
            }
            reply->deleteLater();
            if (--pending == 0) httpLoop.quit();
        });
    }

    QTimer::singleShot(2500, &httpLoop, [&]() {
        for (auto *reply : replies) {
            if (reply && !reply->isFinished()) reply->abort();
        }
    });
    httpLoop.exec();
}

void AgentDetector::startDetection() {
    m_agents.clear();
    detectCodex();
    detectOllama();
    detectLlamaCpp();
    detectOpenInterpreter();
    detectNetworkEndpoints();
    emit detectionCompleted(m_agents);
}

QList<AgentInfo> AgentDetector::detectAll() const { return m_agents; }

QList<AgentInfo> AgentDetector::detectAvailable() const {
    QList<AgentInfo> result;
    for (const auto &a : m_agents) {
        if (a.installed) result.append(a);
    }
    return result;
}

QString AgentDetector::codexPath() const {
    for (const auto &a : m_agents) {
        if (a.id == "codex") {
            // Would need to store path in AgentInfo; for now return empty
            return {};
        }
    }
    return {};
}

QString AgentDetector::ollamaPath() const {
    for (const auto &a : m_agents) {
        if (a.id == "ollama") {
            return {};
        }
    }
    return {};
}

QString AgentDetector::openInterpreterPath() const {
    for (const auto &a : m_agents) {
        if (a.id == "open-interpreter") {
            return {};
        }
    }
    return {};
}
