#include <QStyleOptionComboBox>
#include <QStylePainter>
#include "uiutil.h"

#include "theme.h"

#include <QAction>
#include <QEvent>
#include <QStyle>
#include <QLayout>
#include <QDateTime>
#include <QKeySequence>
#include <QPainter>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QTimerEvent>

namespace ui {

QString withShortcut(const QString &text, const QKeySequence &shortcut) {
    if (shortcut.isEmpty()) return text;
    return QStringLiteral("%1 (%2)").arg(text, shortcut.toString(QKeySequence::NativeText));
}

void describe(QWidget *w, const QString &name, const QString &tooltip) {
    if (!w) return;
    w->setAccessibleName(name);
    const QString tip = tooltip.isEmpty() ? name : tooltip;
    w->setToolTip(tip);
    if (tip != name) w->setAccessibleDescription(tip);
}

void describe(QAction *a, const QString &tooltip) {
    if (!a) return;
    QString base = tooltip.isEmpty() ? a->text() : tooltip;
    base.remove(QLatin1Char('&'));
    if (base.endsWith(QStringLiteral("…"))) base.chop(1);
    a->setToolTip(withShortcut(base, a->shortcut()));
    a->setStatusTip(tooltip.isEmpty() ? QString() : tooltip);
}

QToolButton *iconButton(const QString &icon, const QString &tooltip, QWidget *parent,
                        const QString &objectName, int iconPx) {
    auto *b = new QToolButton(parent);
    b->setObjectName(objectName);
    b->setAutoRaise(true);
    b->setCursor(Qt::PointingHandCursor);
    b->setFocusPolicy(Qt::TabFocus);
    const int s = Theme::instance().px(iconPx);
    b->setIconSize(QSize(s, s));
    b->setIcon(Theme::instance().icon(icon));
    b->setProperty("themeIcon", icon);
    describe(b, tooltip);
    return b;
}

namespace {
class FitOnShow : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject *obj, QEvent *event) override {
        if (event->type() == QEvent::Show) {
            auto *w = static_cast<QWidget *>(obj);
            w->ensurePolished();
            for (QWidget *c : w->findChildren<QWidget *>()) {
                // Widgets polished early (e.g. by QDialogButtonBox) before their
                // "role" property was set need a repolish to pick up the QSS.
                if (c->property("role").isValid()) {
                    c->style()->unpolish(c);
                    c->style()->polish(c);
                }
                c->ensurePolished();
            }
            if (QLayout *l = w->layout()) {
                l->invalidate();
                l->activate();
                const int width = qMax(w->minimumWidth(), qMax(w->width(), l->sizeHint().width()));
                const int height = l->hasHeightForWidth() ? l->totalHeightForWidth(width)
                                                          : l->totalSizeHint().height();
                const int h = qMax(height, l->totalMinimumSize().height());
                w->setMinimumHeight(h);
                w->resize(width, h);
            }
        }
        return false;
    }
};
} // namespace

void fitOnShow(QWidget *dialog) {
    if (!dialog || dialog->property("fitOnShow").toBool()) return;
    dialog->setProperty("fitOnShow", true);
    dialog->installEventFilter(new FitOnShow(dialog));
}

// ─── ElidedLabel ──────────────────────────────────────────────────────────

ElidedLabel::ElidedLabel(QWidget *parent, Qt::TextElideMode mode) : QLabel(parent), m_mode(mode) {
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
}

void ElidedLabel::setFullText(const QString &text) {
    m_full = text;
    setToolTip(text);
    updateElided();
}

QSize ElidedLabel::minimumSizeHint() const {
    return QSize(fontMetrics().horizontalAdvance(QStringLiteral("…")) * 3, QLabel::minimumSizeHint().height());
}

QSize ElidedLabel::sizeHint() const {
    return QSize(fontMetrics().horizontalAdvance(m_full) + 2, QLabel::sizeHint().height());
}

void ElidedLabel::resizeEvent(QResizeEvent *e) {
    QLabel::resizeEvent(e);
    updateElided();
}

void ElidedLabel::updateElided() {
    QLabel::setText(fontMetrics().elidedText(m_full, m_mode, qMax(0, width())));
}

// ─── Spinner ──────────────────────────────────────────────────────────────

Spinner::Spinner(QWidget *parent, int diameter) : QWidget(parent), m_diameter(diameter) {
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setAttribute(Qt::WA_TranslucentBackground);
    setAccessibleName(tr("Working"));
}

