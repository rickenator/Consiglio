#include "theme.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QFontInfo>
#include <QPainter>
#include <QPalette>
#include <QRegularExpression>
#include <QSettings>
#include <QStyleFactory>
#include <QSvgRenderer>
#include <QTemporaryDir>

namespace {

QString hex(const QColor &c) {
    if (c.alpha() == 255) return c.name(QColor::HexRgb);
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(c.red()).arg(c.green()).arg(c.blue())
        .arg(QString::number(c.alphaF(), 'f', 3));
}

QColor withAlpha(QColor c, double a) {
    c.setAlphaF(a);
    return c;
}

QString svgSource(const QString &name) {
    QFile file(QStringLiteral(":/icons/%1.svg").arg(name));
    if (!file.open(QIODevice::ReadOnly)) return QString();
    return QString::fromUtf8(file.readAll());
}

QPixmap renderSvg(const QString &source, const QColor &color, int size, qreal dpr) {
    QString tinted = source;
    tinted.replace(QStringLiteral("currentColor"), color.name(QColor::HexRgb));
    if (color.alpha() < 255) {
        // Lucide icons are stroked; fade the whole glyph for translucent colours.
        tinted.replace(QStringLiteral("<svg"),
                       QStringLiteral("<svg opacity=\"%1\"").arg(color.alphaF(), 0, 'f', 3));
    }
    QSvgRenderer renderer(tinted.toUtf8());
    const int device = qMax(1, qRound(size * dpr));
    QPixmap pixmap(device, device);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter, QRectF(0, 0, device, device));
    painter.end();
    pixmap.setDevicePixelRatio(dpr);
    return pixmap;
}

// The first installed family from a preference list, so the UI looks the same
// wherever a good sans is available and never falls back to a bitmap font.
QString pickFamily(const QStringList &preferred, const QString &fallback) {
    const QStringList installed = QFontDatabase().families();
    for (const QString &family : preferred) {
        if (installed.contains(family, Qt::CaseInsensitive)) return family;
    }
    return fallback;
}

} // namespace

Theme &Theme::instance() {
    static Theme theme;
    return theme;
}

// The icons/logo live in a static library; register them explicitly.
static void initHermesResources() { Q_INIT_RESOURCE(hermes); }

Theme::Theme() {
    initHermesResources();
    loadSettings();
    m_tokens = tokensFor(m_mode == Mode::Dark || (m_mode == Mode::System && systemPrefersDark()));
}

void Theme::loadSettings() {
    QSettings settings;
    m_mode = modeFromName(settings.value("appearance/theme", "system").toString());
    m_scale = qBound(0.85, settings.value("appearance/scale", 1.0).toDouble(), 1.6);
}

Theme::Tokens Theme::tokensFor(bool dark) {
    Tokens t;
    t.dark = dark;
    if (dark) {
        t.window       = QColor("#0f1117");
        t.surface      = QColor("#151821");
        t.raised       = QColor("#1b1f2a");
        t.hover        = QColor("#232836");
        t.pressed      = QColor("#2b3142");
        t.border       = QColor("#262b38");
        t.borderStrong = QColor("#363c4d");
        t.text         = QColor("#e7e9ee");
        t.textMuted    = QColor("#a0a7b6");
        t.textFaint    = QColor("#6c7385");
        t.accent       = QColor("#7c6cff");
        t.accentHover  = QColor("#8f82ff");
        t.accentText   = QColor("#ffffff");
        t.accentSoft   = QColor(124, 108, 255, 46);
        t.success      = QColor("#3fb984");
        t.warning      = QColor("#e3b341");
        t.danger       = QColor("#f2575a");
        t.dangerSoft   = QColor(242, 87, 90, 38);
        t.warningSoft  = QColor(227, 179, 65, 36);
        t.userBubble   = QColor("#2a2f45");
        t.userBubbleText = QColor("#eef0ff");
        t.code         = QColor("#11141c");
        t.overlay      = QColor(5, 6, 10, 150);
        t.focus        = QColor("#8f82ff");
        t.selection    = QColor(124, 108, 255, 90);
    } else {
        t.window       = QColor("#f5f6f8");
        t.surface      = QColor("#ffffff");
        t.raised       = QColor("#ffffff");
        t.hover        = QColor("#eef0f4");
        t.pressed      = QColor("#e3e6ec");
        t.border       = QColor("#e2e5eb");
        t.borderStrong = QColor("#cdd2db");
        t.text         = QColor("#1b1f2b");
        t.textMuted    = QColor("#5a6274");
        t.textFaint    = QColor("#8b93a3");
        t.accent       = QColor("#5b4bea");
        t.accentHover  = QColor("#4c3cdc");
        t.accentText   = QColor("#ffffff");
        t.accentSoft   = QColor(91, 75, 234, 28);
        t.success      = QColor("#1a7f4b");
        t.warning      = QColor("#9a6700");
        t.danger       = QColor("#cf2e32");
        t.dangerSoft   = QColor(207, 46, 50, 22);
        t.warningSoft  = QColor(212, 160, 23, 30);
        t.userBubble   = QColor("#ecebff");
        t.userBubbleText = QColor("#1b1f2b");
        t.code         = QColor("#f2f3f7");
        t.overlay      = QColor(20, 22, 30, 90);
        t.focus        = QColor("#5b4bea");
        t.selection    = QColor(91, 75, 234, 60);
    }
    return t;
}

