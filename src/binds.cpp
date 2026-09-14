#include "binds.h"

#include <QFile>
#include <QRegularExpression>

namespace {

// Index of the ')' matching the '(' at openIdx, or -1.
int matchingParen(const QString &s, int openIdx) {
    int depth = 0;
    bool inStr = false;
    QChar strc;
    for (int i = openIdx; i < s.size(); ++i) {
        QChar ch = s.at(i);
        if (inStr) {
            if (ch == strc && (i == 0 || s.at(i - 1) != '\\')) inStr = false;
            continue;
        }
        if (ch == '"' || ch == '\'') { inStr = true; strc = ch; continue; }
        if (ch == '(') ++depth;
        else if (ch == ')') {
            --depth;
            if (depth == 0) return i;
        }
    }
    return -1;
}

// Start/end (exclusive) indices of the first quoted string at/after `from`.
// Returns {-1,-1} if none.
QPair<int, int> quotedString(const QString &s, int from) {
    for (int i = from; i < s.size(); ++i) {
        QChar ch = s.at(i);
        if (ch == '"' || ch == '\'') {
            bool escaped = false;
            for (int j = i + 1; j < s.size(); ++j) {
                if (s.at(j) == '\\' && !escaped) { escaped = true; continue; }
                if (s.at(j) == ch && !escaped) return {i, j + 1};
                escaped = false;
            }
            return {-1, -1};
        }
        if (ch == '-') {
            if (s.mid(i, 2) == "--") {
                while (i < s.size() && s.at(i) != '\n') ++i;
                continue;
            }
        }
    }
    return {-1, -1};
}

void skipWsAndComments(const QString &s, int &i) {
    while (i < s.size()) {
        QChar ch = s.at(i);
        if (ch.isSpace()) { ++i; continue; }
        if (s.mid(i, 2) == "--") {
            while (i < s.size() && s.at(i) != '\n') ++i;
            continue;
        }
        break;
    }
}

// First top-level comma index after `from` (before closeIdx), or -1.
int topLevelComma(const QString &s, int from, int closeIdx) {
    int depth = 0;
    bool inStr = false;
    QChar strc;
    for (int i = from; i < closeIdx; ++i) {
        QChar ch = s.at(i);
        if (inStr) {
            if (ch == strc && (i == 0 || s.at(i - 1) != '\\')) inStr = false;
            continue;
        }
        if (ch == '"' || ch == '\'') { inStr = true; strc = ch; continue; }
        if (ch == '(' || ch == '{' || ch == '[') { ++depth; continue; }
        if (ch == ')' || ch == '}' || ch == ']') { --depth; continue; }
        if (ch == ',' && depth == 0) return i;
    }
    return -1;
}

QString prettyToken(const QString &t) {
    static const struct { const char *raw; const char *sym; } M[] = {
        {"SUPER_L", "\u229E"},  {"SUPER_R", "\u229E"}, {"SUPER", "\u229E"},
        {"CTRL", "Ctrl"},      {"CONTROL", "Ctrl"},   {"SHIFT", "\u21E7"},
        {"ALT", "Alt"},        {"ALTGR", "AltGr"},
        {"Return", "\u21B5"},  {"Enter", "\u21B5"},   {"Tab", "\u21E5"},
        {"Space", "Space"},
        {"Left", "\u2190"},    {"Right", "\u2192"},   {"Up", "\u2191"}, {"Down", "\u2193"},
        {"Page_Up", "PgUp"},   {"Page_Down", "PgDn"},
        {"Delete", "Del"},     {"Backspace", "\u232B"}, {"Escape", "Esc"},
        {"Print", "PrtSc"},    {"Insert", "Ins"},     {"Home", "Home"}, {"End", "End"},
        {"Minus", "\u2212"},   {"Equal", "="},        {"Slash", "/"},
        {"Semicolon", ";"},    {"Apostrophe", "'"},   {"Backtick", "`"},
        {"BracketLeft", "["},  {"BracketRight", "]"},
        {"Comma", ","},        {"Period", "."},
        {"mouse:272", "LMB"},  {"mouse:273", "RMB"},  {"mouse:274", "MMB"},
        {"mouse:275", "\u2191 sc"}, {"mouse:276", "\u2193 sc"},
        {"mouse_up", "\u2191 sc"},  {"mouse_down", "\u2193 sc"},
        {"XF86AudioRaiseVolume", "\u266A+"}, {"XF86AudioLowerVolume", "\u266A\u2212"},
        {"XF86AudioMute", "Mute"},  {"XF86AudioMicMute", "MicMute"},
        {"XF86AudioPlay", "\u25B6"}, {"XF86AudioPause", "\u23F8"},
        {"XF86AudioNext", "\u23ED"}, {"XF86AudioPrev", "\u23EE"},
        {"XF86MonBrightnessUp", "\u2600+"}, {"XF86MonBrightnessDown", "\u2600\u2212"},
        {"Delete", "Del"},
    };
    for (const auto &m : M)
        if (t == QLatin1String(m.raw)) return QString::fromUtf8(m.sym);
    if (t.startsWith("code:")) return t.mid(5);
    if (t.startsWith("mouse:")) return t.mid(6);
    return t;
}

} // namespace

