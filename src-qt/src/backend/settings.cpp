#include "settings.h"
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

bool AppSettings::hasRunSetup() const {
    return !defaultProvider.isEmpty();
}

void AppSettings::load() {
    QSettings settings("Aniviza", "Consiglio");
    if (settings.contains("defaultProvider"))
        defaultProvider = settings.value("defaultProvider").toString();
    if (settings.contains("ollama/baseUrl"))
        ollama.baseUrl = settings.value("ollama/baseUrl").toString();
    if (settings.contains("ollama/model"))
        ollama.model = settings.value("ollama/model").toString();
    if (settings.contains("ollama/apiKey"))
        ollama.apiKey = settings.value("ollama/apiKey").toString();
    if (settings.contains("remoteLlamaCpp/baseUrl"))
        remoteLlamaCpp.baseUrl = settings.value("remoteLlamaCpp/baseUrl").toString();
    if (settings.contains("remoteLlamaCpp/model"))
        remoteLlamaCpp.model = settings.value("remoteLlamaCpp/model").toString();
    if (settings.contains("remoteLlamaCpp/apiKey"))
        remoteLlamaCpp.apiKey = settings.value("remoteLlamaCpp/apiKey").toString();
    if (settings.contains("defaultModel"))
        defaultModel = settings.value("defaultModel").toString();
    if (settings.contains("localProviderBehavior/isolateProfile"))
        localProviderBehavior.isolateProfile = settings.value("localProviderBehavior/isolateProfile").toBool();
    if (settings.contains("localProviderBehavior/enableWebSearch"))
        localProviderBehavior.enableWebSearch = settings.value("localProviderBehavior/enableWebSearch").toBool();
    if (settings.contains("localProviderBehavior/enableMultiAgent"))
        localProviderBehavior.enableMultiAgent = settings.value("localProviderBehavior/enableMultiAgent").toBool();

    // Load LAN providers from JSON array
    if (settings.contains("lanProviders")) {
        auto arr = settings.value("lanProviders").toList();
        for (const auto &v : arr) {
            auto map = v.toMap();
            LanProviderConfig cfg;
            cfg.id = map.value("id", "").toString();
            cfg.name = map.value("name", "").toString();
            cfg.host = map.value("host", "").toString();
            cfg.port = map.value("port", 8081).toUInt();
            cfg.model = map.value("model", "").toString();
            cfg.apiKey = map.value("apiKey", "").toString();
            lanProviders.append(cfg);
        }
    }
}

void AppSettings::save() {
    QSettings settings("Aniviza", "Consiglio");
    settings.setValue("defaultProvider", defaultProvider);
    settings.setValue("ollama/baseUrl", ollama.baseUrl);
    settings.setValue("ollama/model", ollama.model);
    settings.setValue("ollama/apiKey", ollama.apiKey);
    settings.setValue("remoteLlamaCpp/baseUrl", remoteLlamaCpp.baseUrl);
    settings.setValue("remoteLlamaCpp/model", remoteLlamaCpp.model);
    settings.setValue("remoteLlamaCpp/apiKey", remoteLlamaCpp.apiKey);
    settings.setValue("defaultModel", defaultModel);
    settings.setValue("localProviderBehavior/isolateProfile", localProviderBehavior.isolateProfile);
    settings.setValue("localProviderBehavior/enableWebSearch", localProviderBehavior.enableWebSearch);
    settings.setValue("localProviderBehavior/enableMultiAgent", localProviderBehavior.enableMultiAgent);

    // Save LAN providers as JSON array
    QJsonArray arr;
    for (const auto &cfg : lanProviders) {
        QJsonObject obj;
        obj["id"] = cfg.id;
        obj["name"] = cfg.name;
        obj["host"] = cfg.host;
        obj["port"] = cfg.port;
        obj["model"] = cfg.model;
        obj["apiKey"] = cfg.apiKey;
        arr.append(obj);
    }
    settings.setValue("lanProviders", QJsonDocument(arr).toJson());
}

Settings::Settings(QObject *parent) : QObject(parent) {
    load();
}

const AppSettings &Settings::value() const { return m_value; }

void Settings::setValue(const AppSettings &value) {
    m_value = value;
    emit changed();
}

bool Settings::hasRunSetup() const { return m_value.hasRunSetup(); }

AppSettings &Settings::value() { return m_value; }
void Settings::load() { m_value.load(); }
void Settings::save() { m_value.save(); }