bool Theme::systemPrefersDark() {
    // Qt 5 has no colour-scheme API: an explicit override wins, then the
    // desktop's palette (a dark window colour means a dark desktop theme).
    const QByteArray forced = qgetenv("CONSIGLIO_THEME").toLower();
    if (forced == "dark") return true;
    if (forced == "light") return false;
    const QByteArray gtk = qgetenv("GTK_THEME").toLower();
    if (gtk.contains("dark")) return true;
    if (!qApp) return true;
    const QColor window = QGuiApplication::palette().color(QPalette::Window);
    return window.lightness() < 128;
}

QString Theme::modeName(Mode mode) {
    switch (mode) {
        case Mode::Light: return QStringLiteral("light");
        case Mode::Dark: return QStringLiteral("dark");
        default: return QStringLiteral("system");
    }
}

Theme::Mode Theme::modeFromName(const QString &name) {
    if (name == QLatin1String("light")) return Mode::Light;
    if (name == QLatin1String("dark")) return Mode::Dark;
    return Mode::System;
}

void Theme::apply(QApplication *app) {
    if (!m_app) {
        m_app = app;
        // Capture the desktop's palette before we replace it, so "System" can
        // still tell whether the desktop is dark.
        m_tokens = tokensFor(m_mode == Mode::Dark || (m_mode == Mode::System && systemPrefersDark()));
        m_basePx = qMax(11, QFontInfo(app->font()).pixelSize());
        app->setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    }
    rebuild();
}

void Theme::setMode(Mode mode) {
    if (mode == m_mode) return;
    m_mode = mode;
    QSettings().setValue("appearance/theme", modeName(mode));
    m_tokens = tokensFor(m_mode == Mode::Dark || (m_mode == Mode::System && systemPrefersDark()));
    rebuild();
}

void Theme::setScale(double scale) {
    scale = qBound(0.85, scale, 1.6);
    if (qFuzzyCompare(scale, m_scale)) return;
    m_scale = scale;
    QSettings().setValue("appearance/scale", scale);
    rebuild();
}

int Theme::px(double basePx) const {
    // Sizes are authored for a 13 px base font; scale to the real (DPI-aware)
    // base and the user's interface-size preference.
    return qMax(1, qRound(basePx * (m_basePx / 13.0) * m_scale));
}

