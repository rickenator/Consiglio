#include "commandpalette.h"

#include "theme.h"
#include "uiutil.h"

#include <QFrame>
#include <QMouseEvent>
#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyledItemDelegate>
#include <QVBoxLayout>
#include <algorithm>

namespace {

enum Roles {
    IdRole = Qt::UserRole + 1,
    HeaderRole,
    DescriptionRole,
    IconRole,
    ShortcutRole,
    DetailRole,
    EnabledRole,
    BadgeRole,
};

class PaletteDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        auto &t = Theme::instance();
        const bool header = index.data(HeaderRole).toBool();
        return QSize(option.rect.width(), t.px(header ? 30 : 40));
    }

    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        auto &t = Theme::instance();
        const auto &tk = t.tokens();
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const QRect r = option.rect;
        QFont base = option.font;
        if (index.data(HeaderRole).toBool()) {
            QFont f = base;
            f.setPixelSize(t.px(11));
            f.setWeight(QFont::DemiBold);
            f.setLetterSpacing(QFont::AbsoluteSpacing, 0.5);
            p->setFont(f);
            p->setPen(tk.textFaint);
            p->drawText(r.adjusted(t.px(10), t.px(8), -t.px(10), 0), Qt::AlignLeft | Qt::AlignVCenter,
                        index.data(Qt::DisplayRole).toString().toUpper());
            p->restore();
            return;
        }
        const bool selected = option.state & QStyle::State_Selected;
        const bool enabled = index.data(EnabledRole).toBool();
        if (selected) {
            QPainterPath path;
            path.addRoundedRect(QRectF(r).adjusted(1, 1, -1, -1), t.px(8), t.px(8));
            p->fillPath(path, tk.accentSoft);
        }
        const int pad = t.px(10);
        const int iconSize = t.px(17);
        const QString icon = index.data(IconRole).toString();
        int x = r.left() + pad;
        if (!icon.isEmpty()) {
            const QPixmap pm = t.pixmap(icon, iconSize, selected ? tk.accent : (enabled ? tk.textMuted : tk.textFaint));
            p->drawPixmap(x, r.top() + (r.height() - iconSize) / 2, pm);
        }
        x += iconSize + t.px(12);

        // Right side: badge ("Dialog"/"Chat"), then shortcut keycap, else detail ("/model").
        int right = r.right() - pad;
        const QString shortcut = index.data(ShortcutRole).toString();
        const QString detail = index.data(DetailRole).toString();
        const QString badge = index.data(BadgeRole).toString();
        QFont small = base;
        small.setPixelSize(t.px(11));
        if (!shortcut.isEmpty()) {
            p->setFont(small);
            const QFontMetrics sm(small);
            const int w = sm.horizontalAdvance(shortcut) + t.px(12);
            const int h = t.px(20);
            QRectF key(right - w, r.top() + (r.height() - h) / 2.0, w, h);
            p->setPen(QPen(tk.borderStrong, 1));
            p->setBrush(tk.surface);
            p->drawRoundedRect(key, t.px(5), t.px(5));
            p->setPen(tk.textMuted);
            p->drawText(key, Qt::AlignCenter, shortcut);
            right -= w + t.px(10);
        } else if (!detail.isEmpty()) {
            QFont mono = small;
            mono.setFamily(QStringLiteral("monospace"));
            mono.setStyleHint(QFont::Monospace);
            p->setFont(mono);
            const QFontMetrics mm(mono);
            const QString d = mm.elidedText(detail, Qt::ElideRight, t.px(160));
            const int w = mm.horizontalAdvance(d);
            p->setPen(tk.textFaint);
            p->drawText(QRect(right - w, r.top(), w, r.height()), Qt::AlignVCenter | Qt::AlignRight, d);
            right -= w + t.px(12);
        }
        if (!badge.isEmpty()) {
            QFont bf = small;
            bf.setPixelSize(t.px(10));
            bf.setWeight(QFont::DemiBold);
            p->setFont(bf);
            const QFontMetrics bm(bf);
            const int bw = bm.horizontalAdvance(badge) + t.px(12);
            const int bh = t.px(18);
            QRectF pill(right - bw, r.top() + (r.height() - bh) / 2.0, bw, bh);
            const bool dialog = badge.compare(QLatin1String("Dialog"), Qt::CaseInsensitive) == 0;
            p->setPen(Qt::NoPen);
            p->setBrush(dialog ? tk.accentSoft : tk.hover);
            p->drawRoundedRect(pill, t.px(7), t.px(7));
            p->setPen(dialog ? tk.accent : tk.textMuted);
            p->drawText(pill, Qt::AlignCenter, badge);
            right -= bw + t.px(8);
        }

        // Label, then the description in a fainter colour.
        QFont labelFont = base;
        labelFont.setPixelSize(t.px(13));
        labelFont.setWeight(QFont::Medium);
        p->setFont(labelFont);
        const QFontMetrics lm(labelFont);
        const int avail = qMax(0, right - x);
        const QString label = lm.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, avail);
        p->setPen(enabled ? tk.text : tk.textFaint);
        p->drawText(QRect(x, r.top(), avail, r.height()), Qt::AlignVCenter | Qt::AlignLeft, label);
        const int used = lm.horizontalAdvance(label) + t.px(10);
        const QString desc = index.data(DescriptionRole).toString();
        if (!desc.isEmpty() && avail - used > t.px(40)) {
            QFont df = base;
            df.setPixelSize(t.px(12));
            p->setFont(df);
            const QFontMetrics dm(df);
            p->setPen(tk.textFaint);
            p->drawText(QRect(x + used, r.top(), avail - used, r.height()), Qt::AlignVCenter | Qt::AlignLeft,
                        dm.elidedText(desc, Qt::ElideRight, avail - used));
        }
        p->restore();
    }
};

