#include "fontsettings.h"

#include <QSettings>

namespace {
constexpr int kChatDefault = 16;
constexpr int kThinkingDefault = 15;
constexpr int kToolDefault = 13;
} // namespace

FontSettings::FontSettings(QObject *parent) : QObject(parent) {
    QSettings s;
    m_chat = s.value("fonts/chat", kChatDefault).toInt();
    m_thinking = s.value("fonts/thinking", kThinkingDefault).toInt();
    m_tool = s.value("fonts/tool", kToolDefault).toInt();
}

int FontSettings::chatPx() const { return m_chat; }
int FontSettings::thinkingPx() const { return m_thinking; }
int FontSettings::toolPx() const { return m_tool; }

void FontSettings::setChatPx(int px) {
    if (px < 8 || px > 72) return;
    m_chat = px;
    QSettings s;
    s.setValue("fonts/chat", px);
    emit changed();
}

void FontSettings::setThinkingPx(int px) {
    if (px < 8 || px > 72) return;
    m_thinking = px;
    QSettings s;
    s.setValue("fonts/thinking", px);
    emit changed();
}

void FontSettings::setToolPx(int px) {
    if (px < 8 || px > 72) return;
    m_tool = px;
    QSettings s;
    s.setValue("fonts/tool", px);
    emit changed();
}

void FontSettings::resetToDefaults() {
    m_chat = kChatDefault;
    m_thinking = kThinkingDefault;
    m_tool = kToolDefault;
    QSettings s;
    s.setValue("fonts/chat", m_chat);
    s.setValue("fonts/thinking", m_thinking);
    s.setValue("fonts/tool", m_tool);
}
