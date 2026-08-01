#pragma once

#include <QListWidget>
#include <QStyledItemDelegate>

class Sidebar : public QListWidget {
    Q_OBJECT

public:
    enum class PanelId {
        Welcome,
        Sessions,
        Timeline,
        Files,
        Discussions,
        Secrets,
        Mobile
    };

    explicit Sidebar(QWidget *parent = nullptr);

signals:
    void panelChanged(PanelId id);
    void newSessionRequested();
    void settingsRequested();

private slots:
    void onItemClicked(QListWidgetItem *item);

private:
    void setupUI();
    PanelId currentPanelId() const;
public:
    void selectPanel(PanelId id);
};