int score(const PaletteAction &a, const QStringList &terms) {
    if (terms.isEmpty()) return 1;
    int total = 0;
    const QString label = a.label.toLower();
    const QString id = a.id.toLower();
    const QString kw = a.keywords.toLower();
    const QString desc = a.description.toLower();
    const QString detail = a.detail.toLower();
    for (const QString &term : terms) {
        int s = 0;
        if (label.startsWith(term)) s = 100;
        else if (detail.startsWith(term) || detail.startsWith('/' + term) || id.startsWith('/' + term)) s = 95;
        else if (label.contains(QLatin1Char(' ') + term)) s = 80;
        else if (label.contains(term)) s = 60;
        else if (kw.contains(term) || detail.contains(term)) s = 50;
        else if (a.group.toLower().contains(term)) s = 30;
        else if (desc.contains(term)) s = 20;
        if (s == 0) return 0;  // every term must match somewhere
        total += s;
    }
    return total;
}

} // namespace

CommandPalette::CommandPalette(QWidget *parent) : QWidget(parent) {
    setObjectName("overlayDim");
    setAttribute(Qt::WA_StyledBackground);
    setupUI();
    // The palette is a child of the main window but must NOT be visible until
    // open() is called — otherwise it parks itself at (0,0) as a stray rectangle.
    hide();
    if (parent) parent->installEventFilter(this);
    connect(&Theme::instance(), &Theme::changed, this, [this]() {
        m_searchIcon->setPixmap(Theme::instance().pixmap("search", Theme::instance().px(18)));
        m_list->viewport()->update();
    });
}

void CommandPalette::setupUI() {
    auto &t = Theme::instance();
    m_sheet = new QFrame(this);
    m_sheet->setObjectName("paletteSheet");
    m_sheet->setAccessibleName(tr("Command palette"));
    auto *sl = new QVBoxLayout(m_sheet);
    sl->setContentsMargins(t.px(10), t.px(4), t.px(10), 0);
    sl->setSpacing(0);

    auto *searchRow = new QHBoxLayout();
    searchRow->setContentsMargins(t.px(8), 0, t.px(4), 0);
    searchRow->setSpacing(t.px(6));
    m_searchIcon = new QLabel(m_sheet);
    m_searchIcon->setPixmap(t.pixmap("search", t.px(18)));
    searchRow->addWidget(m_searchIcon);
    m_filter = new QLineEdit(m_sheet);
    m_filter->setObjectName("paletteSearch");
    m_filter->setPlaceholderText(tr("Search commands, settings and Hermes slash commands…"));
    m_filter->setAccessibleName(tr("Search commands"));
    m_filter->installEventFilter(this);
    searchRow->addWidget(m_filter, 1);
    sl->addLayout(searchRow);

    m_model = new QStandardItemModel(this);
    m_list = new QListView(m_sheet);
    m_list->setObjectName("paletteList");
    m_list->setModel(m_model);
    m_list->setItemDelegate(new PaletteDelegate(m_list));
    m_list->setUniformItemSizes(false);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setFocusPolicy(Qt::NoFocus);
    m_list->setMouseTracking(true);
    m_list->setAccessibleName(tr("Commands"));
    sl->addWidget(m_list, 1);

    auto *footerBar = new QWidget(m_sheet);
    footerBar->setObjectName("paletteFooterBar");
    auto *fl = new QHBoxLayout(footerBar);
    fl->setContentsMargins(t.px(8), t.px(8), t.px(8), t.px(9));
    m_footer = new QLabel(tr("↑↓ navigate   ↵ run   Esc close    Dialog = UI    Chat = / command"), footerBar);
    m_footer->setObjectName("paletteFooter");
    m_count = new QLabel(footerBar);
    m_count->setObjectName("paletteFooter");
    fl->addWidget(m_footer);
    fl->addStretch();
    fl->addWidget(m_count);
    sl->addWidget(footerBar);

    connect(m_filter, &QLineEdit::textChanged, this, [this]() { rebuild(); });
    connect(m_list, &QListView::clicked, this, [this](const QModelIndex &idx) {
        if (idx.data(HeaderRole).toBool() || !idx.data(EnabledRole).toBool()) return;
        m_list->setCurrentIndex(idx);
        triggerCurrent();
    });
    connect(m_list, &QListView::entered, this, [this](const QModelIndex &idx) {
        if (!idx.data(HeaderRole).toBool() && idx.data(EnabledRole).toBool()) m_list->setCurrentIndex(idx);
    });
}