QSize Spinner::sizeHint() const {
    const int d = Theme::instance().px(m_diameter);
    return QSize(d, d);
}

void Spinner::setRunning(bool running) {
    m_wanted = running;
    if (running && isVisible() && !m_timer) m_timer = startTimer(16);
    if (!running && m_timer) {
        killTimer(m_timer);
        m_timer = 0;
    }
    update();
}

void Spinner::showEvent(QShowEvent *) {
    if (m_wanted && !m_timer) m_timer = startTimer(16);
}

void Spinner::hideEvent(QHideEvent *) {
    if (m_timer) {
        killTimer(m_timer);
        m_timer = 0;
    }
}

void Spinner::timerEvent(QTimerEvent *) {
    m_angle = (m_angle + 6) % 360;
    update();
}

void Spinner::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const auto &t = Theme::instance().tokens();
    const qreal pen = qMax(1.5, width() / 8.0);
    const QRectF r = QRectF(rect()).adjusted(pen, pen, -pen, -pen);
    QColor track = t.accent;
    track.setAlphaF(0.18);
    p.setPen(QPen(track, pen));
    p.drawEllipse(r);
    p.setPen(QPen(t.accent, pen, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(r, -m_angle * 16, 100 * 16);
}

// ─── StatusDot ────────────────────────────────────────────────────────────

StatusDot::StatusDot(QWidget *parent) : QWidget(parent) {
    setObjectName("statusDot");
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void StatusDot::setState(const QString &state) {
    m_state = state;
    update();
}

QSize StatusDot::sizeHint() const {
    const int d = Theme::instance().px(8);
    return QSize(d + 2, d + 2);
}

void StatusDot::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const auto &t = Theme::instance().tokens();
    QColor c = t.textFaint;
    if (m_state == "ok") c = t.success;
    else if (m_state == "busy") c = t.accent;
    else if (m_state == "warn") c = t.warning;
    else if (m_state == "error") c = t.danger;
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    const int d = qMin(width(), height()) - 2;
    p.drawEllipse(QRectF((width() - d) / 2.0, (height() - d) / 2.0, d, d));
}

// ─── FlowLayout ───────────────────────────────────────────────────────────

FlowLayout::FlowLayout(QWidget *parent, int hSpacing, int vSpacing)
    : QLayout(parent), m_h(hSpacing), m_v(vSpacing) {
    setContentsMargins(0, 0, 0, 0);
}

FlowLayout::~FlowLayout() {
    while (QLayoutItem *item = takeAt(0)) delete item;
}

void FlowLayout::addItem(QLayoutItem *item) { m_items.append(item); }
int FlowLayout::count() const { return m_items.size(); }
QLayoutItem *FlowLayout::itemAt(int index) const { return m_items.value(index); }
QLayoutItem *FlowLayout::takeAt(int index) {
    return (index >= 0 && index < m_items.size()) ? m_items.takeAt(index) : nullptr;
}

int FlowLayout::heightForWidth(int width) const { return doLayout(QRect(0, 0, width, 0), true); }

QSize FlowLayout::minimumSize() const {
    QSize size;
    for (QLayoutItem *item : m_items) size = size.expandedTo(item->minimumSize());
    const QMargins m = contentsMargins();
    return size + QSize(m.left() + m.right(), m.top() + m.bottom());
}

void FlowLayout::setGeometry(const QRect &rect) {
    QLayout::setGeometry(rect);
    doLayout(rect, false);
}

int FlowLayout::doLayout(const QRect &rect, bool testOnly) const {
    const QMargins m = contentsMargins();
    const QRect r = rect.adjusted(m.left(), m.top(), -m.right(), -m.bottom());
    int x = r.x(), y = r.y(), lineHeight = 0;
    for (QLayoutItem *item : m_items) {
        if (item->widget() && item->widget()->isHidden()) continue;
        const QSize hint = item->sizeHint();
        int nextX = x + hint.width() + m_h;
        if (nextX - m_h > r.right() + 1 && lineHeight > 0) {
            x = r.x();
            y += lineHeight + m_v;
            nextX = x + hint.width() + m_h;
            lineHeight = 0;
        }
        if (!testOnly) item->setGeometry(QRect(QPoint(x, y), hint));
        x = nextX;
        lineHeight = qMax(lineHeight, hint.height());
    }
    return y + lineHeight - rect.y() + m.bottom();
}

// ─── Text helpers ─────────────────────────────────────────────────────────

QString maskSecrets(const QString &text) {
    QString out = text;
    // Provider-style keys: sk-…, sk-ant-…, ghp_…, xai-…, hf_…, AIza…, long hex/base64 runs.
    static const QRegularExpression keyish(QStringLiteral(
        "\\b(?:sk-[A-Za-z0-9_-]{8,}|sk-ant-[A-Za-z0-9_-]{8,}|gh[pousr]_[A-Za-z0-9]{12,}|xai-[A-Za-z0-9]{12,}"
        "|hf_[A-Za-z0-9]{12,}|AIza[0-9A-Za-z_-]{20,}|nous[_-][A-Za-z0-9_-]{16,})\\b"));
    out.replace(keyish, QStringLiteral("••••••••"));
    static const QRegularExpression bearer(QStringLiteral("(?i)(bearer\\s+)[A-Za-z0-9._~+/=-]{8,}"));
    out.replace(bearer, QStringLiteral("\\1••••••••"));
    // KEY: value / KEY=value where the key name says it's a secret.
    // Only values that contain a letter (so "Tokens: 1,204" stays readable).
    static const QRegularExpression kv(QStringLiteral(
        "(?i)(\\b[\\w.-]*(?:api[_-]?key|token|secret|password|passwd)[\\w.-]*\\s*[:=]\\s*)"
        "(?!\\(not set\\)|not set\\b|none\\b|null\\b)((?=[^\\s,;]*[A-Za-z])[^\\s,;]+)"));
    out.replace(kv, QStringLiteral("\\1••••••••"));
    return out;
}

QString relativeTime(const QDateTime &when, const QDateTime &now) {
    if (!when.isValid()) return QString();
    const qint64 secs = when.secsTo(now);
    if (secs < 60) return QObject::tr("Just now");
    if (secs < 3600) return QObject::tr("%1 min ago").arg(secs / 60);
    const QDate d = when.toLocalTime().date();
    const QDate today = now.toLocalTime().date();
    if (d == today) return when.toLocalTime().toString(QStringLiteral("h:mm AP"));
    if (d == today.addDays(-1)) return QObject::tr("Yesterday");
    if (d.daysTo(today) < 7) return d.toString(QStringLiteral("dddd"));
    if (d.year() == today.year()) return d.toString(QStringLiteral("MMM d"));
    return d.toString(QStringLiteral("MMM d, yyyy"));
}

} // namespace ui