QString formatChord(const QString &rawChord) {
    QStringList out;
    const QStringList tokens = rawChord.split('+');
    for (QString t : tokens) out << prettyToken(t.trimmed());
    return out.join(" + ");
}

bool chordLooksValid(const QString &chord) {
    if (chord.trimmed().isEmpty()) return false;
    if (chord.contains('"') || chord.contains('\'') || chord.contains('\\')) return false;
    for (QChar c : chord) {
        if (c.isLetterOrNumber() || c == ' ' || c == '+' || c == '-' || c == '_' ||
            c == ':' || c == ';')
            continue;
        return false;
    }
    return true;
}

bool actionIsSelfContained(const QString &action, const QSet<QString> &allowedVars) {
    static const QSet<QString> keywords = {"true", "false", "nil"};
    // Mask out string literals and comments, then inspect remaining identifiers.
    QString masked = action;
    for (int i = 0; i < masked.size();) {
        QChar ch = masked.at(i);
        if (ch == '"' || ch == '\'') {
            QChar q = ch;
            int j = i + 1;
            while (j < masked.size()) {
                if (masked.at(j) == '\\') { j += 2; continue; }
                if (masked.at(j) == q) { ++j; break; }
                ++j;
            }
            masked.replace(i, j - i, QString(j - i, ' '));
            i = j;
            continue;
        }
        if (masked.mid(i, 2) == "--") {
            int j = i;
            while (j < masked.size() && masked.at(j) != '\n') ++j;
            masked.replace(i, j - i, QString(j - i, ' '));
            i = j;
            continue;
        }
        ++i;
    }

    QRegularExpression identRe("[A-Za-z_][A-Za-z0-9_]*");
    auto it = identRe.globalMatch(masked);
    int prevEnd = 0;
    while (it.hasNext()) {
        const auto m = it.next();
        const QString word = m.captured();
        const int start = m.capturedStart();
        // Skip any leading whitespace between the previous identifier and this one.
        int k = prevEnd;
        while (k < start && masked.at(k).isSpace()) ++k;
        bool afterDot = (k < start && masked.at(k) == '.');
        if (afterDot) { // member of the dsp module chain, e.g. hl.dsp.exec_cmd
            prevEnd = m.capturedEnd();
            continue;
        }
        if (!keywords.contains(word) && !allowedVars.contains(word) && word != "hl")
            return false;
        prevEnd = m.capturedEnd();
    }
    return true;
}