void CommandPalette::setActions(const QList<PaletteAction> &actions) {
    m_actions = actions;
    if (isVisible()) rebuild();
}

void CommandPalette::rebuild() {
    QString query = m_filter->text().trimmed();
    if (query.startsWith('/')) query = query.mid(1);
    const QStringList terms = query.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);

    m_model->clear();
    int shown = 0;
    if (terms.isEmpty()) {
        // Grouped, in the order given.
        QString lastGroup;
        for (const auto &a : qAsConst(m_actions)) {
            if (a.group != lastGroup) {
                auto *h = new QStandardItem(a.group);
                h->setData(true, HeaderRole);
                h->setFlags(Qt::NoItemFlags);
                m_model->appendRow(h);
                lastGroup = a.group;
            }
            auto *it = new QStandardItem(a.label);
            it->setData(a.id, IdRole);
            it->setData(a.description, DescriptionRole);
            it->setData(a.icon, IconRole);
            it->setData(a.shortcut, ShortcutRole);
            it->setData(a.detail, DetailRole);
            it->setData(a.badge, BadgeRole);
            it->setData(a.enabled, EnabledRole);
            it->setData(a.description.isEmpty() ? a.label : a.label + ". " + a.description, Qt::AccessibleTextRole);
            it->setFlags(a.enabled ? (Qt::ItemIsEnabled | Qt::ItemIsSelectable) : Qt::ItemIsEnabled);
            m_model->appendRow(it);
            ++shown;
        }
    } else {
        QList<QPair<int, int>> ranked;  // score, index
        for (int i = 0; i < m_actions.size(); ++i) {
            const int s = score(m_actions[i], terms);
            if (s > 0) ranked.append({s, i});
        }
        std::stable_sort(ranked.begin(), ranked.end(),
                         [](const QPair<int, int> &a, const QPair<int, int> &b) { return a.first > b.first; });
        for (const auto &pair : ranked) {
            const auto &a = m_actions[pair.second];
            auto *it = new QStandardItem(a.label);
            it->setData(a.id, IdRole);
            it->setData(a.description, DescriptionRole);
            it->setData(a.icon, IconRole);
            it->setData(a.shortcut, ShortcutRole);
            it->setData(a.detail.isEmpty() ? a.group : a.detail, DetailRole);
            it->setData(a.badge, BadgeRole);
            it->setData(a.enabled, EnabledRole);
            it->setData(a.description.isEmpty() ? a.label : a.label + ". " + a.description, Qt::AccessibleTextRole);
            it->setFlags(a.enabled ? (Qt::ItemIsEnabled | Qt::ItemIsSelectable) : Qt::ItemIsEnabled);
            m_model->appendRow(it);
            ++shown;
        }
        if (ranked.isEmpty()) {
            auto *h = new QStandardItem(tr("No matching commands"));
            h->setData(true, HeaderRole);
            h->setFlags(Qt::NoItemFlags);
            m_model->appendRow(h);
        }
    }
    m_count->setText(terms.isEmpty() ? (shown == 1 ? tr("1 command") : tr("%1 commands").arg(shown))
                                  : shown == 0 ? tr("No matches")
                                  : (shown == 1 ? tr("1 match") : tr("%1 matches").arg(shown)));
    selectFirst();
}