void Theme::rebuild() {
    m_iconCache.clear();
    delete m_tintDir;
    m_tintDir = new QTemporaryDir(QDir::tempPath() + QStringLiteral("/consiglio-theme-XXXXXX"));

    if (m_app) {
        const Tokens &t = m_tokens;
        QPalette pal;
        pal.setColor(QPalette::Window, t.window);
        pal.setColor(QPalette::WindowText, t.text);
        pal.setColor(QPalette::Base, t.raised);
        pal.setColor(QPalette::AlternateBase, t.surface);
        pal.setColor(QPalette::ToolTipBase, t.raised);
        pal.setColor(QPalette::ToolTipText, t.text);
        pal.setColor(QPalette::PlaceholderText, t.textFaint);
        pal.setColor(QPalette::Text, t.text);
        pal.setColor(QPalette::Button, t.raised);
        pal.setColor(QPalette::ButtonText, t.text);
        pal.setColor(QPalette::BrightText, t.danger);
        pal.setColor(QPalette::Highlight, t.accent);
        pal.setColor(QPalette::HighlightedText, t.accentText);
        pal.setColor(QPalette::Link, t.accent);
        pal.setColor(QPalette::Light, t.hover);
        pal.setColor(QPalette::Midlight, t.border);
        pal.setColor(QPalette::Mid, t.borderStrong);
        pal.setColor(QPalette::Dark, t.borderStrong);
        pal.setColor(QPalette::Shadow, QColor(0, 0, 0, 120));
        for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
            pal.setColor(QPalette::Disabled, role, t.textFaint);
        }
        m_app->setPalette(pal);

        QFont font = m_app->font();
        font.setFamily(pickFamily({"Inter", "Segoe UI", "SF Pro Text", "Helvetica Neue",
                                   "Noto Sans", "IBM Plex Sans", "Cantarell", "Ubuntu",
                                   "DejaVu Sans"}, font.family()));
#ifdef Q_OS_LINUX
        // Hermes replies use emoji/symbols (⏸ 📊 ⚡). Qt 5 on Linux often can't
        // draw colour-bitmap emoji fonts and shows tofu boxes; prefer installed
        // monochrome symbol fonts as fallbacks for those glyphs.
        {
            static const QStringList symbolFonts = []() {
                QStringList found;
                const QStringList all = QFontDatabase().families();
                for (const char *f : {"Noto Sans Symbols 2", "Noto Sans Symbols", "Symbola"})
                    if (all.contains(QLatin1String(f))) found << QLatin1String(f);
                return found;
            }();
            if (!symbolFonts.isEmpty()) font.setFamilies(QStringList{font.family()} + symbolFonts);
        }
#endif
        font.setPixelSize(px(13));
        m_app->setFont(font);
        m_app->setStyleSheet(styleSheet());
    }
    emit changed();
}

QString Theme::tintedFile(const QString &name, const QColor &color) const {
    if (!m_tintDir || !m_tintDir->isValid()) return QString();
    const QString path = m_tintDir->filePath(
        QStringLiteral("%1-%2.svg").arg(name, color.name(QColor::HexArgb).mid(1)));
    if (!QFile::exists(path)) {
        QString source = svgSource(name);
        source.replace(QStringLiteral("currentColor"), color.name(QColor::HexRgb));
        QFile out(path);
        if (out.open(QIODevice::WriteOnly)) out.write(source.toUtf8());
    }
    return path;
}

bool Theme::hasIcon(const QString &name) {
    return QFile::exists(QStringLiteral(":/icons/%1.svg").arg(name));
}

QPixmap Theme::pixmap(const QString &name, int size, const QColor &color) const {
    const QString source = svgSource(name);
    if (source.isEmpty()) return QPixmap();
    const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    return renderSvg(source, color.isValid() ? color : m_tokens.textMuted, size, dpr);
}

QIcon Theme::icon(const QString &name, const QColor &color) const {
    const QColor normal = color.isValid() ? color : m_tokens.textMuted;
    const QString key = name + QLatin1Char('|') + normal.name(QColor::HexArgb);
    auto it = m_iconCache.constFind(key);
    if (it != m_iconCache.constEnd()) return it.value();

    const QString source = svgSource(name);
    QIcon icon;
    if (!source.isEmpty()) {
        const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
        // Hover/pressed brighten towards the text colour unless the caller
        // asked for a specific (e.g. on-accent) colour.
        const QColor active = color.isValid() ? color : m_tokens.text;
        const QColor disabled = withAlpha(normal, 0.38);
        for (int size : {14, 16, 18, 20, 24, 32, 48}) {
            icon.addPixmap(renderSvg(source, normal, size, dpr), QIcon::Normal, QIcon::Off);
            icon.addPixmap(renderSvg(source, normal, size, dpr), QIcon::Normal, QIcon::On);
            icon.addPixmap(renderSvg(source, active, size, dpr), QIcon::Active, QIcon::Off);
            icon.addPixmap(renderSvg(source, active, size, dpr), QIcon::Active, QIcon::On);
            icon.addPixmap(renderSvg(source, active, size, dpr), QIcon::Selected, QIcon::Off);
            icon.addPixmap(renderSvg(source, disabled, size, dpr), QIcon::Disabled, QIcon::Off);
        }
    }
    m_iconCache.insert(key, icon);
    return icon;
}

