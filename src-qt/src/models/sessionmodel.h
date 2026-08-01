#pragma once
#include <QAbstractListModel>
class SESSIONMODEL : public QAbstractListModel {
    Q_OBJECT
public:
    explicit SESSIONMODEL(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override { return 0; }
    QVariant data(const QModelIndex &index, int role) const override { return {}; }
};
