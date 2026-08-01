#pragma once
#include <QWidget>
#include <QTreeView>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QFileSystemModel>

class FileBrowser : public QWidget {
    Q_OBJECT
public:
    explicit FileBrowser(QWidget *parent = nullptr);

signals:
    void fileSelected(const QString &path);

public slots:
    void setRootPath(const QString &path);

private:
    void setupUI();

    QTreeView *m_treeView = nullptr;
    QFileSystemModel *m_model = nullptr;
    QLineEdit *m_pathEdit = nullptr;
    QPushButton *m_browseBtn = nullptr;
};
