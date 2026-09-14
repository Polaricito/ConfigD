#include "luaparser.h"

#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QMetaType>

namespace lua {

namespace {

struct Ctx {
    const QString &s;
    int i = 0;
};

bool isIdentStart(QChar c) { return c.isLetter() || c == '_'; }
bool isIdentChar(QChar c)  { return c.isLetterOrNumber() || c == '_'; }

void skipWs(Ctx &c) {
    while (c.i < c.s.size()) {
        QChar ch = c.s.at(c.i);
        if (ch.isSpace()) {
            ++c.i;
        } else if (c.s.mid(c.i, 2) == "--") {
            while (c.i < c.s.size() && c.s.at(c.i) != '\n') ++c.i;
        } else if (c.s.mid(c.i, 2) == "/*") {
            c.i += 2;
            while (c.i < c.s.size() && c.s.mid(c.i, 2) != "*/") ++c.i;
        } else {
            break;
        }
    }
}

QString readString(Ctx &c) {
    ++c.i; // opening quote
    QString out;
    while (c.i < c.s.size()) {
        QChar ch = c.s.at(c.i);
        if (ch == '"') { ++c.i; break; }
        if (ch == '\\' && c.i + 1 < c.s.size()) {
            ++c.i;
            QChar esc = c.s.at(c.i);
            if (esc == 'n') out += '\n';
            else if (esc == 't') out += '\t';
            else if (esc == '"') out += '"';
            else if (esc == '\\') out += '\\';
            else out += esc;
            ++c.i;
            continue;
        }
        out += ch;
        ++c.i;
    }
    return out;
}

QString readNumber(Ctx &c) {
    int start = c.i;
    if (c.i < c.s.size() && (c.s.at(c.i) == '-' || c.s.at(c.i) == '+')) ++c.i;
    bool seenDot = false, seenExp = false;
    while (c.i < c.s.size()) {
        QChar ch = c.s.at(c.i);
        if (ch.isDigit()) { ++c.i; continue; }
        if (ch == '.' && !seenDot && !seenExp) { seenDot = true; ++c.i; continue; }
        if ((ch == 'e' || ch == 'E') && !seenExp) {
            seenExp = true;
            ++c.i;
            if (c.i < c.s.size() && (c.s.at(c.i) == '-' || c.s.at(c.i) == '+')) ++c.i;
            continue;
        }
        break;
    }
    return c.s.mid(start, c.i - start);
}

QString readIdent(Ctx &c, bool *ok = nullptr) {
    skipWs(c);
    if (c.i >= c.s.size() || !isIdentStart(c.s.at(c.i))) {
        if (ok) *ok = false;
        return QString();
    }
    int start = c.i;
    while (c.i < c.s.size() && isIdentChar(c.s.at(c.i))) ++c.i;
    if (ok) *ok = true;
    return c.s.mid(start, c.i - start);
}

QVariant parseValue(Ctx &c);

QVariant parseTable(Ctx &c, char opener = '{') {
    // assumes we are at '{'
    char closer = (opener == '{') ? '}' : ')';
    ++c.i;
    QVariantMap map;
    QList<QVariant> positional;
    bool hasKeyed = false;

    for (;;) {
        skipWs(c);
        if (c.i >= c.s.size()) break;
        QChar ch = c.s.at(c.i);
        if (ch == closer) { ++c.i; break; }
        if (ch == ',' || ch == ';') { ++c.i; continue; }

        int save = c.i;
        bool ok = false;
        QString ident = readIdent(c, &ok);
        skipWs(c);
        if (ok && c.i < c.s.size() && c.s.at(c.i) == '=') {
            ++c.i; // '='
            QVariant v = parseValue(c);
            map.insert(ident, v);
            hasKeyed = true;
        } else {
            c.i = save;
            positional.append(parseValue(c));
        }
    }

    if (!hasKeyed && !positional.isEmpty()) {
        return QVariant::fromValue(positional);
    }
    for (int i = 0; i < positional.size(); ++i) {
        QString k = QString::number(i);
        if (!map.contains(k)) map.insert(k, positional.at(i));
    }
    return map;
}

QVariant parseValue(Ctx &c) {
    skipWs(c);
    if (c.i >= c.s.size()) return QVariant();
    QChar ch = c.s.at(c.i);
    if (ch == '"') return readString(c);
    if (ch == '{') return parseTable(c);
    if (ch == '(') { // e.g. (monitor_w*0.60) helper expressions, keep as string
        int start = c.i;
        int depth = 0;
        while (c.i < c.s.size()) {
            if (c.s.at(c.i) == '(') ++depth;
            else if (c.s.at(c.i) == ')') { --depth; if (depth == 0) { ++c.i; break; } }
            ++c.i;
        }
        return c.s.mid(start, c.i - start);
    }
    if (ch == '-' || ch == '+') {
        QString num = readNumber(c);
        if (!num.isEmpty()) return num.toDouble();
    }
    if (ch.isDigit() || ch == '.') {
        QString num = readNumber(c);
        if (!num.isEmpty()) return num.toDouble();
    }
    QString ident = readIdent(c);
    if (ident == "true") return true;
    if (ident == "false") return false;
    if (ident == "nil") return QVariant();
    return ident; // strings can appear unquoted in hyprland's lua (rare)
}

QStringList readLines(const QString &filePath) {
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QString text = QString::fromUtf8(f.readAll());
    return text.split('\n');
}

bool isList(const QVariant &v) {
    return v.metaType().id() == QMetaType::fromType<QList<QVariant>>().id();
}

bool isMap(const QVariant &v) {
    return v.metaType().id() == QMetaType::fromType<QVariantMap>().id();
}

QString quote(const QString &s) {
    QString out = "\"";
    for (const QChar &ch : s) {
        if (ch == '\\') out += "\\\\";
        else if (ch == '"') out += "\\\"";
        else if (ch == '\n') out += "\\n";
        else out += ch;
    }
    out += "\"";
    return out;
}

QString serializeValue(const QVariant &v, int depth);

QString serializeTable(const QVariantMap &m, int depth) {
    QString out = "{";
    const int childIndent = depth + 1;
    for (auto it = m.cbegin(); it != m.cend(); ++it) {
        QString line = QString(childIndent * 4, ' ') + it.key() + " = " + serializeValue(it.value(), childIndent);
        out += "\n" + line + ",";
    }
    if (!m.isEmpty()) out += "\n" + QString(depth * 4, ' ');
    out += "}";
    return out;
}

QString serializeList(const QList<QVariant> &list, int depth) {
    QString out = "{";
    const int childIndent = depth + 1;
    for (const QVariant &v : list) {
        out += "\n" + QString(childIndent * 4, ' ') + serializeValue(v, childIndent) + ",";
    }
    if (!list.isEmpty()) out += "\n" + QString(depth * 4, ' ');
    out += "}";
    return out;
}

QString serializeValue(const QVariant &v, int depth) {
    switch (v.typeId()) {
    case QMetaType::Bool:
        return v.toBool() ? "true" : "false";
    case QMetaType::Char: case QMetaType::UChar:
    case QMetaType::Short: case QMetaType::UShort:
    case QMetaType::Int: case QMetaType::UInt:
    case QMetaType::Long: case QMetaType::ULong:
    case QMetaType::LongLong: case QMetaType::ULongLong:
    case QMetaType::Float:
    case QMetaType::Double:
        return QString::number(v.toDouble());
    case QMetaType::QString:
        return quote(v.toString());
    default:
        break;
    }
    if (isList(v)) return serializeList(v.toList(), depth);
    if (isMap(v)) {
        if (v.toMap().isEmpty()) return "{}";
        return serializeTable(v.toMap(), depth);
    }
    return "nil";
}

} // namespace

void merge(QVariantMap &dst, const QVariantMap &src) {
    for (auto it = src.cbegin(); it != src.cend(); ++it) {
        const QVariant &srcV = it.value();
        auto dstIt = dst.find(it.key());
        if (dstIt != dst.end() && isMap(dstIt.value()) && isMap(srcV)) {
            QVariantMap sub = dstIt.value().toMap();
            merge(sub, srcV.toMap());
            dstIt.value() = sub;
        } else {
            dst.insert(it.key(), srcV);
        }
    }
}

QVariantMap parseHlConfigFile(const QString &filePath) {
    QVariantMap all;
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return all;
    QString text = QString::fromUtf8(f.readAll());
    Ctx c{text, 0};
    while (c.i < text.size()) {
        skipWs(c);
        if (c.i >= text.size()) break;
        QString ident = readIdent(c);
        if (ident == "hl" && c.s.mid(c.i, 7) == ".config") {
            c.i += 7;
            skipWs(c);
            if (c.i < text.size() && text.at(c.i) == '(') {
                ++c.i;
                skipWs(c);
                if (c.i < text.size() && text.at(c.i) == '{') {
                    QVariant tbl = parseTable(c);
                    QVariantMap m = tbl.toMap();
                    merge(all, m);
                }
            }
        } else {
            ++c.i;
        }
    }
    return all;
}

QMap<QString, QString> parseSimpleAssignments(const QString &filePath) {
    QMap<QString, QString> out;
    const QStringList lines = readLines(filePath);
    for (const QString &line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith("--")) continue;
        int eq = trimmed.indexOf('=');
        if (eq <= 0) continue;
        QString key = trimmed.left(eq).trimmed();
        bool ok = false;
        if (!isIdentStart(key.at(0))) continue;
        for (int i = 1; i < key.size(); ++i) {
            if (!isIdentChar(key.at(i))) { ok = false; break; }
            ok = true;
        }
        if (!ok && key.size() == 1) ok = isIdentStart(key.at(0));
        if (!ok) continue;
        QString val = trimmed.mid(eq + 1).trimmed();
        if (val.startsWith("\"") && val.endsWith("\"") && val.size() >= 2) {
            val = val.mid(1, val.size() - 2);
        }
        out.insert(key, val);
    }
    return out;
}