QString Theme::styleSheet() const {
    const Tokens &t = m_tokens;
    const QString chevron = tintedFile(QStringLiteral("chevron-down"), t.textMuted);
    const QString check = tintedFile(QStringLiteral("check"), t.accentText);
    const int r = px(8);     // control radius
    const int rl = px(12);   // card radius
    // MSVC limits one string literal to ~16 KB: the sheet is split into
    // adjacent raw-string pieces that the compiler concatenates.
    QString s = QString::fromUtf8(
R"QSS(
* { outline: none; }
QWidget { color: %text; }
QMainWindow, QDialog { background: %window; }
QWidget#appCentral, QWidget#chatPane, QScrollArea#conversationScroll,
QWidget#conversationHost { background: %window; }
QToolTip {
    background: %raised; color: %text; border: 1px solid %borderStrong;
    border-radius: %rpx; padding: %p4px %p8px;
}

/* ── Menus ─────────────────────────────────────────────── */
QMenuBar {
    background: %surface; color: %text; border-bottom: 1px solid %border;
    padding: %p2px %p6px;
}
QMenuBar::item { background: transparent; padding: %p5px %p10px; border-radius: %rpx; }
QMenuBar::item:selected, QMenuBar::item:pressed { background: %hover; }
QMenu {
    background: %raised; color: %text; border: 1px solid %borderStrong;
    border-radius: %rlpx; padding: %p6px;
}
QMenu::item {
    padding: %p6px %p28px %p6px %p10px; border-radius: %p6px; margin: 1px 0;
}
QMenu::item:selected { background: %accentSoft; color: %text; }
QMenu::item:disabled { color: %textFaint; }
QMenu::icon { padding-left: %p8px; }
QMenu::separator { height: 1px; background: %border; margin: %p6px %p8px; }
QMenu::right-arrow { width: %p12px; height: %p12px; }

/* ── Toolbar ───────────────────────────────────────────── */
QToolBar#mainToolbar {
    background: %surface; border: none; border-bottom: 1px solid %border;
    padding: %p6px %p12px; spacing: %p4px;
}
QToolBar#mainToolbar::separator { width: 1px; background: %border; margin: %p6px %p8px; }
QToolButton {
    background: transparent; color: %text; border: 1px solid transparent;
    border-radius: %rpx; padding: %p6px %p10px;
}
QToolButton:hover { background: %hover; }
QToolButton:pressed, QToolButton:checked { background: %pressed; }
QToolButton:disabled { color: %textFaint; }
QToolButton[popupMode="1"] { padding-right: %p20px; }
QToolButton::menu-button {
    border: none; border-left: 1px solid %border; width: %p18px;
    border-top-right-radius: %rpx; border-bottom-right-radius: %rpx;
}
QToolButton::menu-button:hover { background: %pressed; }
QToolButton::menu-arrow, QToolButton::menu-indicator {
    image: url(%chevron); width: %p12px; height: %p12px;
}
QToolButton::menu-indicator { subcontrol-position: right center; subcontrol-origin: padding; right: %p6px; }
QLabel#toolbarLabel { color: %textMuted; padding: 0 %p2px 0 %p8px; }

/* ── Buttons ───────────────────────────────────────────── */
QPushButton {
    background: %raised; color: %text; border: 1px solid %borderStrong;
    border-radius: %rpx; padding: %p7px %p14px; min-height: %p18px;
}
QPushButton:hover { background: %hover; }
QPushButton:pressed { background: %pressed; }
QPushButton:disabled { color: %textFaint; border-color: %border; background: %surface; }
QPushButton:default { border-color: %accent; }
QPushButton[role="primary"] {
    background: %accent; color: %accentText; border-color: %accent; font-weight: 600;
}
QPushButton[role="primary"]:hover { background: %accentHover; border-color: %accentHover; }
QPushButton[role="primary"]:disabled { background: %hover; color: %textFaint; border-color: %border; }
QPushButton[role="primary"]:default { background: %accent; color: %accentText; border-color: %accent; }
QPushButton[role="primary"]:default:hover { background: %accentHover; border-color: %accentHover; }
QPushButton[role="primary"]:default:disabled { background: %hover; color: %textFaint; border-color: %border; }
QPushButton[role="danger"] { background: %raised; color: %danger; border-color: %borderStrong; }
QPushButton[role="danger"]:default { background: %raised; color: %danger; border-color: %danger; }
QPushButton[role="danger"]:hover { background: %dangerSoft; border-color: %danger; }
QPushButton[role="ghost"] { background: transparent; border-color: transparent; color: %textMuted; }
QPushButton[role="ghost"]:hover { background: %hover; color: %text; }
QPushButton[role="chip"] {
    background: %surface; border: 1px solid %border; border-radius: %p14px;
    padding: %p4px %p12px; color: %text;
}
QPushButton[role="chip"]:hover { border-color: %accent; background: %accentSoft; }
QPushButton[role="chip"]:checked { border-color: %accent; background: %accentSoft; color: %text; }

/* ── Inputs ────────────────────────────────────────────── */
QLineEdit, QPlainTextEdit, QTextEdit, QSpinBox, QDoubleSpinBox {
    background: %raised; color: %text; border: 1px solid %borderStrong;
    border-radius: %rpx; padding: %p6px %p10px;
    selection-background-color: %selection; selection-color: %text;
}
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QSpinBox:focus { border: 1px solid %focus; }
QLineEdit:disabled, QPlainTextEdit:disabled { color: %textFaint; background: %surface; }
QSpinBox, QDoubleSpinBox { padding-right: %p26px; }
QSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {
    subcontrol-origin: border; width: %p22px; border: none; border-left: 1px solid %border; background: transparent;
}
QSpinBox::up-button, QDoubleSpinBox::up-button { subcontrol-position: top right; border-top-right-radius: %rpx; }
QSpinBox::down-button, QDoubleSpinBox::down-button { subcontrol-position: bottom right; border-bottom-right-radius: %rpx; }
QSpinBox::up-button:hover, QSpinBox::down-button:hover, QDoubleSpinBox::up-button:hover, QDoubleSpinBox::down-button:hover { background: %hover; }
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { image: url(%chevronUp); width: %p10px; height: %p10px; }
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { image: url(%chevron); width: %p10px; height: %p10px; }
QSlider::groove:horizontal { height: %p4px; background: %hover; border-radius: %p2px; }
QSlider::sub-page:horizontal { background: %accent; border-radius: %p2px; }
QSlider::handle:horizontal {
    background: %accentText; border: %p2px solid %accent; width: %p12px; height: %p12px;
    margin: -%p6px 0; border-radius: %p8px;
}
QSlider::handle:horizontal:hover { background: %accentSoft; }
QComboBox {
    background: %raised; color: %text; border: 1px solid %borderStrong;
    border-radius: %rpx; padding: %p5px %p30px %p5px %p10px; min-height: %p18px;
}
QComboBox:hover { border-color: %textFaint; }
QComboBox:focus { border-color: %focus; }
QComboBox:disabled { color: %textFaint; background: %surface; border-color: %border; }
QComboBox::drop-down { border: none; width: %p26px; subcontrol-position: right center; }
QComboBox::down-arrow { image: url(%chevron); width: %p12px; height: %p12px; }
QComboBox QAbstractItemView {
    background: %raised; border: 1px solid %borderStrong; border-radius: %rpx;
)QSS"
R"QSS(    padding: %p4px; selection-background-color: %accentSoft; selection-color: %text;
    outline: none;
}
QComboBox#toolbarCombo { background: transparent; border-color: transparent; }
QComboBox#toolbarCombo:hover { background: %hover; border-color: %border; }
QCheckBox, QRadioButton { spacing: %p8px; color: %text; }
QCheckBox::indicator, QRadioButton::indicator {
    width: %p16px; height: %p16px; border: 1px solid %borderStrong; background: %raised;
}
QCheckBox::indicator { border-radius: %p4px; }
QRadioButton::indicator { border-radius: %p8px; }
QCheckBox::indicator:checked { background: %accent; border-color: %accent; image: url(%check); }
QRadioButton::indicator:checked {
    border-color: %accent;
    background: qradialgradient(cx:0.5, cy:0.5, radius:0.5, fx:0.5, fy:0.5,
        stop:0 %raised, stop:0.36 %raised, stop:0.42 %accent, stop:1 %accent);
}
QCheckBox::indicator:hover, QRadioButton::indicator:hover { border-color: %accent; }

