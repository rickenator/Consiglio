#pragma once

#include <QDialog>
#include "fontsettings.h"

// A small modal dialog with one size control per balloon type.
// Changes apply live (as you type) and persist via FontSettings.
class FontSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit FontSettingsDialog(FontSettings *fonts, QWidget *parent = nullptr);

private:
    FontSettings *m_fonts;
};
