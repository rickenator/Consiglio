#pragma once
#include <QDialog>
#include "backend/settings.h"
#include "backend/agentdetector.h"

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(AppSettings *settings, AgentDetector *agentDetector, QWidget *parent = nullptr);
};
