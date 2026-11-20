#include <QtTest/QtTest>
#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include "commandpalette.h"
#include "hermesoptionsdialog.h"

// Widget regressions from the audit: H3 (Hermes options dialog could never
// be accepted, and --yolo had no confirmation) and M4 (stray command palette
// at startup, overlay not filling the window, Esc not closing).
class TestHermesUi : public QObject {
    Q_OBJECT

private slots:
    void optionsDialogAccepts();
    void optionsDialogSeedsAndReturnsOptions();
    void yoloNeedsConfirmation();
    void paletteHiddenUntilOpened();
    void paletteOverlayFillsWindowAndEscCloses();
};

static QPushButton *acceptButton(QDialog &d) {
    auto *box = d.findChild<QDialogButtonBox *>();
    return box ? box->button(QDialogButtonBox::Ok) : nullptr;
}

void TestHermesUi::optionsDialogAccepts() {
    HermesOptionsDialog d(QDir::tempPath());
    QPushButton *ok = acceptButton(d);
    QVERIFY(ok);
    QSignalSpy accepted(&d, &QDialog::accepted);
    QTimer::singleShot(50, ok, [ok]() { ok->click(); });
    QTimer::singleShot(2000, &d, [&d]() { if (d.isVisible()) d.reject(); });
    QCOMPARE(d.exec(), int(QDialog::Accepted));
    QCOMPARE(accepted.count(), 1);
}

void TestHermesUi::optionsDialogSeedsAndReturnsOptions() {
    QVariantMap saved;
    saved["model"] = "anthropic/x";
    saved["maxTurns"] = 9;
    HermesOptionsDialog d(QDir::tempPath(), saved);
    const QVariantMap opts = d.options();
    QCOMPARE(opts.value("model").toString(), QString("anthropic/x"));
    QCOMPARE(opts.value("maxTurns").toInt(), 9);
}

void TestHermesUi::yoloNeedsConfirmation() {
    HermesOptionsDialog d(QDir::tempPath());
    QCheckBox *yolo = nullptr;
    for (auto *c : d.findChildren<QCheckBox *>()) if (c->text().contains("--yolo")) yolo = c;
    QVERIFY(yolo);
    yolo->setChecked(true);
    QPushButton *ok = acceptButton(d);
    bool sawConfirm = false;
    // Decline the confirmation: the dialog must stay open.
    QTimer::singleShot(50, ok, [ok]() { ok->click(); });
    QTimer::singleShot(300, &d, [&]() {
        if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            sawConfirm = true;
            box->button(QMessageBox::Cancel)->click();
        }
    });
    QTimer::singleShot(600, &d, [&d]() { if (d.isVisible()) d.reject(); });
    QCOMPARE(d.exec(), int(QDialog::Rejected));
    QVERIFY(sawConfirm);

    // Once confirmed (e.g. seeded from saved options), it accepts directly.
    HermesOptionsDialog d2(QDir::tempPath(), QVariantMap{{"yolo", true}});
    QPushButton *ok2 = acceptButton(d2);
    QTimer::singleShot(50, ok2, [ok2]() { ok2->click(); });
    QTimer::singleShot(2000, &d2, [&d2]() { if (d2.isVisible()) d2.reject(); });
    QCOMPARE(d2.exec(), int(QDialog::Accepted));
    QCOMPARE(d2.options().value("yolo").toBool(), true);
}

void TestHermesUi::paletteHiddenUntilOpened() {
    QMainWindow w;
    w.resize(1200, 800);
    w.setCentralWidget(new QWidget(&w));
    auto *p = new CommandPalette(&w);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    QVERIFY(!p->isVisible());
}

void TestHermesUi::paletteOverlayFillsWindowAndEscCloses() {
    QMainWindow w;
    w.resize(1200, 800);
    w.setCentralWidget(new QWidget(&w));
    auto *p = new CommandPalette(&w);
    p->setActions({{"new-session", "New Session", ""}});
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    p->open();
    QVERIFY(p->isVisible());
    auto *overlay = p->findChild<QWidget *>("paletteOverlay");
    QVERIFY(overlay);
    QTRY_COMPARE(overlay->size(), p->size());
    QCOMPARE(p->size(), w.size());
    QWidget *focus = QApplication::focusWidget();
    QVERIFY(focus);
    QTest::keyClick(focus, Qt::Key_Escape);
    QTRY_VERIFY(!p->isVisible());
}

QTEST_MAIN(TestHermesUi)
#include "test_hermesui.moc"
