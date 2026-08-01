#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QPair>

struct AgentInfo {
    QString id;       // codex, ollama, llama-cpp, open-interpreter
    QString name;
    bool installed = false;
    bool authenticated = false;
    QString version;
    QString diagnostic; // human-readable status message
};

class AgentDetector : public QObject {
    Q_OBJECT

public:
    explicit AgentDetector(QObject *parent = nullptr);

    QList<AgentInfo> detectAll() const;
    QList<AgentInfo> detectAvailable() const;
    QString codexPath() const;
    QString ollamaPath() const;
    QString openInterpreterPath() const;

signals:
    void detectionCompleted(const QList<AgentInfo> &agents);

public slots:
    void startDetection();

private:
    QList<AgentInfo> m_agents;
    void detectCodex();
    void detectOllama();
    void detectLlamaCpp();
    void detectOpenInterpreter();
    QString findExecutable(const QString &name) const;
    QString getVersion(const QString &path) const;
};