/* ── Lists, scroll bars, progress ─────────────────────── */
QListWidget, QListView, QTreeView, QTableView {
    background: transparent; border: none; color: %text; outline: none;
}
QListWidget::item, QListView::item { padding: %p6px %p8px; border-radius: %p6px; }
QListWidget::item:hover, QListView::item:hover { background: %hover; }
QListWidget::item:selected, QListView::item:selected { background: %accentSoft; color: %text; }
QScrollBar:vertical { background: transparent; width: %p10px; margin: %p2px; }
QScrollBar::handle:vertical { background: %borderStrong; border-radius: %p3px; min-height: %p32px; }
QScrollBar::handle:vertical:hover { background: %textFaint; }
QScrollBar:horizontal { background: transparent; height: %p10px; margin: %p2px; }
QScrollBar::handle:horizontal { background: %borderStrong; border-radius: %p3px; min-width: %p32px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QProgressBar {
    background: %hover; border: none; border-radius: %p3px; max-height: %p6px; min-height: %p6px;
}
QProgressBar::chunk { background: %accent; border-radius: %p3px; }
QProgressBar[level="warn"]::chunk { background: %warning; }
QProgressBar[level="danger"]::chunk { background: %danger; }

/* ── Status bar ────────────────────────────────────────── */
QStatusBar {
    background: %surface; color: %textMuted; border-top: 1px solid %border;
    padding: 0 %p8px; min-height: %p30px;
}
QStatusBar::item { border: none; }
QStatusBar QLabel { color: %textMuted; padding: 0 %p6px; }
QLabel#statusDot { padding: 0; }

/* ── Text roles ────────────────────────────────────────── */
QLabel[role="title"] { font-size: %p20px; font-weight: 650; color: %text; }
QLabel[role="heading"] { font-size: %p15px; font-weight: 600; color: %text; }
QLabel[role="section"] {
    font-size: %p11px; font-weight: 600; color: %textFaint; letter-spacing: 0.6px;
}
QLabel[role="muted"] { color: %textMuted; }
QLabel[role="faint"] { color: %textFaint; }
QLabel[role="mono"] { font-family: "JetBrains Mono", "Fira Code", "DejaVu Sans Mono", monospace; }
QLabel[role="badge"] {
    background: %accentSoft; color: %text; border-radius: %p9px; padding: %p2px %p8px;
    font-size: %p11px; font-weight: 600;
}
QLabel[role="kbd"] {
    background: %surface; color: %textMuted; border: 1px solid %borderStrong; border-radius: %p5px;
    padding: %p1px %p6px; font-size: %p11px;
    font-family: "JetBrains Mono", "Fira Code", "DejaVu Sans Mono", monospace;
}
a { color: %accent; }

/* ── Cards, banners, rail ─────────────────────────────── */
QFrame[role="card"] { background: %surface; border: 1px solid %border; border-radius: %rlpx; }
QFrame[role="sheet"] { background: %raised; border: 1px solid %borderStrong; border-radius: %p14px; }
QFrame#banner { border-radius: %rpx; border: 1px solid %border; background: %surface; }
QFrame#banner[level="error"] { background: %dangerSoft; border-color: %danger; }
QFrame#banner[level="warning"] { background: %warningSoft; border-color: %warning; }
QFrame#banner[level="info"] { background: %accentSoft; border-color: %accent; }
QWidget#sessionRail { background: %surface; border-right: 1px solid %border; }
QWidget#sessionRail QListWidget::item { padding: %p8px %p10px; }
QWidget#overlayDim { background: %overlay; }
QListWidget#settingsNav { background: %surface; border-right: 1px solid %border; padding: %p8px; }
QListWidget#settingsNav::item { padding: %p8px %p12px; margin: 1px 0; }
QWidget#settingsPage { background: %window; }
QTabWidget::pane { border: 1px solid %border; border-radius: %rpx; top: -1px; }
QTabBar::tab { background: transparent; color: %textMuted; padding: %p6px %p14px; border-bottom: 2px solid transparent; }
QTabBar::tab:selected { color: %text; border-bottom-color: %accent; }
QDialogButtonBox QPushButton { min-width: %p84px; }
QMessageBox QLabel { color: %text; }

/* ── Conversation ──────────────────────────────────────── */
QWidget#conversationColumn { background: transparent; }
QFrame#userBubble { background: %userBubble; border: none; border-radius: %p16px; }
QLabel#userBubbleText { color: %userBubbleText; background: transparent; }
QFrame#commandPill { background: %surface; border: 1px solid %border; border-radius: %p16px; }
QLabel#commandPillText { color: %text; background: transparent;
    font-family: "JetBrains Mono", "Fira Code", "DejaVu Sans Mono", monospace; }
