#pragma once

#include <QColor>
#include <QHash>
#include <QIcon>
#include <QObject>
#include <QString>

class QApplication;
class QTemporaryDir;

// Central look-and-feel for Consiglio · Hermes.
//
// Every colour, radius and font size the UI uses comes from one token set
// (light or dark), so the two themes stay consistent and a widget never
// hard-codes a hex value. Theme::apply() installs:
//   - the Fusion style with a QPalette built from the tokens (dialogs, tooltips
//     and anything not covered by the stylesheet follow the theme too);
//   - one application stylesheet generated from the tokens;
//   - the application font, scaled by the user's interface-size preference.
// Icons are monochrome Lucide SVGs tinted at runtime with icon(), so they
// follow the theme without shipping per-theme artwork.
class Theme : public QObject {
    Q_OBJECT
public:
    enum class Mode { System, Light, Dark };
    Q_ENUM(Mode)

    struct Tokens {
        bool dark = true;
        QColor window, surface, raised, hover, pressed, border, borderStrong;
        QColor text, textMuted, textFaint;
        QColor accent, accentHover, accentText, accentSoft;
        QColor success, warning, danger, dangerSoft, warningSoft;
        QColor userBubble, userBubbleText, code, overlay, focus, selection;
    };

    static Theme &instance();

    // Install the theme on the application (style, palette, stylesheet, font).
    void apply(QApplication *app);

    Mode mode() const { return m_mode; }
    void setMode(Mode mode);              // persists + re-applies + emits changed()
    bool isDark() const { return m_tokens.dark; }
    const Tokens &tokens() const { return m_tokens; }

    // Interface size multiplier (0.85 … 1.6), persisted.
    double scale() const { return m_scale; }
    void setScale(double scale);
    // A pixel size scaled from the base UI font (DPI-aware).
    int px(double basePx) const;

    // A themed icon from :/icons/<name>.svg. `color` defaults to the muted
    // text colour; disabled/active variants are derived automatically.
    QIcon icon(const QString &name, const QColor &color = QColor()) const;
    QPixmap pixmap(const QString &name, int size, const QColor &color = QColor()) const;
    static bool hasIcon(const QString &name);

    static QString modeName(Mode mode);
    static Mode modeFromName(const QString &name);
    static Tokens tokensFor(bool dark);

signals:
    void changed();

private:
    Theme();
    void loadSettings();
    void rebuild();
    QString styleSheet() const;
    QString tintedFile(const QString &name, const QColor &color) const;
    static bool systemPrefersDark();

    Mode m_mode = Mode::System;
    double m_scale = 1.0;
    int m_basePx = 13;
    Tokens m_tokens;
    QApplication *m_app = nullptr;
    QTemporaryDir *m_tintDir = nullptr;
    mutable QHash<QString, QIcon> m_iconCache;
};
