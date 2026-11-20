#include "fontsettingsdialog.h"

#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>

FontSettingsDialog::FontSettingsDialog(FontSettings *fonts, QWidget *parent)
    : QDialog(parent), m_fonts(fonts) {
    setWindowTitle(tr("Fonts"));
    setModal(true);

    auto *layout = new QFormLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    auto makeSpin = [&](int current) {
        auto *spin = new QSpinBox(this);
        spin->setRange(8, 72);
        spin->setValue(current);
        spin->setSuffix(" px");
        return spin;
    };

    auto *chatSpin = makeSpin(m_fonts->chatPx());
    auto *thinkingSpin = makeSpin(m_fonts->thinkingPx());
    auto *toolSpin = makeSpin(m_fonts->toolPx());

    layout->addRow(tr("Chat (answers + your messages)"), chatSpin);
    layout->addRow(tr("Thinking (reasoning stream)"), thinkingSpin);
    layout->addRow(tr("Tool (tool calls + output)"), toolSpin);

    // Apply live as the user types; persist via FontSettings.
    connect(chatSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        m_fonts->setChatPx(v);
    });
    connect(thinkingSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        m_fonts->setThinkingPx(v);
    });
    connect(toolSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        m_fonts->setToolPx(v);
    });

    auto *resetBtn = new QPushButton(tr("Reset to Defaults"), this);
    connect(resetBtn, &QPushButton::clicked, this, [this, chatSpin, thinkingSpin, toolSpin]() {
        m_fonts->resetToDefaults();
        chatSpin->blockSignals(true);
        thinkingSpin->blockSignals(true);
        toolSpin->blockSignals(true);
        chatSpin->setValue(m_fonts->chatPx());
        thinkingSpin->setValue(m_fonts->thinkingPx());
        toolSpin->setValue(m_fonts->toolPx());
        chatSpin->blockSignals(false);
        thinkingSpin->blockSignals(false);
        toolSpin->blockSignals(false);
    });
    layout->addRow(QString(), resetBtn);

    auto *closeBtn = new QPushButton(tr("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, [this]() { accept(); });
    layout->addRow(QString(), closeBtn);

    resize(360, 0);
}
