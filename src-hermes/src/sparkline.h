#pragma once

#include <QColor>
#include <QList>
#include <QWidget>

class QPainterPath;

namespace ui {

// A small trend line drawn from a series of samples.
//
// No axes, no grid, no labels: the shape is the information, and the numbers
// travel in the tooltip so a graph never replaces the values it came from
// (the Design Rule: every visual maps to something inspectable). Values are
// normalised to the series' own min/max unless `fixedRange` is set — for a
// percentage series that means 0…100 so two sessions stay comparable.
class Sparkline : public QWidget {
    Q_OBJECT
public:
    explicit Sparkline(QWidget *parent = nullptr);

    void setValues(const QList<double> &values);
    QList<double> values() const { return m_values; }

    // Draw the filled area under the line (default on).
    void setFill(bool on);
    void setLineWidth(double px);

    // Normalise against a fixed range instead of the series' own extent.
    void setFixedRange(double min, double max);
    void clearFixedRange();

    // Colour follows the theme unless overridden.
    void setColor(const QColor &color);
    void clearColor();

    // Re-read theme colours (after a light/dark switch or a zoom change).
    void restyle();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QPainterPath pathFor(const QRectF &rect, double min, double max) const;

    QList<double> m_values;
    bool m_fill = true;
    double m_lineWidth = 1.5;
    bool m_fixed = false;
    double m_fixedMin = 0.0;
    double m_fixedMax = 100.0;
    QColor m_color;
    bool m_colorSet = false;
};

}  // namespace ui
