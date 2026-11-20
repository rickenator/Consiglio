#pragma once

#include <QLabel>
#include <QLayout>
#include <QString>
#include <QToolButton>
#include <QWidget>

class QAction;
class QKeySequence;

// Small shared UI helpers so every control gets the same treatment: themed
// icon, tooltip that names its shortcut, accessible name.
namespace ui {

// "Label (Ctrl+K)" — the native-text form of a shortcut for tooltips.
QString withShortcut(const QString &text, const QKeySequence &shortcut);

// Set tooltip + accessible name/description in one go.
void describe(QWidget *w, const QString &name, const QString &tooltip = QString());

// Apply tooltip "Text (Shortcut)" to an action, and make menus show it.
void describe(QAction *a, const QString &tooltip = QString());

// A borderless icon-only tool button (objectName decides its QSS look).
QToolButton *iconButton(const QString &icon, const QString &tooltip, QWidget *parent,
                        const QString &objectName = QStringLiteral("blockAction"), int iconPx = 16);

// Size a dialog to its polished contents when it is first shown (word-wrapped
// labels and stylesheet fonts otherwise leave it clipped) and keep it from
// being shrunk below that.
void fitOnShow(QWidget *dialog);

// A label whose text is elided to its width (full text in the tooltip).
class ElidedLabel : public QLabel {
    Q_OBJECT
public:
    explicit ElidedLabel(QWidget *parent = nullptr, Qt::TextElideMode mode = Qt::ElideRight);
    void setFullText(const QString &text);
    QString fullText() const { return m_full; }
    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

protected:
    void resizeEvent(QResizeEvent *e) override;

private:
    void updateElided();
    QString m_full;
    Qt::TextElideMode m_mode;
};

// An indeterminate spinner drawn in the accent colour.
class Spinner : public QWidget {
    Q_OBJECT
public:
    explicit Spinner(QWidget *parent = nullptr, int diameter = 16);
    void setRunning(bool running);
    bool isRunning() const { return m_timer != 0; }
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;
    void timerEvent(QTimerEvent *) override;
    void hideEvent(QHideEvent *) override;
    void showEvent(QShowEvent *) override;

private:
    int m_diameter;
    int m_angle = 0;
    int m_timer = 0;
    bool m_wanted = false;
};

// A dot coloured by connection state ("ok", "busy", "warn", "error", "off").
class StatusDot : public QWidget {
    Q_OBJECT
public:
    explicit StatusDot(QWidget *parent = nullptr);
    void setState(const QString &state);
    QString state() const { return m_state; }
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QString m_state = QStringLiteral("off");
};

// A layout that wraps its items onto new lines (chips, tags).
class FlowLayout : public QLayout {
public:
    explicit FlowLayout(QWidget *parent = nullptr, int hSpacing = 8, int vSpacing = 8);
    ~FlowLayout() override;
    void addItem(QLayoutItem *item) override;
    int count() const override;
    QLayoutItem *itemAt(int index) const override;
    QLayoutItem *takeAt(int index) override;
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;
    QSize minimumSize() const override;
    QSize sizeHint() const override { return minimumSize(); }
    void setGeometry(const QRect &rect) override;

private:
    int doLayout(const QRect &rect, bool testOnly) const;
    QList<QLayoutItem *> m_items;
    int m_h, m_v;
};

// Mask anything that looks like a secret in text shown to the user
// (API keys, bearer tokens, key=value secrets).
QString maskSecrets(const QString &text);

// A friendly relative time ("just now", "5 min ago", "Yesterday", "Mar 3").
QString relativeTime(const QDateTime &when, const QDateTime &now);

} // namespace ui
