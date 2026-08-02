#include <QtTest/QtTest>

#include "providerselectiondialog.h"

#include <QComboBox>
#include <QDir>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>

class TestProviderSelectionDialog : public QObject {
    Q_OBJECT

private slots:
    void acceptsCompleteSessionConfiguration();
};

void TestProviderSelectionDialog::acceptsCompleteSessionConfiguration() {
    AgentInfo provider;
    provider.id = "lan:192.168.1.243:8081";
    provider.provider = "remote_llamacpp";
    provider.name = "LAN llama.cpp";
    provider.installed = true;
    provider.authenticated = true;
    provider.endpoint = "http://192.168.1.243:8081";
    provider.model = "test-model";
    provider.diagnostic = "Ready";

    ProviderSelectionDialog dialog({provider}, "remote_llamacpp",
                                   QDir::currentPath());
    QCOMPARE(dialog.selectedProvider(), QString("remote_llamacpp"));
    QCOMPARE(dialog.selectedWorkspace(), QDir::currentPath());
    QCOMPARE(dialog.selectedSandboxMode(), QString("workspace-write"));

    QTimer::singleShot(0, &dialog, [&dialog]() {
        auto *buttons = dialog.findChild<QDialogButtonBox *>();
        QVERIFY(buttons);
        for (auto *button : buttons->buttons()) {
            if (button->text() == "Start session") {
                button->click();
                return;
            }
        }
        QFAIL("Start session button not found");
    });
    QCOMPARE(dialog.exec(), static_cast<int>(QDialog::Accepted));
}

QTEST_MAIN(TestProviderSelectionDialog)
#include "test_providerselectiondialog.moc"
