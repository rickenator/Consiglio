#include <QtTest/QtTest>
#include "backend/settings.h"
#include <QSettings>

class TestSettings : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void testDefaultValues();
    void testSaveAndLoad();
    void testOllamaConfig();
    void testLanProviders();
    void testLocalProviderBehavior();
    void testSetValueSignal();

private:
    Settings *m_settings = nullptr;
};

void TestSettings::init() {
    // Clear the real settings store before each test to ensure isolation
    QSettings s("Aniviza", "Consiglio");
    s.clear();
    m_settings = new Settings(nullptr);
}

void TestSettings::cleanup() {
    delete m_settings;
    m_settings = nullptr;
    // Clear after each test too
    QSettings s("Aniviza", "Consiglio");
    s.clear();
}

// Default values should be sensible defaults
void TestSettings::testDefaultValues() {
    QCOMPARE(m_settings->value().defaultProvider, QString("default"));
    QCOMPARE(m_settings->value().ollama.baseUrl, QString("http://localhost:11434"));
    QCOMPARE(m_settings->value().ollama.model, QString("qwen2.5:32b-instruct-q4_K_M"));
    QCOMPARE(m_settings->value().lanProviders.size(), 0);
}

// Save and load roundtrip — uses a separate Settings instance to verify persistence
void TestSettings::testSaveAndLoad() {
    AppSettings &s = m_settings->value();
    s.defaultProvider = "ollama";
    s.ollama.baseUrl = "http://localhost:11435";
    s.ollama.model = "llama3:8b";
    s.save();

    // Create a fresh Settings instance — it loads from the same QSettings store
    Settings freshSettings(nullptr);
    QCOMPARE(freshSettings.value().defaultProvider, QString("ollama"));
    QCOMPARE(freshSettings.value().ollama.baseUrl, QString("http://localhost:11435"));
    QCOMPARE(freshSettings.value().ollama.model, QString("llama3:8b"));
}

// Ollama configuration roundtrip
void TestSettings::testOllamaConfig() {
    AppSettings &s = m_settings->value();
    s.ollama.baseUrl = "http://192.168.1.100:11434";
    s.ollama.model = "mistral:7b";
    s.ollama.apiKey = "test-key-123";
    s.save();

    Settings freshSettings(nullptr);
    QCOMPARE(freshSettings.value().ollama.baseUrl, QString("http://192.168.1.100:11434"));
    QCOMPARE(freshSettings.value().ollama.model, QString("mistral:7b"));
    QCOMPARE(freshSettings.value().ollama.apiKey, QString("test-key-123"));
}

// LAN providers roundtrip
void TestSettings::testLanProviders() {
    AppSettings &s = m_settings->value();
    LanProviderConfig cfg;
    cfg.id = "lan-1";
    cfg.name = "Remote Ollama";
    cfg.host = "192.168.1.50";
    cfg.port = 11434;
    cfg.model = "llama3:70b";
    cfg.apiKey = "";
    s.lanProviders.append(cfg);

    LanProviderConfig cfg2;
    cfg2.id = "lan-2";
    cfg2.name = "Dev Server";
    cfg2.host = "10.0.0.5";
    cfg2.port = 8081;
    cfg2.model = "codellama:34b";
    cfg2.apiKey = "dev-key";
    s.lanProviders.append(cfg2);

    s.save();

    Settings freshSettings(nullptr);
    QCOMPARE(freshSettings.value().lanProviders.size(), 2);
    QCOMPARE(freshSettings.value().lanProviders[0].id, QString("lan-1"));
    QCOMPARE(freshSettings.value().lanProviders[0].name, QString("Remote Ollama"));
    QCOMPARE(freshSettings.value().lanProviders[0].host, QString("192.168.1.50"));
    QCOMPARE(freshSettings.value().lanProviders[0].port, 11434u);
    QCOMPARE(freshSettings.value().lanProviders[1].id, QString("lan-2"));
}

// Local provider behavior flags roundtrip
void TestSettings::testLocalProviderBehavior() {
    AppSettings &s = m_settings->value();
    s.localProviderBehavior.isolateProfile = false;
    s.localProviderBehavior.enableWebSearch = false;
    s.localProviderBehavior.enableMultiAgent = true;
    s.save();

    Settings freshSettings(nullptr);
    QCOMPARE(freshSettings.value().localProviderBehavior.isolateProfile, false);
    QCOMPARE(freshSettings.value().localProviderBehavior.enableWebSearch, false);
    QCOMPARE(freshSettings.value().localProviderBehavior.enableMultiAgent, true);
}

// setValue should emit changed signal
void TestSettings::testSetValueSignal() {
    QSignalSpy spy(m_settings, &Settings::changed);
    QCOMPARE(spy.count(), 0);

    AppSettings newSettings;
    newSettings.defaultProvider = "ollama";
    m_settings->setValue(newSettings);

    QCOMPARE(spy.count(), 1);
}

QTEST_MAIN(TestSettings)
#include "test_settings.moc"
