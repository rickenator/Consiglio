#pragma once
#include <QAbstractListModel>
class EVENTMODEL : public QAbstractListModel {
    Q_OBJECT
public:
    explicit EVENTMODEL(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override { return 0; }
    QVariant data(const QModelIndex &index, int role) const override { return {}; }
};
