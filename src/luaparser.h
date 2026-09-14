#pragma once

#include <QString>
#include <QVariant>
#include <QVariantMap>

namespace lua {

// Parse all `hl.config({ ... })` blocks in a file and deep-merge them into a single nested map.
QVariantMap parseHlConfigFile(const QString &filePath);

// Parse top-level lines of the form: name = "value" (used for variables.lua).
QMap<QString, QString> parseSimpleAssignments(const QString &filePath);

// Deep merge src into dst (recurses into nested maps).
void merge(QVariantMap &dst, const QVariantMap &src);

// Dotted-path tree helpers.
bool get(const QVariantMap &tree, const QStringList &parts, QVariant *out);
void setPath(QVariantMap &tree, const QStringList &parts, const QVariant &value);
void remove(QVariantMap &tree, const QStringList &parts);
bool emptyLeafAt(const QVariantMap &tree, const QStringList &parts);
bool isEmptyMap(const QVariantMap &tree);

// Serialize the wrapper file: header comment + hl.config({ ... })
QString serializeHlConfigFile(const QVariantMap &tree, const QString &headerComment);

} // namespace lua