bool get(const QVariantMap &tree, const QStringList &parts, QVariant *out) {
    if (parts.isEmpty()) return false;
    auto it = tree.constFind(parts.first());
    if (it == tree.cend()) return false;
    const QVariant &v = it.value();
    if (parts.size() == 1) {
        if (out) *out = v;
        return true;
    }
    if (!isMap(v)) return false;
    return get(v.toMap(), parts.mid(1), out);
}

void setPath(QVariantMap &tree, const QStringList &parts, const QVariant &value) {
    if (parts.isEmpty()) return;
    if (parts.size() == 1) {
        tree.insert(parts.first(), value);
        return;
    }
    QVariantMap child = tree.value(parts.first()).toMap();
    setPath(child, parts.mid(1), value);
    tree.insert(parts.first(), child);
}

void remove(QVariantMap &tree, const QStringList &parts) {
    if (parts.isEmpty()) return;
    if (parts.size() == 1) {
        tree.remove(parts.first());
        return;
    }
    auto it = tree.find(parts.first());
    if (it == tree.end()) return;
    if (isMap(it.value())) {
        QVariantMap child = it.value().toMap();
        remove(child, parts.mid(1));
        if (child.isEmpty()) tree.erase(it);
        else it.value() = child;
    }
}

bool emptyLeafAt(const QVariantMap &tree, const QStringList &parts) {
    QVariant v;
    return lua::get(tree, parts, &v) && v.isNull();
}

bool isEmptyMap(const QVariantMap &tree) {
    return tree.isEmpty();
}

QString serializeHlConfigFile(const QVariantMap &tree, const QString &headerComment) {
    QStringList headerLines;
    for (const QString &l : headerComment.split('\n')) headerLines << ("-- " + l.trimmed());
    QString out = headerLines.join('\n') + "\n\n";
    if (tree.isEmpty()) {
        out += "hl.config({})\n";
        return out;
    }
    out += "hl.config(" + serializeTable(tree, 0) + ")\n";
    return out;
}

} // namespace lua