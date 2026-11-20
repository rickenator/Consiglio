#include "sessionrail.h"

#include "theme.h"
#include "uiutil.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

namespace {

enum { IdRole = Qt::UserRole, CwdRole, UpdatedRole, TitleRole };

// Two-line session row: title, then "5 min ago · ~/project".
class SessionDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const override {
        return QSize(option.rect.width(), Theme::instance().px(50));
    }
    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        auto &t = Theme::instance();
        const auto &tk = t.tokens();
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const QRect r = option.rect.adjusted(0, 1, 0, -1);
        const bool selected = option.state & QStyle::State_Selected;
        const bool hover = option.state & QStyle::State_MouseOver;
        const bool current = index.data(Qt::UserRole + 10).toBool();
        if (selected || current || hover) {
            p->setPen(Qt::NoPen);
            p->setBrush(selected || current ? tk.accentSoft : tk.hover);
            p->drawRoundedRect(r, t.px(8), t.px(8));
        }
        const int pad = t.px(10);
        QFont f = option.font;
        f.setPixelSize(t.px(13));
        f.setWeight(current ? QFont::DemiBold : QFont::Medium);
        p->setFont(f);
        p->setPen(tk.text);
        const QFontMetrics fm(f);
        const QRect textRect = r.adjusted(pad, t.px(7), -pad, 0);
        p->drawText(textRect, Qt::AlignLeft | Qt::AlignTop,
                    fm.elidedText(index.data(TitleRole).toString(), Qt::ElideRight, textRect.width()));
        QFont s = option.font;
        s.setPixelSize(t.px(11));
        p->setFont(s);
        p->setPen(tk.textFaint);
        const QFontMetrics sm(s);
        const QDateTime when = QDateTime::fromString(index.data(UpdatedRole).toString(), Qt::ISODate);
        QString sub = ui::relativeTime(when, QDateTime::currentDateTimeUtc());
        QString cwd = index.data(CwdRole).toString();
        if (cwd.startsWith(QDir::homePath())) cwd = QStringLiteral("~") + cwd.mid(QDir::homePath().size());
        if (!cwd.isEmpty()) sub = sub.isEmpty() ? cwd : sub + QStringLiteral("  ·  ") + cwd;
        const QRect subRect = r.adjusted(pad, t.px(27), -pad, 0);
        p->drawText(subRect, Qt::AlignLeft | Qt::AlignTop, sm.elidedText(sub, Qt::ElideMiddle, subRect.width()));
        p->restore();
    }
};

QString prettyPath(const QString &path) {
    if (path.startsWith(QDir::homePath())) return QStringLiteral("~") + path.mid(QDir::homePath().size());
    return path;
}

} // namespace

