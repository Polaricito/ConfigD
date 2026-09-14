#include "settings.h"

#include "luaparser.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QHash>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>
#include <QDateTime>
#include <QTemporaryFile>
#include <QTextStream>

namespace {

QString readFile(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    return QString::fromUtf8(f.readAll());
}

bool writeFile(const QString &path, const QString &content) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    f.write(content.toUtf8());
    return true;
}

bool copyRecursively(const QString &src, const QString &dst, const QStringList &skipDirs = {}) {
    QDir srcDir(src);
    if (!srcDir.exists()) return false;
    if (!QDir().mkpath(dst)) return false;
    for (const QFileInfo &entry : srcDir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot)) {
        if (entry.isDir()) {
            if (skipDirs.contains(entry.fileName())) continue;
            if (!copyRecursively(entry.absoluteFilePath(), dst + "/" + entry.fileName(), skipDirs))
                return false;
        } else {
            QFile::remove(dst + "/" + entry.fileName());
            if (!QFile::copy(entry.absoluteFilePath(), dst + "/" + entry.fileName()))
                return false;
        }
    }
    return true;
}

bool fileContentsSame(const QString &a, const QString &b) {
    QFile fa(a), fb(b);
    if (!fa.open(QIODevice::ReadOnly)) return false;
    if (!fb.open(QIODevice::ReadOnly)) return false;
    return fa.readAll() == fb.readAll();
}

QStringList collectRegularFiles(const QString &dir, const QString &prefix = QString()) {
    QStringList out;
    const QFileInfoList entries =
        QDir(dir).entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo &fi : entries) {
        const QString rel = prefix.isEmpty() ? fi.fileName() : prefix + "/" + fi.fileName();
        if (fi.isDir()) out += collectRegularFiles(fi.absoluteFilePath(), rel);
        else out << rel;
    }
    return out;
}

bool sameNumber(const QVariant &a, const QVariant &b) {
    return qFuzzyCompare(a.toDouble(), b.toDouble());
}

QStringList keyParts(const QString &key) {
    QStringList parts = key.split('.');
    if (parts.size() == 1 && parts.first().isEmpty()) return {};
    return parts;
}

} // namespace

// ---- Hyprland color <-> QColor ----
// In the rgba(rrggbbaa) form used by these configs the LAST two hex digits
// are alpha: "ffdcc2FF" = #ffdcc2 with full opacity.
QString hyprColorToConfig(const QColor &c) {
    return QString("rgba(%1%2%3%4)")
        .arg(c.red(),   2, 16, QLatin1Char('0'))
        .arg(c.green(), 2, 16, QLatin1Char('0'))
        .arg(c.blue(),  2, 16, QLatin1Char('0'))
        .arg(c.alpha(), 2, 16, QLatin1Char('0'))
        .toUpper()
        .replace(0, 4, "rgba");
}

