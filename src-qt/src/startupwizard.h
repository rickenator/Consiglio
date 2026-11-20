#pragma once
#include <QDialog>
#include "backend/settings.h"
#include "backend/agentdetector.h"

class StartupWizard : public QDialog {
    Q_OBJECT
public:
    explicit StartupWizard(AgentDetector *agentDetector, AppSettings *settings, QWidget *parent = nullptr);
};