SessionRail::SessionRail(QWidget *parent) : QWidget(parent) {
    auto &t = Theme::instance();
    setObjectName("sessionRail");
    setAttribute(Qt::WA_StyledBackground);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(t.px(12), t.px(14), t.px(12), t.px(12));
    layout->setSpacing(t.px(8));

    auto *wsLabel = new QLabel(tr("WORKSPACE"), this);
    wsLabel->setProperty("role", "section");
    layout->addWidget(wsLabel);
    m_workspaceBtn = new QPushButton(this);
    m_workspaceBtn->setObjectName("railWorkspace");
    m_workspaceBtn->setCursor(Qt::PointingHandCursor);
    m_workspaceBtn->setIconSize(QSize(t.px(16), t.px(16)));
    layout->addWidget(m_workspaceBtn);

    m_newBtn = new QPushButton(tr("New Session"), this);
    m_newBtn->setProperty("role", "primary");
    m_newBtn->setCursor(Qt::PointingHandCursor);
    m_newBtn->setIconSize(QSize(t.px(16), t.px(16)));
    ui::describe(m_newBtn, tr("New Session"),
                 ui::withShortcut(tr("Start a new Hermes session in this workspace"), QKeySequence::New));
    layout->addWidget(m_newBtn);

    // Live session card.
    m_activeCard = new QFrame(this);
    m_activeCard->setObjectName("railActive");
    auto *al = new QHBoxLayout(m_activeCard);
    al->setContentsMargins(t.px(10), t.px(9), t.px(6), t.px(9));
    al->setSpacing(t.px(8));
    m_activeDot = new ui::StatusDot(m_activeCard);
    m_activeDot->setState("ok");
    al->addWidget(m_activeDot, 0, Qt::AlignTop | Qt::AlignHCenter);
    auto *atext = new QVBoxLayout();
    atext->setSpacing(t.px(1));
    m_activeTitle = new ui::ElidedLabel(m_activeCard);
    m_activeTitle->setObjectName("railActiveTitle");
    m_activeCwd = new ui::ElidedLabel(m_activeCard, Qt::ElideMiddle);
    m_activeCwd->setProperty("role", "faint");
    atext->addWidget(m_activeTitle);
    atext->addWidget(m_activeCwd);
    al->addLayout(atext, 1);
    m_stopBtn = ui::iconButton("square", ui::withShortcut(tr("End session"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_W)),
                               m_activeCard, "blockAction", 14);
    al->addWidget(m_stopBtn, 0, Qt::AlignVCenter);
    m_activeCard->setVisible(false);
    layout->addWidget(m_activeCard);

    layout->addSpacing(t.px(6));
    auto *recent = new QLabel(tr("RECENT SESSIONS"), this);
    recent->setProperty("role", "section");
    layout->addWidget(recent);
    m_filter = new QLineEdit(this);
    m_filter->setObjectName("railFilter");
    m_filter->setPlaceholderText(tr("Filter sessions"));
    m_filter->setClearButtonEnabled(true);
    ui::describe(m_filter, tr("Filter sessions"), tr("Type to filter previous sessions by title"));
    layout->addWidget(m_filter);

    m_sessionsList = new QListWidget(this);
    m_sessionsList->setObjectName("railSessions");
    m_sessionsList->setItemDelegate(new SessionDelegate(m_sessionsList));
    m_sessionsList->setMouseTracking(true);
    m_sessionsList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_sessionsList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_sessionsList->setAccessibleName(tr("Previous sessions"));
    m_sessionsList->setToolTip(tr("Click a session to resume it"));
    layout->addWidget(m_sessionsList, 1);
    m_emptyLabel = new QLabel(this);
    m_emptyLabel->setProperty("role", "faint");
    m_emptyLabel->setWordWrap(true);
    m_emptyLabel->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    m_emptyLabel->setContentsMargins(t.px(8), t.px(12), t.px(8), 0);
    layout->addWidget(m_emptyLabel, 1);
    m_emptyMessage = tr("No previous sessions yet.");

    connect(m_workspaceBtn, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Choose Workspace"), m_workspace,
                                                              QFileDialog::ShowDirsOnly);
        if (!dir.isEmpty()) {
            setWorkspace(dir);
            emit dirChanged(m_workspace);
        }
    });
    connect(m_newBtn, &QPushButton::clicked, this, [this]() {
        if (QDir(m_workspace).exists()) emit startRequested(m_workspace);
    });
    connect(m_stopBtn, &QToolButton::clicked, this, &SessionRail::stopRequested);
    connect(m_filter, &QLineEdit::textChanged, this, &SessionRail::applyFilter);
    // Clicking (or Enter on) a previous session resumes it. Some styles fire
    // both clicked and activated for one click; emit once.
    auto resume = [this](QListWidgetItem *item) {
        if (!m_connected || !item) return;
        static QElapsedTimer last;
        static QString lastId;
        const QString id = item->data(IdRole).toString();
        if (id == lastId && last.isValid() && last.elapsed() < 600) return;
        lastId = id;
        last.restart();
        emit resumeRequested(id, item->data(CwdRole).toString());
    };
    connect(m_sessionsList, &QListWidget::itemActivated, this, resume);
    connect(m_sessionsList, &QListWidget::itemClicked, this, resume);
    connect(&Theme::instance(), &Theme::changed, this, &SessionRail::restyle);

    setWorkspace(QDir::homePath());
    setConnected(false);
    applyFilter();
    restyle();
}

