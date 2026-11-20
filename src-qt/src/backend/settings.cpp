#include "settings.h"
#include "appdatabase.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

bool AppSettings::hasRunSetup() const {
    return providerConfigured;
}

void AppSettings::load() {
    if (!database || !database->isOpen()) return;
    userName = database->preference("userName", userName).toString().trimmed();
    if (userName.isEmpty()) userName = "Dude";
    if (!database->containsPreference("userName"))
        database->setPreference("userName", userName);
    defaultProvider = database->preference("defaultProvider", defaultProvider).toString();
    if (defaultProvider.isEmpty() || defaultProvider == "default")
        defaultProvider = "codex";
    providerConfigured = database->preference("providerConfigured", providerConfigured).toBool();
    ollama.baseUrl = database->preference("ollama/baseUrl", ollama.baseUrl).toString();
    ollama.model = database->preference("ollama/model", ollama.model).toString();
    ollama.apiKey = database->preference("ollama/apiKey", ollama.apiKey).toString();
    remoteLlamaCpp.baseUrl = database->preference(
        "remoteLlamaCpp/baseUrl", remoteLlamaCpp.baseUrl).toString();
    remoteLlamaCpp.model = database->preference(
        "remoteLlamaCpp/model", remoteLlamaCpp.model).toString();
    remoteLlamaCpp.apiKey = database->preference(
        "remoteLlamaCpp/apiKey", remoteLlamaCpp.apiKey).toString();
    defaultModel = database->preference("defaultModel", defaultModel).toString();
    localProviderBehavior.isolateProfile = database->preference(
        "localProviderBehavior/isolateProfile",
        localProviderBehavior.isolateProfile).toBool();
    localProviderBehavior.enableWebSearch = database->preference(
        "localProviderBehavior/enableWebSearch",
        localProviderBehavior.enableWebSearch).toBool();
    localProviderBehavior.enableMultiAgent = database->preference(
        "localProviderBehavior/enableMultiAgent",
        localProviderBehavior.enableMultiAgent).toBool();

    // Load LAN providers from JSON array
    lanProviders.clear();
    const QByteArray json = database->preference("lanProviders").toByteArray();
    if (!json.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(json);
        if (doc.isArray()) {
            for (const auto &v : doc.array()) {
                auto map = v.toObject().toVariantMap();
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
}

void AppSettings::save() {
    if (!database || !database->isOpen()) return;
    userName = userName.trimmed();
    if (userName.isEmpty()) userName = "Dude";
    database->setPreference("userName", userName);
    database->setPreference("defaultProvider", defaultProvider);
    database->setPreference("providerConfigured", providerConfigured);
    database->setPreference("ollama/baseUrl", ollama.baseUrl);
    database->setPreference("ollama/model", ollama.model);
    database->setPreference("ollama/apiKey", ollama.apiKey);
    database->setPreference("remoteLlamaCpp/baseUrl", remoteLlamaCpp.baseUrl);
    database->setPreference("remoteLlamaCpp/model", remoteLlamaCpp.model);
    database->setPreference("remoteLlamaCpp/apiKey", remoteLlamaCpp.apiKey);
    database->setPreference("defaultModel", defaultModel);
    database->setPreference("localProviderBehavior/isolateProfile", localProviderBehavior.isolateProfile);
    database->setPreference("localProviderBehavior/enableWebSearch", localProviderBehavior.enableWebSearch);
    database->setPreference("localProviderBehavior/enableMultiAgent", localProviderBehavior.enableMultiAgent);

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
    database->setPreference("lanProviders", QJsonDocument(arr).toJson());
}

Settings::Settings(QObject *parent, AppDatabase *database)
    : QObject(parent), m_database(database) {
    if (!m_database) {
        m_database = new AppDatabase;
        m_ownsDatabase = true;
    }
    m_value.database = m_database;
    load();
}

Settings::~Settings() {
    if (m_ownsDatabase) delete m_database;
}

const AppSettings &Settings::value() const { return m_value; }

void Settings::setValue(const AppSettings &value) {
    m_value = value;
    m_value.database = m_database;
    emit changed();
}

bool Settings::hasRunSetup() const { return m_value.hasRunSetup(); }

AppSettings &Settings::value() { return m_value; }
void Settings::load() { m_value.load(); }
void Settings::save() { m_value.save(); }
