#include <QtTest/QtTest>

#include "eventtimeline.h"

#include <QTextEdit>

class TestEventTimeline : public QObject {
    Q_OBJECT

private slots:
    void rendersAgentMarkdownAndMath();
    void animatesThinkingState();
};

void TestEventTimeline::rendersAgentMarkdownAndMath() {
    EventTimeline timeline;
    EventModel::EventItem event;
    event.type = EventModel::AssistantMessage;
    event.timestamp = 1;
    event.content = R"MARKDOWN(## Stochastic Functional Diffusion

This has **important structure** and inline math \(u(t,x)\).

- nonlinear diffusion
- nonlocal functional

| Reference | Focus |
|---|---|
| Da Prato | SPDE theory |

\[
du = \Delta_M F[u] \, dt
\]
)MARKDOWN";

    timeline.addEvent(event);

    auto *transcript = timeline.findChild<QTextEdit *>();
    QVERIFY(transcript);
    const QString plainText = transcript->toPlainText();
    const QString html = transcript->toHtml();

    QVERIFY(plainText.contains("Stochastic Functional Diffusion"));
    QVERIFY(plainText.contains("nonlinear diffusion"));
    QVERIFY(plainText.contains("Reference"));
    QVERIFY(plainText.contains("du = ΔM F[u]"));
    QVERIFY(!plainText.contains("## Stochastic"));
    QVERIFY(!plainText.contains("**important structure**"));
    QVERIFY(!plainText.contains("|---|---|"));
    QVERIFY(!plainText.contains("\\["));
    QVERIFY(!plainText.contains("\\]"));
    QVERIFY(!plainText.contains("\\("));
    QVERIFY(!plainText.contains("\\)"));
    QVERIFY(!plainText.contains("\\Delta"));
    QVERIFY(html.contains("font-weight:700") || html.contains("font-weight:600"));
    QVERIFY(html.contains("<table"));
}

void TestEventTimeline::animatesThinkingState() {
    EventTimeline timeline;
    timeline.show();
    auto *indicator = timeline.findChild<QLabel *>("thinkingIndicator");
    QVERIFY(indicator);
    QVERIFY(!timeline.isThinking());
    QVERIFY(indicator->isHidden());

    timeline.setThinking(true);
    QVERIFY(timeline.isThinking());
    QVERIFY(indicator->isVisible());
    const QString firstFrame = indicator->text();
    QTest::qWait(120);
    QVERIFY(indicator->text() != firstFrame);

    timeline.setThinking(false);
    QVERIFY(!timeline.isThinking());
    QVERIFY(indicator->isHidden());
}

QTEST_MAIN(TestEventTimeline)
#include "test_eventtimeline.moc"