// Scans from `openIdx` for one static quoted chord of a bind/unbind call.
// Returns the raw chord without quotes, or empty string if it's dynamic.
QString chordArgAt(const QString &text, int openIdx) {
    const auto q = quotedString(text, openIdx + 1);
    if (q.first < 0) return QString();
    // The chord must be the very first thing in the argument list (after whitespace).
    int j = openIdx + 1;
    skipWsAndComments(text, j);
    if (j != q.first) return QString(); // e.g. hl.bind(keycombos[i], ...) -> dynamic
    const QString raw = text.mid(q.first, q.second - q.first);
    const QString inner = raw.mid(1, raw.size() - 2);
    if (inner.contains("..")) return QString(); // loop/variable-generated
    // Reject concatenations started right after the quoted prefix,
    // e.g. hl.bind("SUPER + " .. arrowkey[i], ...)
    int i = q.second;
    skipWsAndComments(text, i);
    if (text.mid(i, 2) == "..") return QString();
    return inner;
}

KeybindFile parseKeybinds(const QString &filePath) {
    KeybindFile result;
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return result;

    const QString text = QString::fromUtf8(f.readAll());

    // "-- HyprChange remap of \"<stockChord>\" ..." markers written by the app
    // link a remapped bind back to its original chord so the merge identity is
    // stable. "-- HyprChange custom of \"<chord>\" ..." marks a duplicated
    // custom bind. Markers written before the rename (brand "HyprSet") are
    // still understood so existing dotfiles keep merging correctly.
    static const QRegularExpression markerRe(
        QStringLiteral("--\\s*(?:HyprSet|HyprChange) (remap|custom copy|custom) of \"([^\"]*)\""));
    QVector<QPair<int, QString>> markers;
    {
        auto mit = markerRe.globalMatch(text);
        while (mit.hasNext()) {
            const auto mm = mit.next();
            markers.append({mm.capturedStart(), mm.captured(2)});
        }
    }

    auto markerStockChordFor = [&markers](int bindPos) -> QString {
        QString best;
        for (const auto &mr : markers) {
            if (mr.first < bindPos && (bindPos - mr.first) < 400)
                best = mr.second;
        }
        return best;
    };

    static const QRegularExpression callRe("hl\\.(bind|unbind|binde)\\s*\\(");
    auto it = callRe.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString kind = m.captured(1);
        const int open = m.capturedStart() + m.capturedLength() - 1;
        const int close = matchingParen(text, open);
        if (close < 0) continue;

        const QString chord = chordArgAt(text, open);
        if (kind == "unbind") {
            if (!chord.isEmpty()) result.unbound << chord;
            continue;
        }

        if (chord.isEmpty()) continue; // loop/variable-generated: not remappable

        KeyBind b;
        b.chord = chord;
        b.stockChord = chord;
        b.display = formatChord(chord);

        // Slice the verbatim action and options arguments.
        int i = (quotedString(text, open + 1).second); // char after closing quote
        skipWsAndComments(text, i);
        if (i < close && text.at(i) == ',') {
            ++i;
            skipWsAndComments(text, i);
            const int comma = topLevelComma(text, i, close);
            const int actionEnd = (comma > 0) ? comma : close;
            b.action = text.mid(i, actionEnd - i).trimmed();
            if (comma > 0) {
                int o = comma + 1;
                skipWsAndComments(text, o);
                b.options = text.mid(o, close - o).trimmed();
            }
        }

        // Description lives in the options table argument.
        const QString region = text.mid(open + 1, close - open - 1);
        const QRegularExpression descRe(QStringLiteral("description\\s*=\\s*\"([^\"]*)\""));
        const QRegularExpressionMatch dm = descRe.match(region);
        if (dm.hasMatch()) b.description = dm.captured(1);

        // Rebind markers: remember which stock bind this custom bind replaces.
        const QString mark = markerStockChordFor(m.capturedStart());
        if (!mark.isEmpty()) {
            b.stockChord = mark;
            b.fromCustom = true;
            b.rebound = true;
        }

        result.binds.append(b);
    }
    return result;
}