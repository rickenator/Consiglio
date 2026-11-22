#include "sparkline.h"

#include "theme.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QtMath>

namespace ui {

Sparkline::Sparkline(QWidget *parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setFocusPolicy(Qt::NoFocus);
}

void Sparkline::setValues(const QList<double> &values) {
    m_values = values;
    update();
}

void Sparkline::setFill(bool on) {
    m_fill = on;
    update();
}

void Sparkline::setLineWidth(double px) {
    m_lineWidth = qMax(0.5, px);
    update();
}

void Sparkline::setFixedRange(double min, double max) {
    m_fixed = true;
    m_fixedMin = min;
    m_fixedMax = max;
    update();
}

void Sparkline::clearFixedRange() {
    m_fixed = false;
    update();
}

void Sparkline::setColor(const QColor &color) {
    m_color = color;
    m_colorSet = true;
    update();
}

void Sparkline::clearColor() {
    m_colorSet = false;
    update();
}

void Sparkline::restyle() { update(); }

QSize Sparkline::sizeHint() const {
    const int px = Theme::instance().px(1);
    return QSize(px * 64, px * 18);
}

QSize Sparkline::minimumSizeHint() const { return QSize(Theme::instance().px(24), Theme::instance().px(12)); }

QPainterPath Sparkline::pathFor(const QRectF &rect, double min, double max) const {
    QPainterPath path;
    const int n = m_values.size();
    if (n == 0) return path;
    const double span = max - min;
    auto xAt = [&](int i) {
        return n == 1 ? rect.right() : rect.left() + (rect.width() * i) / double(n - 1);
    };
    auto yAt = [&](double v) {
        const double t = span > 0 ? (v - min) / span : 0.5;
        return rect.bottom() - qBound(0.0, t, 1.0) * rect.height();
    };
    path.moveTo(xAt(0), yAt(m_values.at(0)));
    for (int i = 1; i < n; ++i) path.lineTo(xAt(i), yAt(m_values.at(i)));
    return path;
}

void Sparkline::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)
    if (m_values.isEmpty() || width() <= 2 || height() <= 2) return;

    Theme &t = Theme::instance();
    const QColor stroke = m_colorSet ? m_color : t.tokens().accent;

    double min = m_fixed ? m_fixedMin : 0.0;
    double max = m_fixed ? m_fixedMax : 0.0;
    if (!m_fixed) {
        min = m_values.first();
        max = m_values.first();
        for (double v : m_values) {
            min = qMin(min, v);
            max = qMax(max, v);
        }
        if (max - min < 1e-9) {
            // A flat series still deserves a line in the middle, not a divide
            // by zero and not a line stuck to the floor.
            const double pad = qMax(0.5, qAbs(max) * 0.05);
            min -= pad;
            max += pad;
        }
    }

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const double inset = m_lineWidth / 2.0 + 0.5;
    const QRectF rect(inset, inset, width() - 2 * inset, height() - 2 * inset);
    if (rect.width() <= 0 || rect.height() <= 0) return;

    const QPainterPath line = pathFor(rect, min, max);

    if (m_fill && m_values.size() >= 2) {
        QPainterPath area = line;
        area.lineTo(rect.right(), rect.bottom());
        area.lineTo(rect.left(), rect.bottom());
        area.closeSubpath();
        QLinearGradient g(0, rect.top(), 0, rect.bottom());
        QColor top = stroke;
        top.setAlphaF(0.28);
        QColor bottom = stroke;
        bottom.setAlphaF(0.02);
        g.setColorAt(0.0, top);
        g.setColorAt(1.0, bottom);
        p.fillPath(area, g);
    }

    QPen pen(stroke, m_lineWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    if (m_values.size() == 1) {
        p.setBrush(stroke);
        p.drawEllipse(QPointF(rect.right() - 1, rect.center().y()), m_lineWidth, m_lineWidth);
    } else {
        p.setBrush(Qt::NoBrush);
        p.drawPath(line);
    }
}

}  // namespace ui
