#pragma once

#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

struct KeyBind {
    QString chord;        // effective raw chord, e.g. "SUPER + Q" (custom override if remapped)
    QString display;      // symbol-formatted, e.g. "⊞ + Q"
    QString description;  // may be empty
    QString action;       // verbatim action source, e.g. `hl.dsp.exec_cmd(terminal)`
    QString options;      // verbatim 3rd-arg options text, e.g. `{ description = "...", locked = true }`
    QString stockChord;   // chord as defined in the stock hyprland file ("" for pure-custom binds)
    bool fromCustom = false; // row exists only in the custom file (or chord was remapped there)
    bool rebound = false;    // this stock bind was remapped in the custom file
    bool editable = false;   // action is self-contained -> safe to copy and remap
};

// Result of parsing one keybinds file.
struct KeybindFile {
    QVector<KeyBind> binds;
    QStringList unbound; // chords removed via hl.unbind(...), in order
};

// Parse a keybinds.lua file, extracting statically-declared binds and unbinds.
// Loop-generated binds (chords built with `..`) and closure binds are parsed
// for their actions but marked with an empty chord (not listed individually).
KeybindFile parseKeybinds(const QString &filePath);

// Turn a raw chord like "SUPER + SHIFT + Return" into "⊞ + ⇧ + ↵".
QString formatChord(const QString &rawChord);

// True when the action source is a plain `hl.dsp.…(...)` call whose only
// identifiers are the dsp/module chain, keywords, or members of `allowedVars`.
// Such actions can be copied verbatim into the custom keybinds file.
bool actionIsSelfContained(const QString &action, const QSet<QString> &allowedVars);

// Sanity check a chord typed by the user: non-empty, no quotes, valid characters.
bool chordLooksValid(const QString &chord);