QLabel#blockAuthor { color: %text; font-weight: 600; }
QLabel#blockTime { color: %textFaint; font-size: %p11px; }
QTextEdit#agentBody, QTextEdit#thinkingBody {
    background: transparent; border: none; padding: 0; margin: 0;
}
QTextEdit#thinkingBody { color: %textMuted; }
QFrame#thinkingCard { background: transparent; border: none; border-left: 2px solid %borderStrong;
    border-radius: 0; }
QFrame#toolCard { background: %surface; border: 1px solid %border; border-radius: %rpx; }
QFrame#toolCard[state="failed"] { border-color: %danger; }
QTextEdit#toolBody {
    background: %code; border: none; border-top: 1px solid %border; border-radius: 0;
    border-bottom-left-radius: %rpx; border-bottom-right-radius: %rpx; padding: %p8px %p10px;
    font-family: "JetBrains Mono", "Fira Code", "DejaVu Sans Mono", monospace;
}
QToolButton#blockToggle {
    background: transparent; border: none; color: %textMuted; padding: %p2px %p4px;
    text-align: left; font-weight: 500;
}
QToolButton#blockToggle:hover { color: %text; background: transparent; }
QToolButton#blockAction { background: transparent; border: none; padding: %p3px; border-radius: %p6px; }
QToolButton#blockAction:hover { background: %hover; }
)QSS"
R"QSS(QLabel#toolTitle { color: %text; background: transparent;
    font-family: "JetBrains Mono", "Fira Code", "DejaVu Sans Mono", monospace; }
