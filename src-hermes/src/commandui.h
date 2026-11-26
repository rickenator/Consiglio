#pragma once

#include "acpclient.h"
#include "commandcatalog.h"

#include <QDialog>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

class QCheckBox;
class QDialogButtonBox;
class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QPlainTextEdit;
class QComboBox;
class QVBoxLayout;
class CommandHost;

// How a discovered Hermes command is presented in Consiglio. Discovery itself
// stays dynamic (ACP + /commands); this only decides the surface. Unknown or
// future names fall through to the Command console (passthrough).
enum class CommandArchetype {
    Picker,
    Form,
    Info,
    Confirm,
    TextPrompt,
    LivePanel,
    AppLink,
    Passthrough
};

enum class CommandApply {
    ConfigOption,   // session/set_config_option
    QuietCommand,   // hidden /command; output stays in the dialog
    NewSession,     // ACP session/new
    OpenSettings,   // existing Settings page
    OpenPalette,    // command palette, optional prefill
    CheckUpdates,   // existing updater
    FocusSessions,  // session rail
    StopTurn,
    QuitApp,
    OpenSetup,      // provider / login setup
    Passthrough     // send the line to chat
};

struct CommandPlan {
    QString name;                 // canonical name, no slash
    QStringList aliases;
    CommandArchetype archetype = CommandArchetype::Passthrough;
    CommandApply apply = CommandApply::Passthrough;
    QString title;                // dialog title; empty = catalog label
    QString configId;             // ACP config option id when known
    QString configCategory;       // fallback lookup (e.g. "model")
    QString settingsPage;         // for OpenSettings
    QString confirmText;          // default body for Confirm
    bool dangerous = false;
};

struct CommandPickerItem {
    QString value;
    QString name;
    QString description;
    QString group;
};

inline bool operator==(const CommandPickerItem &a, const CommandPickerItem &b) {
    return a.value == b.value && a.name == b.name && a.description == b.description && a.group == b.group;
}
inline bool operator!=(const CommandPickerItem &a, const CommandPickerItem &b) { return !(a == b); }

struct CommandApplyRequest {
    CommandApply method = CommandApply::Passthrough;
    QString configId;
    QString value;
    QString quietLine;
    QString persistLine;          // optional follow-up (e.g. /model id --global)
    QStringList quietLines;       // when applying more than one quiet command (e.g. queue sync)
    bool keepOpen = false;        // live panels: apply, then refresh instead of closing
    bool reseedAfter = true;      // when keepOpen, ask Hermes for a fresh list
    QString settingsPage;
    QString paletteQuery;
};

class CommandUi {
public:
    static CommandPlan planFor(const QString &name);
    static bool hasNativeUi(const QString &name);
    static bool isAppLink(const QString &name);
    // Palette badge: "Dialog" (native or existing Consiglio surface) or "Chat".
    static QString badgeFor(const QString &name);
    static QStringList nativeCommandNames();
    // Best-effort parse of a quiet /command report into picker rows
    // (personalities, approval modes, …). Empty when nothing looks like a list.
    static QList<CommandPickerItem> itemsFromReport(const QString &text);
    static QString currentFromReport(const QString &text, const QStringList &known = QStringList());
    // Quiet commands used to fill a dialog (status/list). Empty → no seed.
    static QStringList seedCommands(const CommandPlan &plan);
    // Numbered or id-tagged rows from a quiet /command report (queue, memory, …).
    static QList<CommandPickerItem> itemsFromLiveReport(const QString &commandName, const QString &text);
    static QString statusFromReport(const QString &text);
};

// Shared dialog shell for the native archetypes. Preview-on-select, apply
// only on Save; Cancel discards. MainWindow performs the actual apply after
// applyRequested() so tests can inspect the request without talking to ACP.
class CommandHostDialog : public QDialog {
    Q_OBJECT

public:
    CommandHostDialog(const CommandPlan &plan, const CommandCatalog::Command &command,
                      QWidget *parent = nullptr);
    ~CommandHostDialog() override;

    const CommandPlan &plan() const { return m_plan; }
    const CommandCatalog::Command &command() const { return m_command; }
    // The command-specific host this dialog is showing (never null for a
    // dialog: the factory falls back to the archetype's generic host).
    CommandHost *host() const { return m_host; }

    // ── Rendering primitives ─────────────────────────────────────────────
    // Used by CommandHost implementations. Each adds its widgets to the body
    // container; the chrome and the apply lifecycle stay in this class.
    QWidget *bodyWidget() const;
    QVBoxLayout *bodyLayout() const;
    void buildPickerBody();
    void buildInfoBody();
    void buildConfirmBody();
    void buildTextPromptBody();
    void buildUndoForm();
    void buildCompressForm();
    void buildBranchForm();
    void buildSaveForm();
    void buildToolsForm();
    void buildPersonalityExtras();
    // Re-render the picker's preview panel from the current selection.
    void refreshPickerPreview();

    // Seed pickers from ACP session config and any extra model ids.
    void setConfigOptions(const QList<AcpClient::ConfigOption> &options,
                          const QStringList &availableModels = QStringList());
    // Seed a picker from an explicit list (approvals, personality, …).
    void setPickerItems(const QList<CommandPickerItem> &items, const QString &currentValue);
    void setSessionTitle(const QString &title);
    void setBodyText(const QString &text);
    QString bodyText() const;
    void setBusy(bool busy);
    bool isBusy() const { return m_busy; }

