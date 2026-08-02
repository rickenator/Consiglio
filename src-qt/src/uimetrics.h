#pragma once

#include <QApplication>
#include <QFont>
#include <QScreen>
#include <QtGlobal>
#include <QtMath>

namespace UiMetrics {

inline qreal scale() {
    const QScreen *screen = QApplication::primaryScreen();
    if (!screen) return 1.0;

    const QSize available = screen->availableGeometry().size();
    const qreal pixelRatio = screen->devicePixelRatio();
    const qreal widthScale = (available.width() / 1920.0) * pixelRatio;
    const qreal heightScale = (available.height() / 1080.0) * pixelRatio;
    return qBound(0.85, qMin(widthScale, heightScale), 2.0);
}

inline int px(int designPixels) {
    return qMax(1, qRound(designPixels * scale()));
}

inline int bodyText() { return px(21); }
inline int secondaryText() { return px(18); }
inline int titleText() { return px(32); }
inline int panelMargin() { return px(28); }
inline int panelSpacing() { return px(14); }
inline int controlPadding() { return px(10); }
inline int cornerRadius() { return px(7); }


inline QFont bodyFont() {
    QFont font("Segoe UI");
    font.setStyleHint(QFont::SansSerif);
    font.setPixelSize(bodyText());
    return font;
}

inline QFont titleFont() {
    QFont font = bodyFont();
    font.setPixelSize(titleText());
    font.setWeight(QFont::Bold);
    return font;
}

inline QFont secondaryFont() {
    QFont font = bodyFont();
    font.setPixelSize(secondaryText());
    return font;
}

} // namespace UiMetrics
