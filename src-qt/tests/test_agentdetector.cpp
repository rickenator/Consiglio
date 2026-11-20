#include <QtTest/QtTest>
#include "backend/agentdetector.h"

class TestAgentDetector : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void testDetectAll_returnsAgents();
    void testDetectAvailable_filtersInstalled();
    void testCodexPath_returnsValidOrEmpty();
    void testOllamaPath_returnsValidOrEmpty();
    void testOpenInterpreterPath_returnsValidOrEmpty();

private:
    AgentDetector *m_detector = nullptr;
};

void TestAgentDetector::init() {
    m_detector = new AgentDetector(nullptr);
}

void TestAgentDetector::cleanup() {
    delete m_detector;
    m_detector = nullptr;
}

// detectAll should return a list of agent definitions (after detection runs)
void TestAgentDetector::testDetectAll_returnsAgents() {
    m_detector->startDetection();
    auto agents = m_detector->detectAll();

    QVERIFY(agents.size() > 0);

    // Should have at least codex and ollama entries
    bool hasCodex = false, hasOllama = false, hasLlamaCpp = false;
    for (const auto &agent : agents) {
        if (agent.id == "codex") hasCodex = true;
        if (agent.id == "ollama") hasOllama = true;
        if (agent.id == "llama-cpp") hasLlamaCpp = true;
    }

    QVERIFY(hasCodex);
    QVERIFY(hasOllama);
    QVERIFY(hasLlamaCpp);
}

// detectAvailable should only return installed agents
void TestAgentDetector::testDetectAvailable_filtersInstalled() {
    m_detector->startDetection();
    auto available = m_detector->detectAvailable();

    // All returned agents should be marked as installed
    for (const auto &agent : available) {
        QVERIFY(agent.installed);
    }

    // Available count should be <= total count
    auto all = m_detector->detectAll();
    QVERIFY(available.size() <= all.size());
}

// codexPath should return a valid path or empty string
void TestAgentDetector::testCodexPath_returnsValidOrEmpty() {
    QString path = m_detector->codexPath();

    if (!path.isEmpty()) {
        // If non-empty, it should point to an existing executable
        QVERIFY(QFile::exists(path));
    }
}

// ollamaPath should return a valid path or empty string
void TestAgentDetector::testOllamaPath_returnsValidOrEmpty() {
    QString path = m_detector->ollamaPath();

    if (!path.isEmpty()) {
        QVERIFY(QFile::exists(path));
    }
}

// openInterpreterPath should return a valid path or empty string
void TestAgentDetector::testOpenInterpreterPath_returnsValidOrEmpty() {
    QString path = m_detector->openInterpreterPath();

    if (!path.isEmpty()) {
        QVERIFY(QFile::exists(path));
    }
}

QTEST_MAIN(TestAgentDetector)
#include "test_agentdetector.moc"
