#pragma once
#include <QWidget>
#include "backend/approvalrouter.h"

class ConsiglioCmdApproval : public QWidget {
    Q_OBJECT
public:
    explicit ConsiglioCmdApproval(const ApprovalRequest &request, QWidget *parent = nullptr);

signals:
    void approved();
    void rejected();
};