    // Preview / selection.
    QString selectedValue() const;
    void setSelectedValue(const QString &value);
    // The value the session currently uses (what the picker opened with).
    QString currentPickerValue() const { return m_currentValue; }
    QString previewHeadline() const;
    QString previewBody() const;
    bool isDirty() const;
    bool persistAsDefault() const;
    void setPersistAsDefault(bool on);

    // Form accessors used by tests and applyRequest().
    int undoCount() const;
    void setUndoCount(int n);
    QString textArgument() const;
    void setTextArgument(const QString &text);
    bool compressPreviewOnly() const;
    void setCompressPreviewOnly(bool on);
    bool branchStayHere() const;
    void setBranchStayHere(bool on);
    QString saveFormat() const;
    void setSaveFormat(const QString &format);
    QString saveFilename() const;
    void setSaveFilename(const QString &name);
    bool saveRedact() const;
    QString toolsAction() const;
    void setToolsAction(const QString &action);
    QString toolsNames() const;
    void setToolsNames(const QString &names);

    CommandApplyRequest applyRequest() const;
    QString commandLine() const;          // quiet / passthrough line for the current form
    bool canApply() const;

    QPushButton *saveButton() const { return m_save; }
    QPushButton *cancelButton() const { return m_cancel; }
    QString errorText() const;
    void setError(const QString &message);
    void clearError();

    // After applyRequested: success closes the dialog; failure re-enables Save.
    void beginApply();
    void endApply(bool ok, const QString &error = QString());
    void releaseApply();
    bool isApplying() const { return m_applying; }

    // Test helpers.
    QListWidget *pickerList() const { return m_list; }
    QLineEdit *filterEdit() const { return m_filter; }
    int pickerCount() const;

    // Chrome hooks used by command-specific hosts.
    QPushButton *addChromeButton(const QString &label);
    void setSaveText(const QString &text);
    void setSaveVisible(bool visible);
    void submitApply(const CommandApplyRequest &request);
    void refreshSaveEnabled() { updateSaveEnabled(); }

    // Live-panel test / apply helpers (forwarded to the LivePanelHost when present).
    QString liveAction() const;
    void setLiveAction(const QString &action);
    QList<CommandPickerItem> liveItems() const;
    void setLiveItems(const QList<CommandPickerItem> &items, bool markClean = false);
    void addLiveItem(const QString &text);
    void removeSelectedLiveItem();
    void moveSelectedLiveItem(int delta);
    QString intervalText() const;
    void setIntervalText(const QString &text);
    int insightDays() const;
    void setInsightDays(int days);
    QString diffMode() const;
    void setDiffMode(const QString &mode);
    bool optionAll() const;
    void setOptionAll(bool on);
    bool optionStat() const;
    void setOptionStat(bool on);
    void ingestReport(const QString &command, const QString &output);
    QStringList applyLines() const;

signals:
    void applyRequested(const CommandApplyRequest &request);
    void refreshRequested();          // Info Refresh, and compress --preview

private:
    void buildShell();
    void buildBody();
    void populatePicker(const QList<CommandPickerItem> &items, const QString &current);
    void rebuildPickerList();
    void onPickerCurrentChanged();
    void updateInfoPanel();
    void updateSaveEnabled();
    void onSave();
    static void splitModelIdentity(const CommandPickerItem &item,
                                   QString *provider, QString *model, QString *id);
    QString defaultTitle() const;
    QString defaultIcon() const;

    CommandPlan m_plan;
    CommandCatalog::Command m_command;
    // The command's own body. Owned here; outlives every widget the shell has.
    CommandHost *m_host = nullptr;

    QLabel *m_error = nullptr;
    QFrame *m_infoPanel = nullptr;
    QLabel *m_infoBadge = nullptr;
    QLabel *m_infoHeadline = nullptr;
    QLabel *m_infoBody = nullptr;
    QLineEdit *m_filter = nullptr;
    QListWidget *m_list = nullptr;
    QCheckBox *m_persist = nullptr;
    QPlainTextEdit *m_body = nullptr;
    QLineEdit *m_text = nullptr;
    QSpinBox *m_undoSpin = nullptr;
    QCheckBox *m_previewOnly = nullptr;
    QCheckBox *m_stayHere = nullptr;
    QComboBox *m_saveFormat = nullptr;
    QLineEdit *m_saveFile = nullptr;
    QCheckBox *m_saveRedact = nullptr;
    QComboBox *m_toolsAction = nullptr;
    QLineEdit *m_toolsNames = nullptr;
    QLabel *m_busyLabel = nullptr;
    QWidget *m_spinner = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
    QPushButton *m_save = nullptr;
    QPushButton *m_cancel = nullptr;
    QPushButton *m_refresh = nullptr;

    QList<CommandPickerItem> m_items;
    QString m_currentValue;
    QString m_selected;
    bool m_busy = false;
    bool m_applying = false;
    QHash<QString, QString> m_providerByValue;
};
