#include "filebrowser.h"
#include <QDir>
#include <QFileDialog>
#include <QHeaderView>

FileBrowser::FileBrowser(QWidget *parent) : QWidget(parent) {
    setupUI();
}

void FileBrowser::setupUI() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);

    // Header
    auto *headerLayout = new QHBoxLayout();
    auto *titleLabel = new QLabel(tr("Files"), this);
    titleLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #58a6ff;");
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();
    mainLayout->addLayout(headerLayout);

    // Path bar
    auto *pathLayout = new QHBoxLayout();
    m_pathEdit = new QLineEdit(this);
    m_pathEdit->setPlaceholderText(tr("/home/user/project"));
    m_pathEdit->setStyleSheet(R"(
        QLineEdit {
            background: #161b22;
            border: 1px solid #30363d;
            border-radius: 4px;
            padding: 8px;
            color: #c9d1d9;
        }
        QLineEdit:focus { border-color: #58a6ff; }
    )");
    pathLayout->addWidget(m_pathEdit);

    m_browseBtn = new QPushButton(tr("Browse"), this);
    m_browseBtn->setStyleSheet(R"(
        QPushButton {
            background: #21262d;
            border: 1px solid #30363d;
            color: #c9d1d9;
            padding: 8px 12px;
            border-radius: 4px;
        }
        QPushButton:hover { background: #30363d; }
    )");
    pathLayout->addWidget(m_browseBtn);
    mainLayout->addLayout(pathLayout);

    // File tree view
    m_model = new QFileSystemModel(this);
    m_model->setRootPath("");
    m_model->setFilter(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
    m_model->setReadOnly(true);

    m_treeView = new QTreeView(this);
    m_treeView->setModel(m_model);
    m_treeView->setRootIndex(m_model->index(QDir::currentPath()));
    m_treeView->setAlternatingRowColors(true);
    m_treeView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_treeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_treeView->setStyleSheet(R"(
        QTreeView {
            background: #0d1117;
            border: 1px solid #30363d;
            border-radius: 6px;
            padding: 4px;
        }
        QTreeView::item {
            padding: 6px;
            border-radius: 4px;
        }
        QTreeView::item:selected {
            background: rgba(88, 166, 255, 0.15);
        }
    )");

    // Hide size and type columns, show only name
    m_treeView->hideColumn(1); // Size
    m_treeView->hideColumn(2); // Type
    m_treeView->hideColumn(3); // Contents

    mainLayout->addWidget(m_treeView);

    // Connections
    connect(m_browseBtn, &QPushButton::clicked, this, [this]() {
        auto dir = QFileDialog::getExistingDirectory(this, tr("Select Directory"), QDir::currentPath());
        if (!dir.isEmpty()) {
            setRootPath(dir);
        }
    });

    connect(m_pathEdit, &QLineEdit::returnPressed, this, [this]() {
        auto path = m_pathEdit->text().trimmed();
        if (!path.isEmpty() && QDir(path).exists()) {
            setRootPath(path);
        }
    });

    connect(m_treeView, &QTreeView::doubleClicked, this, [this](const QModelIndex &index) {
        auto path = m_model->filePath(index);
        emit fileSelected(path);
    });
}

void FileBrowser::setRootPath(const QString &path) {
    if (QDir(path).exists()) {
        m_pathEdit->setText(path);
        m_treeView->setRootIndex(m_model->index(path));
    }
}