void CommandPalette::selectFirst() {
    for (int row = 0; row < m_model->rowCount(); ++row) {
        const QModelIndex idx = m_model->index(row, 0);
        if (!idx.data(HeaderRole).toBool() && idx.data(EnabledRole).toBool()) {
            m_list->setCurrentIndex(idx);
            m_list->scrollTo(idx);
            if (row <= 1) m_list->scrollToTop();
            return;
        }
    }
}

void CommandPalette::moveSelection(int delta) {
    const int rows = m_model->rowCount();
    if (rows == 0) return;
    int row = m_list->currentIndex().isValid() ? m_list->currentIndex().row() : -1;
    const int step = delta > 0 ? 1 : -1;
    int remaining = qAbs(delta);
    int candidate = row;
    while (remaining > 0) {
        int next = candidate + step;
        while (next >= 0 && next < rows
               && (m_model->index(next, 0).data(HeaderRole).toBool()
                   || !m_model->index(next, 0).data(EnabledRole).toBool())) {
            next += step;
        }
        if (next < 0 || next >= rows) break;
        candidate = next;
        --remaining;
    }
    if (candidate >= 0 && candidate != row) {
        const QModelIndex idx = m_model->index(candidate, 0);
        m_list->setCurrentIndex(idx);
        m_list->scrollTo(idx);
        // Keep the group header of the first item visible.
        if (candidate > 0 && m_model->index(candidate - 1, 0).data(HeaderRole).toBool()) {
            m_list->scrollTo(m_model->index(candidate - 1, 0));
        }
    }
}

QStringList CommandPalette::visibleIds() const {
    QStringList ids;
    for (int row = 0; row < m_model->rowCount(); ++row) {
        const QModelIndex idx = m_model->index(row, 0);
        if (!idx.data(HeaderRole).toBool()) ids << idx.data(IdRole).toString();
    }
    return ids;
}

void CommandPalette::setQuery(const QString &query) { m_filter->setText(query); }

void CommandPalette::triggerCurrent() {
    const QModelIndex idx = m_list->currentIndex();
    if (!idx.isValid() || idx.data(HeaderRole).toBool() || !idx.data(EnabledRole).toBool()) return;
    const QString id = idx.data(IdRole).toString();
    closeOverlay();
    emit actionSelected(id);
}

void CommandPalette::open(const QString &query) {
    if (!parentWidget()) return;
    setGeometry(parentWidget()->rect());
    m_filter->setText(query);
    rebuild();
    layoutSheet();
    show();
    raise();
    m_filter->setFocus();
    m_filter->end(false);
}

void CommandPalette::closeOverlay() {
    hide();
    if (parentWidget()) parentWidget()->setFocus();
}

void CommandPalette::layoutSheet() {
    auto &t = Theme::instance();
    const int w = qMin(t.px(660), width() - t.px(48));
    const int top = qMax(t.px(64), height() / 9);
    const int h = qMin(t.px(500), height() - top - t.px(32));
    m_sheet->setGeometry((width() - w) / 2, top, w, qMax(t.px(200), h));
}

void CommandPalette::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    layoutSheet();
}

void CommandPalette::mousePressEvent(QMouseEvent *event) {
    // Click on the dimmed area closes.
    if (!m_sheet->geometry().contains(event->pos())) closeOverlay();
    else QWidget::mousePressEvent(event);
}

void CommandPalette::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        closeOverlay();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool CommandPalette::eventFilter(QObject *obj, QEvent *event) {
    if (obj == parentWidget() && event->type() == QEvent::Resize && isVisible()) {
        setGeometry(parentWidget()->rect());
    }
    if (obj == m_filter && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        switch (key->key()) {
            case Qt::Key_Escape:
                if (!m_filter->text().isEmpty()) m_filter->clear();
                else closeOverlay();
                return true;
            case Qt::Key_Down: moveSelection(1); return true;
            case Qt::Key_Up: moveSelection(-1); return true;
            case Qt::Key_PageDown: moveSelection(8); return true;
            case Qt::Key_PageUp: moveSelection(-8); return true;
            case Qt::Key_Return:
            case Qt::Key_Enter: triggerCurrent(); return true;
            case Qt::Key_Tab: moveSelection(key->modifiers() & Qt::ShiftModifier ? -1 : 1); return true;
            case Qt::Key_Backtab: moveSelection(-1); return true;
            default: break;
        }
    }
    return QWidget::eventFilter(obj, event);
}
