#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QList>

class AppDatabase;

struct LanProviderConfig {
    QString id;
    QString name;
    QString host;
    quint16 port = 8081;
    QString model;
    QString apiKey;
};

struct AppSettings {
    QString userName = "Dude";
    QString defaultProvider = "codex"; // codex, ollama, remote_llamacpp, lan
    bool providerConfigured = false;
    struct {
        QString baseUrl = "http://localhost:11434";
        QString model = "qwen2.5:32b-instruct-q4_K_M";
        QString apiKey;
    } ollama;
    struct {
        QString baseUrl;
        QString model;
        QString apiKey = "llama.cpp";
    } remoteLlamaCpp;
    QList<LanProviderConfig> lanProviders;
    QString defaultModel;
    struct {
        bool isolateProfile = true;
        bool enableWebSearch = true;
        bool enableMultiAgent = false;
    } localProviderBehavior;

    AppDatabase *database = nullptr;

    bool hasRunSetup() const;
    void load();
    void save();
};

class Settings : public QObject {
    Q_OBJECT

public:
    explicit Settings(QObject *parent = nullptr, AppDatabase *database = nullptr);
    ~Settings() override;

    const AppSettings &value() const;
    AppSettings &value();
    void load();
    void save();
    void setValue(const AppSettings &value);

    bool hasRunSetup() const;

signals:
    void changed();

private:
    AppSettings m_value;
    AppDatabase *m_database = nullptr;
    bool m_ownsDatabase = false;
};
