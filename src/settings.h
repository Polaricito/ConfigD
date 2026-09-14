#pragma once

#include <QColor>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

#include "binds.h"

struct SettingSpec {
    enum Type { Int, Double, Bool, String, Color, Combo };
    Type type = String;
    QString key;     // dotted path inside the hl.config tree
    QString section; // "display" | "input" | "colors"
    QString label;
    double minV = 0, maxV = 100, stepV = 1;
    int decimals = 2;
    QStringList comboLabels;
    QList<QVariant> comboData;
    QString hint;

    static const QVector<SettingSpec> &all();
};

struct AppField {
    QString var;   // variable name in variables.lua
    QString label;
};
const QVector<AppField> &appFields();

struct GeneralEdit {
    SettingSpec::Type type = SettingSpec::String;
    QString key;
    QVariant value;
};

// Hyprland color strings ("rgba(AARRGGBB)" / "AARRGGBB" / "RRGGBB") <-> QColor
QColor hyprColorFromConfig(const QString &s);
QString hyprColorToConfig(const QColor &c);

class SettingsModel {
public:
    SettingsModel();

    const QString &configPath() const { return m_configDir; }
    QString defaultGeneralPath() const;
    QString defaultColorsPath() const;
    QString defaultVarsPath() const;
    QString defaultKeybindsPath() const;
    QString customGeneralPath() const;
    QString customVarsPath() const;
    QString customKeybindsPath() const;
    QString idlePath() const;
    QString idleImportPath() const;

    bool load();
    void loadIdle();
    void loadApps();
    void loadBinds();

    // Effective value: custom override wins, else default.
    QVariant valueAt(const QString &dottedKey) const;
    // Stock default only (ignores custom overrides).
    QVariant defaultValue(const QString &dottedKey) const;
    QVariant defaultVar(const QString &name) const;
    QVariant overrideVar(const QString &name) const;

    const QVector<KeyBind> &keybinds() const { return m_keybinds; }
    // Pretty shortcut (e.g. "⊞ + ↵") bound to the given variables.lua app var.
    QString keybindForApp(const QString &var) const;

    // Remap a bind identified by its stock chord + action, or reset it.
    // err receives a user-facing message when returning false.
    bool remapBind(const QString &stockChord, const QString &action,
                   const QString &newChord, QString *err);
    bool resetBind(const QString &stockChord, const QString &action, QString *err);
    // Duplicate a bind as a new custom bind (same action + options, new chord).
    bool addBind(const QString &newChord, const QString &action,
                 const QString &options, QString *err);
    // Other binds currently using a chord (conflict check for remapping).
    QVector<KeyBind> bindsUsingChord(const QString &chord,
                                     const QString &exclStockChord,
                                     const QString &exclAction) const;
    // Allowed Lua identifiers reachable in the custom keybinds file.
    bool bindEditable(const KeyBind &b) const;

    // Git-style backups: a `base/` snapshot is taken once (pre-change state), and
// every later Apply stores only the files that changed relative to the base in
// `commits/<stamp>/`.
    void ensureBaseBackup() const;
    void commitConfigChanges();
    // Copy config files into destDir (--export / snapshots).
    static bool exportSnapshot(const QString &destDir, const QString &configDir, QString *err);
    // Validate a snippet with `luac -p`.
    static bool validateLuaSource(const QString &content);
    // Re-read everything after an external (raw editor) change.
    void reloadAfterCustomEdit();

    int idleLockSec = 300;
    int idleDpmsSec = 600;
    int idleSuspendSec = 900;

    // Build changed overrides from edits and write config files.
    bool save(const QList<GeneralEdit> &generalEdits,
              const QMap<QString, QString> &varChanges,
              int lockSec, int dpmsSec, int suspendSec);

private:
    QString m_configDir;
    QVariantMap m_defaultTree;  // merged hyprland/general + hyprland/colors
    QVariantMap m_overrideTree; // custom/general
    QMap<QString, QString> m_defaultVars;
    QMap<QString, QString> m_overrideVars;
    QVector<KeyBind> m_keybinds;
    QVector<KeyBind> m_stockBinds;
    QSet<QString> m_allowedIdentifiers;
    QSet<QString> m_customUnbound;

    bool writeGeneral(const QList<GeneralEdit> &edits);
    bool writeVariables(const QMap<QString, QString> &varChanges);
    bool writeIdle(int lockSec, int dpmsSec, int suspendSec);
    bool appendCustomKeybinds(const QString &markerLine, const QString &block);
    void reloadHypr() const;
};