QLabel#statusBadge { border-radius: %p8px; padding: %p1px %p8px; font-size: %p11px; font-weight: 600;
    background: %hover; color: %textMuted; }
QLabel#statusBadge[state="completed"] { color: %success; }
QLabel#statusBadge[state="failed"] { background: %dangerSoft; color: %danger; }
QLabel#statusBadge[state="in_progress"], QLabel#statusBadge[state="pending"] { background: %accentSoft; color: %text; }
QLabel#noticeText { color: %textMuted; background: transparent; }
QLabel#noticeText[level="error"] { color: %danger; }
QLabel#noticeText[level="warning"] { color: %warning; }
QLabel#noticeText[level="success"] { color: %success; }

/* ── Empty state ───────────────────────────────────────── */
QWidget#emptyState { background: transparent; }
QLabel#emptyTitle { font-size: %p22px; font-weight: 650; color: %text; }
QLabel#emptySubtitle { color: %textMuted; font-size: %p14px; }
QPushButton#emptyAction {
    background: %surface; border: 1px solid %border; border-radius: %rlpx;
    padding: %p10px %p14px; text-align: left; color: %text; min-height: %p38px;
}
QPushButton#emptyAction:hover { border-color: %accent; background: %accentSoft; }
QPushButton#emptyAction:disabled { color: %textFaint; }
QLabel#emptyActionTitle { color: %text; font-weight: 600; background: transparent; }
QLabel#emptyActionText { color: %textMuted; font-size: %p12px; background: transparent; }

