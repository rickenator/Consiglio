#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QList>

struct LanProviderConfig {
    QString id;
    QString name;
    QString host;
    quint16 port = 8081;
    QString model;
    QString apiKey;
};

struct AppSettings {
    QString defaultProvider = "default"; // default, ollama, remote_llamacpp, lan
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

    bool hasRunSetup() const;
    void load();
    void save();
};

class Settings : public QObject {
    Q_OBJECT

public:
    explicit Settings(QObject *parent = nullptr);

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
};
