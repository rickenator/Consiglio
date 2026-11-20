#include <QtTest/QtTest>

#include "settingsdialog.h"

#include <QLineEdit>
#include <QPushButton>

class TestSettingsDialog : public QObject {
    Q_OBJECT

private slots:
    void editsPreferredName();
    void restoresDefaultForBlankName();
};

void TestSettingsDialog::editsPreferredName() {
    AppSettings settings;
    SettingsDialog dialog(&settings, nullptr);
    auto *nameEdit = dialog.findChild<QLineEdit *>("userNameEdit");
    auto *saveButton = dialog.findChild<QPushButton *>("saveSettingsButton");
    QVERIFY(nameEdit);
    QVERIFY(saveButton);

    QCOMPARE(nameEdit->text(), QString("Dude"));
    nameEdit->setText("  Rick  ");
    saveButton->click();
    QCOMPARE(settings.userName, QString("Rick"));
}

void TestSettingsDialog::restoresDefaultForBlankName() {
    AppSettings settings;
    settings.userName = "Rick";
    SettingsDialog dialog(&settings, nullptr);
    auto *nameEdit = dialog.findChild<QLineEdit *>("userNameEdit");
    auto *saveButton = dialog.findChild<QPushButton *>("saveSettingsButton");
    QVERIFY(nameEdit);
    QVERIFY(saveButton);

    nameEdit->clear();
    saveButton->click();
    QCOMPARE(settings.userName, QString("Dude"));
}

QTEST_MAIN(TestSettingsDialog)
#include "test_settingsdialog.moc"