QColor hyprColorFromConfig(const QString &s) {
    QString hex = s.trimmed();
    if (hex.startsWith("rgba(") && hex.endsWith(")")) {
        hex = hex.mid(5, hex.size() - 6);
    }
    hex.remove(' ').remove('#');
    if (hex.size() == 8) { // RRGGBBAA
        bool ok = false;
        uint v = hex.toUInt(&ok, 16);
        if (!ok) return QColor();
        const uint r = (v >> 24) & 0xFF, g = (v >> 16) & 0xFF, b = (v >> 8) & 0xFF, a = v & 0xFF;
        return QColor(r, g, b, a);
    }
    if (hex.size() == 6) {
        bool ok = false;
        uint v = hex.toUInt(&ok, 16);
        if (!ok) return QColor();
        return QColor((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF, 255);
    }
    return QColor();
}

// ---- Specs ----

const QVector<SettingSpec> &SettingSpec::all() {
    static const QVector<SettingSpec> specs = {
        {SettingSpec::Int,    "general.gaps_in",              "display", "Gaps inside", 0, 100, 1, 2, {}, {}, ""},
        {SettingSpec::Int,    "general.gaps_out",             "display", "Gaps outside", 0, 100, 1, 2, {}, {}, ""},
        {SettingSpec::Int,    "general.gaps_workspaces",      "display", "Gaps around workspaces", 0, 200, 1, 2, {}, {}, "px"},
        {SettingSpec::Int,    "general.border_size",          "display", "Border width", 0, 20, 1, 2, {}, {}, "px"},
        {SettingSpec::Int,    "decoration.rounding",          "display", "Window rounding", 0, 40, 1, 2, {}, {}, "px"},
        {SettingSpec::Double, "decoration.rounding_power",    "display", "Corner power (squircle)", 0.5, 4, 0.1, 2, {}, {}, "2 = circle"},
        {SettingSpec::Bool,   "decoration.shadow.enabled",    "display", "Shadows", 0, 0, 0, 2, {}, {}, ""},
        {SettingSpec::Int,    "decoration.shadow.range",      "display", "Shadow size", 0, 100, 1, 2, {}, {}, ""},
        {SettingSpec::Bool,   "decoration.blur.enabled",      "display", "Blur", 0, 0, 0, 2, {}, {}, ""},
        {SettingSpec::Int,    "decoration.blur.size",         "display", "Blur size", 1, 20, 1, 2, {}, {}, ""},
        {SettingSpec::Int,    "decoration.blur.passes",       "display", "Blur passes", 1, 8, 1, 2, {}, {}, ""},
        {SettingSpec::Double, "decoration.blur.brightness",   "display", "Blur brightness", 0.1, 2, 0.05, 2, {}, {}, ""},
        {SettingSpec::Double, "decoration.blur.contrast",     "display", "Blur contrast", 0.1, 2, 0.05, 2, {}, {}, ""},
        {SettingSpec::Double, "decoration.blur.noise",        "display", "Blur noise", 0, 1, 0.01, 2, {}, {}, ""},
        {SettingSpec::Double, "decoration.blur.vibrancy",     "display", "Blur vibrancy", 0, 1, 0.05, 2, {}, {}, ""},
        {SettingSpec::Double, "decoration.blur.vibrancy_darkness", "display", "Blur vibrancy (dark)", 0, 1, 0.05, 2, {}, {}, ""},
        {SettingSpec::Bool,   "decoration.blur.popups",       "display", "Blur popups", 0, 0, 0, 2, {}, {}, ""},
        {SettingSpec::Bool,   "decoration.dim_inactive",      "display", "Dim inactive windows", 0, 0, 0, 2, {}, {}, ""},
        {SettingSpec::Double, "decoration.dim_strength",      "display", "Dim strength", 0, 1, 0.01, 2, {}, {}, ""},
        {SettingSpec::Double, "decoration.dim_special",       "display", "Dim scratchpad windows", 0, 1, 0.05, 2, {}, {}, ""},
        {SettingSpec::Bool,   "animations.enabled",           "display", "Animations", 0, 0, 0, 2, {}, {}, ""},
        {SettingSpec::Bool,   "general.allow_tearing",        "display", "Allow tearing (immediate rule)", 0, 0, 0, 2, {}, {}, ""},
        {SettingSpec::Bool,   "general.resize_on_border",     "display", "Resize windows at border", 0, 0, 0, 2, {}, {}, ""},
        {SettingSpec::Bool,   "misc.disable_hyprland_logo",   "display", "Hide Hyprland logo", 0, 0, 0, 2, {}, {}, ""},
        {SettingSpec::Bool,   "misc.disable_splash_rendering","display", "Hide splash text", 0, 0, 0, 2, {}, {}, ""},
        {SettingSpec::Combo,  "misc.vrr",                     "display", "Variable refresh rate (VRR)",
                                         0, 0, 0, 2,
                                         {"0 - off", "1 - always", "2 - on demand"},
                                         {QVariant(0), QVariant(1), QVariant(2)}, ""},

        {SettingSpec::String, "input.kb_layout",              "input", "Keyboard layout", 0, 0, 0, 2, {}, {}, "e.g. es, us, or a list 'us,es'" },
        {SettingSpec::Bool,   "input.numlock_by_default",     "input", "NumLock on start", 0, 0, 0, 2, {}, {}, ""},
        {SettingSpec::Int,    "input.repeat_delay",           "input", "Key repeat delay", 50, 1000, 10, 2, {}, {}, "ms"},
        {SettingSpec::Int,    "input.repeat_rate",            "input", "Key repeat rate", 5, 100, 1, 2, {}, {}, "chars per second"},
        {SettingSpec::Combo,  "input.follow_mouse",           "input", "Focus follows mouse",
                                         0, 0, 0, 2,
                                         {"0 - disabled", "1 - follow mouse", "2 - follow mouse on click"},
                                         {QVariant(0), QVariant(1), QVariant(2)},
                                         "See Hyprland docs for mode 3"},
        {SettingSpec::Combo,  "input.off_window_axis_events", "input", "Scroll events off-window",
                                         0, 0, 0, 2,
                                         {"0 - never", "1 - per workspace borders", "2 - per monitor borders"},
                                         {QVariant(0), QVariant(1), QVariant(2)}, ""},
        {SettingSpec::Bool,   "input.touchpad.natural_scroll",  "input", "Natural scroll", 0, 0, 0, 2, {}, {}, ""},
        {SettingSpec::Bool,   "input.touchpad.disable_while_typing", "input", "Disable touchpad while typing", 0, 0, 0, 2, {}, {}, ""},
        {SettingSpec::Bool,   "input.touchpad.clickfinger_behavior", "input", "Clickfinger behavior (two-finger right click)", 0, 0, 0, 2, {}, {}, ""},
        {SettingSpec::Double, "input.touchpad.scroll_factor", "input", "Touchpad scroll speed", 0.1, 3, 0.05, 2, {}, {}, ""},

        {SettingSpec::Color,  "general.col.active_border",    "colors", "Active border color", 0, 0, 0, 2, {}, {}, ""},
        {SettingSpec::Color,  "general.col.inactive_border",  "colors", "Inactive border color", 0, 0, 0, 2, {}, {}, ""},
        {SettingSpec::Color,  "decoration.shadow.color",      "colors", "Shadow color", 0, 0, 0, 2, {}, {}, ""},
        {SettingSpec::Color,  "misc.background_color",        "colors", "Background color", 0, 0, 0, 2, {}, {}, ""},

        {SettingSpec::Bool,   "dwindle.preserve_split",       "layout", "Preserve split ratio on window change", 0, 0, 0, 2, {}, {}, ""},
        {SettingSpec::Bool,   "dwindle.smart_split",          "layout", "Smart split", 0, 0, 0, 2, {}, {}, ""},
        {SettingSpec::Bool,   "dwindle.smart_resizing",       "layout", "Smart resizing", 0, 0, 0, 2, {}, {}, ""},
        {SettingSpec::Bool,   "general.snap.enabled",         "layout", "Window snapping", 0, 0, 0, 2, {}, {}, ""},
        {SettingSpec::Int,    "general.snap.window_gap",      "layout", "Snap gap between windows", 0, 50, 1, 2, {}, {}, "px"},
        {SettingSpec::Int,    "general.snap.monitor_gap",     "layout", "Snap gap to monitor edges", 0, 50, 1, 2, {}, {}, "px"},
        {SettingSpec::Bool,   "general.no_focus_fallback",    "layout", "No focus fallback (keep focus)", 0, 0, 0, 2, {}, {}, ""},
        {SettingSpec::Bool,   "misc.enable_swallow",          "layout", "Swallow terminal windows", 0, 0, 0, 2, {}, {}, ""},
        {SettingSpec::String, "misc.swallow_regex",           "layout", "Swallow window classes", 0, 0, 0, 2, {}, {}, "regex list"},
        {SettingSpec::Bool,   "misc.animate_manual_resizes",  "layout", "Animate manual resizes", 0, 0, 0, 2, {}, {}, ""},
        {SettingSpec::Bool,   "misc.animate_mouse_windowdragging", "layout", "Animate mouse window dragging", 0, 0, 0, 2, {}, {}, ""},
    };
    return specs;
}

const QVector<AppField> &appFields() {
    static const QVector<AppField> fields = {
        {"terminal",     "Terminal"},
        {"fileManager",  "File manager"},
        {"browser",      "Browser"},
        {"codeEditor",   "Code editor"},
        {"officeSoftware", "Office software"},
        {"textEditor",   "Text editor"},
        {"volumeMixer",  "Volume mixer"},
        {"settingsApp",  "Settings app"},
        {"taskManager",  "Task manager"},
    };
    return fields;
}

// ---- Model ----

SettingsModel::SettingsModel() {
    const QString xdg = QString::fromLocal8Bit(qgetenv("XDG_CONFIG_HOME"));
    if (!xdg.isEmpty()) m_configDir = xdg + "/hypr";
    else m_configDir = QDir::homePath() + "/.config/hypr";
}

QString SettingsModel::defaultGeneralPath() const { return m_configDir + "/hyprland/general.lua"; }
QString SettingsModel::defaultColorsPath() const  { return m_configDir + "/hyprland/colors.lua"; }
QString SettingsModel::defaultVarsPath() const     { return m_configDir + "/hyprland/variables.lua"; }
QString SettingsModel::defaultKeybindsPath() const  { return m_configDir + "/hyprland/keybinds.lua"; }
QString SettingsModel::customGeneralPath() const    { return m_configDir + "/custom/general.lua"; }
QString SettingsModel::customVarsPath() const       { return m_configDir + "/custom/variables.lua"; }
QString SettingsModel::customKeybindsPath() const   { return m_configDir + "/custom/keybinds.lua"; }
QString SettingsModel::idlePath() const             { return m_configDir + "/hypridle.conf"; }

bool SettingsModel::load() {
    // Defaults: general first, then colors (colors.lua is sourced after general.lua)
    m_defaultTree = lua::parseHlConfigFile(defaultGeneralPath());
    lua::merge(m_defaultTree, lua::parseHlConfigFile(defaultColorsPath()));
    // Overrides from the update-friendly custom folder
    m_overrideTree = lua::parseHlConfigFile(customGeneralPath());

    m_defaultVars = lua::parseSimpleAssignments(defaultVarsPath());
    m_overrideVars = lua::parseSimpleAssignments(customVarsPath());

    loadIdle();
    loadBinds();
    return true;
}

void SettingsModel::loadBinds() {
    m_stockBinds = parseKeybinds(defaultKeybindsPath()).binds;
    const KeybindFile cust = parseKeybinds(customKeybindsPath());
    m_customUnbound = QSet<QString>(cust.unbound.begin(), cust.unbound.end());

    // Lua identifiers that are global in custom/keybinds.lua: variables.lua entries
    // (loaded via hyprland.keybinds before the custom file runs).
    m_allowedIdentifiers.clear();
    for (auto it = m_defaultVars.constBegin(); it != m_defaultVars.constEnd(); ++it)
        m_allowedIdentifiers.insert(it.key());
    for (auto it = m_overrideVars.constBegin(); it != m_overrideVars.constEnd(); ++it)
        m_allowedIdentifiers.insert(it.key());

    // Match each custom bind to its stock bind: same original chord + verbatim action.
    QVector<KeyBind> out;
    QSet<int> matchedCustom;

    QHash<QString, int> custIndexByChordAction;
    for (int i = 0; i < cust.binds.size(); ++i) {
        const KeyBind &b = cust.binds.at(i);
        if (!b.stockChord.isEmpty())
            custIndexByChordAction.insert(b.stockChord + "\x1f" + b.action, i);
    }

    for (const KeyBind &stock : m_stockBinds) {
        auto it2 = custIndexByChordAction.constFind(stock.chord + "\x1f" + stock.action);
        if (it2 != custIndexByChordAction.constEnd()) {
            // Chord was remapped in the custom file.
            KeyBind eff = stock;
            eff.chord = cust.binds.at(it2.value()).chord;
            eff.display = cust.binds.at(it2.value()).display;
            eff.fromCustom = true;
            eff.rebound = true;
            out.append(eff);
            matchedCustom.insert(it2.value());
            continue;
        }
        if (m_customUnbound.contains(stock.chord)) continue; // removed, no replacement
        out.append(stock);
    }

    // Pure-custom binds (e.g. the user's own additions) become their own rows.
    for (int i = 0; i < cust.binds.size(); ++i) {
        if (matchedCustom.contains(i)) continue;
        KeyBind eff = cust.binds.at(i);
        eff.fromCustom = true;
        out.append(eff);
    }

    for (KeyBind &b : out) b.editable = bindEditable(b);

    std::stable_sort(out.begin(), out.end(),
                     [](const KeyBind &a, const KeyBind &b) {
                         if (a.description.isEmpty() != b.description.isEmpty())
                             return a.description.isEmpty() < b.description.isEmpty();
                         if (a.description != b.description)
                             return a.description < b.description;
                         return a.display < b.display;
                     });
    m_keybinds = out;
}

static QString appLabelForVar(const QString &var) {
    for (const AppField &f : appFields())
        if (f.var == var) return f.label;
    return QString();
}

bool SettingsModel::bindEditable(const KeyBind &b) const {
    return !b.chord.isEmpty() && !b.action.isEmpty() &&
           actionIsSelfContained(b.action, m_allowedIdentifiers);
}

QString SettingsModel::keybindForApp(const QString &var) const {
    const QString label = appLabelForVar(var);
    if (label.isEmpty()) return QString();
    for (const KeyBind &b : m_keybinds)
        if (b.description == "App: " + label) return b.display;
    return QString();
}

QVector<KeyBind> SettingsModel::bindsUsingChord(const QString &chord,
                                                const QString &exclStockChord,
                                                const QString &exclAction) const {
    QVector<KeyBind> hits;
    for (const KeyBind &b : m_keybinds) {
        if (b.chord == chord && (b.stockChord != exclStockChord || b.action != exclAction))
            hits.append(b);
    }
    return hits;
}

bool SettingsModel::appendCustomKeybinds(const QString &markerLine, const QString &block) {
    QString existing = readFile(customKeybindsPath()).trimmed();
    const QString header = "-- Managed by HyprChange: keybind remaps (manual edits here are kept).\n";
    QString content = existing;
    if (content.isEmpty()) content = header + "\n";
    if (!content.endsWith('\n')) content += '\n';
    content += markerLine + '\n' + block;
    if (!content.endsWith('\n')) content += '\n';
    return writeFile(customKeybindsPath(), content);
}

bool SettingsModel::remapBind(const QString &stockChord, const QString &action,
                              const QString &newChord, QString *err) {
    if (!chordLooksValid(newChord)) {
        *err = "That doesn't look like a valid key. Example: SUPER + Z (modifiers SUPER/CTRL/SHIFT/ALT separated by '+').";
        return false;
    }

    const KeyBind *source = nullptr;
    for (const KeyBind &b : m_stockBinds)
        if (b.chord == stockChord && b.action == action) { source = &b; break; }
    if (!source)
        for (const KeyBind &b : m_keybinds)
            if (b.stockChord == stockChord && b.action == action) { source = &b; break; }
    if (!source) {
        *err = "Could not find the original binding to remap.";
        return false;
    }
    if (!actionIsSelfContained(source->action, m_allowedIdentifiers)) {
        *err = "This action uses Lua code that can't be copied into the custom file; editing it would break the bind.";
        return false;
    }

    const QVector<KeyBind> conflicts = bindsUsingChord(newChord, stockChord, action);
    if (!conflicts.isEmpty()) {
        *err = QString("That key is already used by: %1").arg(
            conflicts.first().description.isEmpty()
                ? conflicts.first().display
                : conflicts.first().description);
        return false;
    }

    QString bindStmt = QString("hl.bind(\"%1\", %2").arg(newChord, source->action);
    if (!source->options.trimmed().isEmpty())
        bindStmt += QString(", %1").arg(source->options.trimmed());
    bindStmt += ')';

    const QString marker = QString("-- HyprChange remap of \"%1\" \"%2\"")
                               .arg(stockChord, source->description.isEmpty()
                                                    ? "no description"
                                                    : source->description);
    if (!appendCustomKeybinds(marker, bindStmt + "\n")) {
        *err = "Could not write " + customKeybindsPath();
        return false;
    }
    loadBinds();
    reloadHypr();
    return true;
}

bool SettingsModel::addBind(const QString &newChord, const QString &action,
                            const QString &options, QString *err) {
    if (!chordLooksValid(newChord)) {
        *err = "That doesn't look like a valid key. Example: SUPER + Z (modifiers SUPER/CTRL/SHIFT/ALT separated by '+').";
        return false;
    }
    const QVector<KeyBind> conflicts = bindsUsingChord(newChord, QString(), QString());
    if (!conflicts.isEmpty()) {
        *err = QString("That key is already used by: %1").arg(
            conflicts.first().description.isEmpty()
                ? conflicts.first().display
                : conflicts.first().description);
        return false;
    }

    QString bindStmt = QString("hl.bind(\"%1\", %2").arg(newChord, action);
    if (!options.trimmed().isEmpty()) bindStmt += QString(", %1").arg(options.trimmed());
    bindStmt += ')';

    const QString marker = QString("-- HyprChange custom of \"%1\"").arg(newChord);
    if (!appendCustomKeybinds(marker, bindStmt + "\n")) {
        *err = "Could not write " + customKeybindsPath();
        return false;
    }
    loadBinds();
    reloadHypr();
    return true;
}

bool SettingsModel::resetBind(const QString &stockChord, const QString &action, QString *err) {
    const QString path = customKeybindsPath();
    if (!QFileInfo::exists(path)) return true;

    QStringList lines = readFile(path).split('\n');
    QStringList out;
    bool removing = false;
    static const QRegularExpression markerRe(
        QStringLiteral("--\\s*(?:HyprSet|HyprChange) (remap|custom copy|custom) of \"([^\"]*)\""));
    for (const QString &raw : lines) {
        QString line = raw;
        if (line.trimmed().startsWith("-- Hypr") && line.trimmed().contains(" of ")) {
            // stop a removal block at the next marker
            if (removing) { removing = false; }
        }
        if (removing) continue; // skip the bind/unbind lines of the current block

        // Start a new removal block when this marker targets the wanted bind.
        const auto mm = markerRe.match(line);
        if (mm.hasMatch() && mm.captured(2) == stockChord) {
            removing = true;
            continue;
        }
        out << line;
    }

    const QString wrote = out.join('\n');
    if (wrote == readFile(path)) return true; // nothing to remove
    if (!writeFile(path, wrote)) {
        *err = "Could not write " + path;
        return false;
    }
    loadBinds();
    reloadHypr();
    return true;
}

void SettingsModel::loadIdle() {
    if (!QFileInfo::exists(idlePath())) return;
    const QString text = readFile(idlePath());
    const QStringList blocks = text.split("listener {");
    for (const QString &block : blocks) {
        if (!block.contains("timeout")) continue;
        QRegularExpression re(R"(\btimeout\s*=\s*(\d+)\b)");
        auto m = re.match(block);
        if (!m.hasMatch()) continue;
        int sec = m.captured(1).toInt();
        if (block.contains("dpms")) idleDpmsSec = sec;
        else if (block.contains("suspend_cmd") || block.contains("systemctl suspend") || block.contains("loginctl suspend"))
            idleSuspendSec = sec;
        else if (block.contains("lock-session"))
            idleLockSec = sec;
    }
}

QVariant SettingsModel::valueAt(const QString &dottedKey) const {
    const QStringList parts = keyParts(dottedKey);
    QVariant v;
    if (lua::get(m_overrideTree, parts, &v)) return v;
    if (lua::get(m_defaultTree, parts, &v)) return v;
    return QVariant();
}

QVariant SettingsModel::defaultValue(const QString &dottedKey) const {
    QVariant v;
    const QStringList parts = keyParts(dottedKey);
    return lua::get(m_defaultTree, parts, &v) ? v : QVariant();
}

QVariant SettingsModel::defaultVar(const QString &name) const {
    return m_defaultVars.value(name, QString());
}

QVariant SettingsModel::overrideVar(const QString &name) const {
    return m_overrideVars.value(name, QString());
}

bool SettingsModel::writeGeneral(const QList<GeneralEdit> &edits) {
    QVariantMap tree = m_overrideTree; // preserve anything already in custom/general.lua
    for (const GeneralEdit &e : edits) {
        const QStringList parts = keyParts(e.key);
        QVariant canon;
        switch (e.type) {
        case SettingSpec::Int:    canon = QVariant(e.value.toInt()); break;
        case SettingSpec::Double: canon = QVariant(e.value.toDouble()); break;
        case SettingSpec::Bool:   canon = QVariant(e.value.toBool()); break;
        case SettingSpec::String: canon = QVariant(e.value.toString()); break;
        case SettingSpec::Combo:
            canon = e.value.isValid()
                        ? QVariant(e.value.toDouble())
                        : QVariant(e.value.toString());
            break;
        case SettingSpec::Color:  canon = QVariant(hyprColorToConfig(e.value.value<QColor>())); break;
        }

        // Anything equal to the stock default should NOT be written: the default
        // already wins, and dropping it keeps the custom file update-friendly.
        QVariant def;
        const bool hasDefault = lua::get(m_defaultTree, parts, &def) && def.isValid();
        bool same = false;
        if (hasDefault) {
            switch (e.type) {
            case SettingSpec::Int:
            case SettingSpec::Double:
            case SettingSpec::Combo:
                same = sameNumber(canon, def);
                break;
            case SettingSpec::Bool:
                same = canon.toBool() == def.toBool();
                break;
            case SettingSpec::Color: {
                QString cs = hyprColorToConfig(hyprColorFromConfig(canon.toString()));
                QString ds = hyprColorToConfig(hyprColorFromConfig(def.toString()));
                same = cs == ds;
                break;
            }
            default:
                same = canon.toString() == def.toString();
                break;
            }
        }

        if (same) lua::remove(tree, parts);
        else lua::setPath(tree, parts, canon);
    }

    QString content = lua::serializeHlConfigFile(
        tree, "Managed by HyprChange. This file is loaded after the default hyprland config.\n"
              "Change values from the app instead of editing by hand.");
    return writeFile(customGeneralPath(), content);
}

bool SettingsModel::writeVariables(const QMap<QString, QString> &varChanges) {
    const QStringList managed;
    const QString path = customVarsPath();
    QStringList out;
    if (QFileInfo::exists(path)) {
        out = readFile(path).split('\n');
    }
    const auto &fields = appFields();
    QStringList names;
    for (const AppField &f : fields) names << f.var;

    QStringList filtered;
    for (const QString &line : out) {
        QString trimmed = line.trimmed();
        bool drop = false;
        for (const QString &name : names) {
            if (trimmed.startsWith(name + " =") || trimmed == name || trimmed.startsWith(name + "=")) {
                drop = true;
                break;
            }
        }
        if (!drop) filtered << line;
    }
    // Trim blank tail
    while (!filtered.isEmpty() && filtered.last().trimmed().isEmpty()) filtered.removeLast();

    for (const QString &name : names) {
        QString newVal = varChanges.value(name);
        QString def = m_defaultVars.value(name);
        if (newVal.isEmpty()) newVal = def;
        if (newVal == def && m_overrideVars.contains(name)) {
            continue; // back to stock -> drop the override line entirely
        }
        if (newVal == def && !m_overrideVars.contains(name)) {
            continue; // never changed, don't write
        }
        filtered << name + " = \"" + newVal + "\"";
    }

    // Prepend a header if no app header is present yet
    bool hasHeader = false;
    for (const QString &line : filtered) {
        if (line.trimmed().startsWith("-- Managed by Hypr")) { hasHeader = true; break; }
    }
    QString header = "-- Managed by HyprChange: app launch commands.\n";
    QString body = filtered.join('\n').trimmed();
    QString content;
    if (hasHeader) content = body.isEmpty() ? header : body + "\n";
    else content = header + (body.isEmpty() ? QString() : "\n" + body + "\n");
    return writeFile(path, content);
}

bool SettingsModel::writeIdle(int lockSec, int dpmsSec, int suspendSec) {
    QString content =
        "## Managed by HyprChange (timeouts in seconds)\n\n"
        "$lock_cmd = hyprctl dispatch 'hl.dsp.global(\"quickshell:lock\")' & pidof qs quickshell hyprlock || hyprlock\n"
        "$suspend_cmd = systemctl suspend || loginctl suspend\n\n"
        "general {\n"
        "    lock_cmd = $lock_cmd\n"
        "    before_sleep_cmd = loginctl lock-session\n"
        "    after_sleep_cmd = hyprctl dispatch 'hl.dsp.global(\"quickshell:lockFocus\")'\n"
        "    inhibit_sleep = 3\n"
        "}\n\n"
        "listener {\n"
        "    timeout = " + QString::number(lockSec) + " # lock screen\n"
        "    on-timeout = loginctl lock-session\n"
        "}\n\n"
        "listener {\n"
        "    timeout = " + QString::number(dpmsSec) + " # display off\n"
        "    on-timeout = hyprctl dispatch 'hl.dsp.dpms({ action = \"disable\" })'\n"
        "    on-resume = hyprctl dispatch 'hl.dsp.dpms({ action = \"enable\" })'\n"
        "}\n\n"
        "listener {\n"
        "    timeout = " + QString::number(suspendSec) + " # suspend\n"
        "    on-timeout = $suspend_cmd\n"
        "}\n";
    return writeFile(idlePath(), content);
}

void SettingsModel::reloadAfterCustomEdit() {
    m_defaultTree = lua::parseHlConfigFile(defaultGeneralPath());
    lua::merge(m_defaultTree, lua::parseHlConfigFile(defaultColorsPath()));
    m_overrideTree = lua::parseHlConfigFile(customGeneralPath());
    m_defaultVars = lua::parseSimpleAssignments(defaultVarsPath());
    m_overrideVars = lua::parseSimpleAssignments(customVarsPath());
    loadIdle();
    loadBinds();
    reloadHypr();
}

void SettingsModel::reloadHypr() const {
    if (QProcessEnvironment::systemEnvironment().contains("WAYLAND_DISPLAY")) {
        QProcess::startDetached("hyprctl", {"reload"});
    }
}

void SettingsModel::ensureBaseBackup() const {
    // `base/` is the untouched pre-change state, captured once. The backups dir
    // lives inside the tree, so it is excluded from the copy (self-inclusion
    // made backups exponential before); the pre-rename backup dir name is
    // skipped too so old snapshots are never copied into themselves.
    const QString base = m_configDir + "/hyprchange-backups/base";
    if (QFileInfo::exists(base)) return;
    copyRecursively(m_configDir, base, {"hyprchange-backups", "hyprset-backups"});
}

void SettingsModel::commitConfigChanges() {
    // Git-style: store only the files that differ from `base/` under
    // commits/<stamp>/. Replaying a stamp over the base reconstructs that state.
    const QString backupsDir = m_configDir + "/hyprchange-backups";
    const QString base = backupsDir + "/base";
    if (!QFileInfo::exists(base)) return;

    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    const QString outDir = backupsDir + "/commits/" + stamp;
    bool any = false;
    const QStringList relFiles = collectRegularFiles(m_configDir);
    for (const QString &rel : relFiles) {
        if (rel.startsWith("hyprchange-backups/") || rel.startsWith("hyprset-backups/"))
            continue;
        const QString src = m_configDir + "/" + rel;
        if (QFileInfo::exists(base + "/" + rel) && fileContentsSame(src, base + "/" + rel))
            continue;
        if (writeFile(outDir + "/" + rel, readFile(src))) any = true;
    }
    if (!any) {
        QDir(outDir).removeRecursively(); // unchanged config: no empty commit
        return;
    }

    QDir dir(backupsDir + "/commits");
    QStringList commits = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    while (commits.size() > 30)
        QDir(backupsDir + "/commits/" + commits.takeFirst()).removeRecursively();
}

bool SettingsModel::exportSnapshot(const QString &destDir, const QString &configDir, QString *err) {
    if (destDir.isEmpty() || destDir.contains("..")) {
        if (err) *err = "Invalid destination folder: " + destDir;
        return false;
    }
    if (!QDir().mkpath(destDir)) {
        if (err) *err = "Cannot create " + destDir;
        return false;
    }
    bool ok = true;
    for (const QString &sub : {"hyprland", "custom"}) {
        const QString src = configDir + "/" + sub;
        if (QDir(src).exists()) ok &= copyRecursively(src, destDir + "/" + sub);
    }
    if (QFileInfo::exists(configDir + "/hypridle.conf"))
        ok &= QFile::copy(configDir + "/hypridle.conf", destDir + "/hypridle.conf");
    if (!ok && err) *err = "Some files could not be copied.";
    return ok;
}

bool SettingsModel::validateLuaSource(const QString &content) {
    if (content.contains('\0')) return false;
    QTemporaryFile tmp(QLatin1String("/tmp/hyprchange-XXXXXX.lua"));
    if (!tmp.open()) return false;
    tmp.write(content.toUtf8());
    tmp.flush();
    tmp.close();
    QProcess p;
    p.start("luac", {"-p", tmp.fileName()});
    if (!p.waitForFinished(5000)) return false;
    return p.exitCode() == 0;
}

bool SettingsModel::save(const QList<GeneralEdit> &generalEdits,
                         const QMap<QString, QString> &varChanges,
                         int lockSec, int dpmsSec, int suspendSec) {
    ensureBaseBackup();
    bool ok = writeGeneral(generalEdits);
    ok &= writeVariables(varChanges);
    ok &= writeIdle(lockSec, dpmsSec, suspendSec);
    if (ok) {
        m_overrideTree = lua::parseHlConfigFile(customGeneralPath());
        m_overrideVars = lua::parseSimpleAssignments(customVarsPath());
        loadIdle();
        commitConfigChanges();
        reloadHypr();
    }
    return ok;
}