/* ── Composer ──────────────────────────────────────────── */
QWidget#composerArea { background: %window; }
QFrame#composerCard { background: %raised; border: 1px solid %borderStrong; border-radius: %p16px; }
QFrame#composerCard[focused="true"] { border-color: %focus; }
QFrame#composerCard[enabled="false"] { background: %surface; border-color: %border; }
QPlainTextEdit#composerInput {
    background: transparent; border: none; padding: 0; margin: 0;
    selection-background-color: %selection;
}
QLabel#composerPlaceholder { color: %textFaint; background: transparent; padding: 0; }
QToolButton#sendButton {
    background: %accent; border: none; border-radius: %p16px; padding: %p6px;
}
QToolButton#sendButton:hover { background: %accentHover; }
QToolButton#sendButton:disabled { background: %hover; }
QToolButton#sendButton[mode="stop"] { background: %text; }
QToolButton#composerTool { background: transparent; border: none; border-radius: %p8px; padding: %p5px; }
QToolButton#composerTool:hover { background: %hover; }
QLabel#composerHint { color: %textFaint; font-size: %p11px; background: transparent; }
QLabel#busyLabel { color: %textMuted; background: transparent; }

/* ── Palette ───────────────────────────────────────────── */
QFrame#paletteSheet { background: %raised; border: 1px solid %borderStrong; border-radius: %p14px; }
QLineEdit#paletteSearch {
    background: transparent; border: none; border-bottom: 1px solid %border; border-radius: 0;
    padding: %p12px %p6px; font-size: %p15px;
}
QListView#paletteList { background: transparent; border: none; padding: %p6px; }
QLabel#paletteFooter { color: %textFaint; font-size: %p11px; background: transparent; }
QWidget#paletteFooterBar { border-top: 1px solid %border; background: transparent; }

/* ── Rail ──────────────────────────────────────────────── */
QPushButton#railWorkspace {
    background: transparent; border: 1px solid %border; border-radius: %rpx; text-align: left;
    padding: %p7px %p10px; color: %text;
}
QPushButton#railWorkspace:hover { background: %hover; }
QFrame#railActive { background: %raised; border: 1px solid %border; border-radius: %rlpx; }
QLabel#railActiveTitle { font-weight: 600; color: %text; background: transparent; }
QLineEdit#railFilter { background: %window; }
QListWidget#railSessions::item { padding: %p8px %p10px; margin: 1px 0; }
QLabel#statusDot { background: transparent; }
QLabel#statusSandbox { color: %accent; font-weight: 600; background: transparent; }
)QSS");

    // Scaled sizes: %pNpx → px(N).
    static const QRegularExpression sizeToken(QStringLiteral("%p(\\d+)px"));
    QString out;
    out.reserve(s.size() + 512);
    int last = 0;
    auto it = sizeToken.globalMatch(s);
    while (it.hasNext()) {
        const auto m = it.next();
        out += s.midRef(last, m.capturedStart() - last);
        out += QString::number(px(m.captured(1).toInt())) + QStringLiteral("px");
        last = m.capturedEnd();
    }
    out += s.midRef(last);

    const QString chevronOnAccent = tintedFile(QStringLiteral("chevron-down"), t.accentText);
    const QString chevronUp = tintedFile(QStringLiteral("chevron-up"), t.textMuted);
    const QList<QPair<QString, QString>> colors = {
        {"%chevronOnAccent", chevronOnAccent}, {"%chevronUp", chevronUp}, {"%chevron", chevron}, {"%check", check},
        {"%rlpx", QString::number(rl) + "px"}, {"%rpx", QString::number(r) + "px"},
        {"%window", hex(t.window)}, {"%surface", hex(t.surface)}, {"%raised", hex(t.raised)},
        {"%hover", hex(t.hover)}, {"%pressed", hex(t.pressed)},
        {"%borderStrong", hex(t.borderStrong)}, {"%border", hex(t.border)},
        {"%textMuted", hex(t.textMuted)}, {"%textFaint", hex(t.textFaint)}, {"%text", hex(t.text)},
        {"%accentHover", hex(t.accentHover)}, {"%accentText", hex(t.accentText)},
        {"%accentSoft", hex(t.accentSoft)}, {"%accent", hex(t.accent)},
        {"%success", hex(t.success)}, {"%warningSoft", hex(t.warningSoft)}, {"%warning", hex(t.warning)},
        {"%dangerSoft", hex(t.dangerSoft)}, {"%danger", hex(t.danger)},
        {"%userBubbleText", hex(t.userBubbleText)}, {"%userBubble", hex(t.userBubble)},
        {"%code", hex(t.code)},
        {"%overlay", hex(t.overlay)}, {"%focus", hex(t.focus)}, {"%selection", hex(t.selection)},
    };
    for (const auto &pair : colors) out.replace(pair.first, pair.second);
    return out;
}