namespace ui {

void ElidedCombo::paintEvent(QPaintEvent *) {
    QStylePainter p(this);
    QStyleOptionComboBox opt;
    initStyleOption(&opt);
    p.drawComplexControl(QStyle::CC_ComboBox, opt);
    // The label may use the field and its right padding, but never run under
    // the drop-down arrow.
    const QRect field = style()->subControlRect(QStyle::CC_ComboBox, &opt, QStyle::SC_ComboBoxEditField, this);
    const QRect arrow = style()->subControlRect(QStyle::CC_ComboBox, &opt, QStyle::SC_ComboBoxArrow, this);
    const int iconW = opt.currentIcon.isNull() ? 0 : opt.iconSize.width() + 4;
    const int right = arrow.isValid() && arrow.left() > field.left() ? arrow.left() : field.right();
    const int avail = right - field.left() - iconW - 2;
    const QFontMetrics &fm = opt.fontMetrics;
    if (fm.horizontalAdvance(opt.currentText) > avail)
        opt.currentText = fm.elidedText(opt.currentText, Qt::ElideRight, qMax(0, avail));
    p.drawControl(QStyle::CE_ComboBoxLabel, opt);
}

QString displayChoice(const QString &s) {
    if (s.isEmpty() || s != s.toLower()) return s;
    for (const QChar c : s) if (!c.isLetter() && c != QLatin1Char(' ')) return s;
    return s.at(0).toUpper() + s.mid(1);
}

QString softWrap(const QString &s, int chunk) {
    QString out;
    out.reserve(s.size() + s.size() / qMax(1, chunk) + 4);
    int run = 0;
    for (const QChar c : s) {
        out += c;
        if (c == QLatin1Char('/') || c == QLatin1Char('-') || c == QLatin1Char('_') || c == QLatin1Char('.')) {
            out += QChar(0x200B);
            run = 0;
        } else if (++run >= chunk) {
            out += QChar(0x200B);
            run = 0;
        }
    }
    return out;
}

} // namespace ui