void SessionRail::restyle() {
    auto &t = Theme::instance();
    m_workspaceBtn->setIcon(t.icon("folder"));
    m_newBtn->setIcon(t.icon("plus", t.tokens().accentText));
    m_stopBtn->setIcon(t.icon("square"));
    m_sessionsList->viewport()->update();
}

void SessionRail::setWorkspace(const QString &dir) {
    m_workspace = QDir::cleanPath(dir);
    const QString pretty = prettyPath(m_workspace);
    const QFontMetrics fm(m_workspaceBtn->font());
    m_workspaceBtn->setText(fm.elidedText(pretty, Qt::ElideMiddle, Theme::instance().px(200)));
    ui::describe(m_workspaceBtn, tr("Workspace: %1").arg(pretty),
                 tr("%1\nClick to choose the folder new sessions start in").arg(m_workspace));
}

void SessionRail::setConnected(bool connected) {
    m_connected = connected;
    m_newBtn->setEnabled(connected);
    m_sessionsList->setEnabled(connected);
}

void SessionRail::setEmptyMessage(const QString &message) {
    m_emptyMessage = message;
    applyFilter();
}

int SessionRail::sessionCount() const { return m_sessionsList->count(); }

void SessionRail::setSessionActive(bool active, const QString &title, const QString &cwd, const QString &sessionId) {
    m_activeCard->setVisible(active);
    m_activeId = active ? sessionId : QString();
    if (active) {
        m_activeTitle->setFullText(title.isEmpty() ? tr("New session") : title);
        m_activeCwd->setFullText(prettyPath(cwd));
        m_activeCard->setAccessibleName(tr("Active session: %1").arg(title.isEmpty() ? tr("New session") : title));
    }
    for (int i = 0; i < m_sessionsList->count(); ++i) {
        auto *item = m_sessionsList->item(i);
        item->setData(Qt::UserRole + 10, !m_activeId.isEmpty() && item->data(IdRole).toString() == m_activeId);
    }
}

void SessionRail::setSessions(const QList<AcpClient::SessionInfo> &sessions) {
    m_sessionsList->blockSignals(true);
    m_sessionsList->clear();
    for (const auto &s : sessions) {
        const QString title = s.title.isEmpty() ? tr("Untitled session") : s.title;
        auto *item = new QListWidgetItem(title);
        item->setData(IdRole, s.id);
        item->setData(CwdRole, s.cwd);
        item->setData(UpdatedRole, s.updatedAt);
        item->setData(TitleRole, title);
        item->setData(Qt::UserRole + 10, !m_activeId.isEmpty() && s.id == m_activeId);
        item->setToolTip(tr("%1\n%2\nClick to resume").arg(title, s.cwd));
        item->setData(Qt::AccessibleTextRole, tr("Resume %1").arg(title));
        m_sessionsList->addItem(item);
    }
    m_sessionsList->blockSignals(false);
    applyFilter();
}

void SessionRail::applyFilter() {
    const QString q = m_filter->text().trimmed();
    int visible = 0;
    for (int i = 0; i < m_sessionsList->count(); ++i) {
        auto *item = m_sessionsList->item(i);
        const bool show = q.isEmpty() || item->data(TitleRole).toString().contains(q, Qt::CaseInsensitive)
                          || item->data(CwdRole).toString().contains(q, Qt::CaseInsensitive);
        item->setHidden(!show);
        visible += show;
    }
    const bool empty = visible == 0;
    m_sessionsList->setVisible(!empty);
    m_emptyLabel->setVisible(empty);
    m_emptyLabel->setText(m_sessionsList->count() > 0 ? tr("No sessions match “%1”.").arg(q) : m_emptyMessage);
    m_filter->setVisible(m_sessionsList->count() > 4 || !q.isEmpty());
}
