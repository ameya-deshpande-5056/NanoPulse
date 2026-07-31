#include "simple_yaml.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

namespace {
struct Line { int indent; QString text; int number; };

int separator(const QString &text, QChar wanted) {
    QChar quote;
    int square = 0, curly = 0;
    for (int i = 0; i < text.size(); ++i) {
        const auto c = text.at(i);
        if (!quote.isNull()) { if (c == quote && (i == 0 || text.at(i - 1) != QLatin1Char('\\'))) quote = {}; continue; }
        if (c == QLatin1Char('\'') || c == QLatin1Char('"')) { quote = c; continue; }
        if (c == QLatin1Char('[')) ++square; else if (c == QLatin1Char(']')) --square;
        else if (c == QLatin1Char('{')) ++curly; else if (c == QLatin1Char('}')) --curly;
        else if (c == wanted && square == 0 && curly == 0) return i;
    }
    return -1;
}

QStringList splitInline(const QString &text) {
    QStringList result; int start = 0;
    for (int cursor = 0; cursor < text.size();) {
        const auto comma = separator(text.mid(cursor), QLatin1Char(','));
        if (comma < 0) break;
        result.append(text.mid(start, cursor + comma - start).trimmed());
        cursor += comma + 1; start = cursor;
    }
    result.append(text.mid(start).trimmed());
    return result;
}

QJsonValue scalar(QString value) {
    value = value.trimmed();
    if ((value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
        || (value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\''))))
        return value.mid(1, value.size() - 2);
    if (value == QStringLiteral("null") || value == QStringLiteral("~")) return {};
    if (value == QStringLiteral("true")) return true;
    if (value == QStringLiteral("false")) return false;
    bool integerOk = false; const auto integer = value.toLongLong(&integerOk);
    if (integerOk) return integer;
    bool numberOk = false; const auto number = value.toDouble(&numberOk);
    if (numberOk) return number;
    if (value.startsWith(QLatin1Char('[')) && value.endsWith(QLatin1Char(']'))) {
        QJsonArray array;
        for (const auto &part : splitInline(value.mid(1, value.size() - 2)))
            if (!part.isEmpty()) array.append(scalar(part));
        return array;
    }
    if (value.startsWith(QLatin1Char('{')) && value.endsWith(QLatin1Char('}'))) {
        QJsonObject object;
        for (const auto &part : splitInline(value.mid(1, value.size() - 2))) {
            const auto colon = separator(part, QLatin1Char(':'));
            if (colon > 0) object.insert(part.left(colon).trimmed(), scalar(part.mid(colon + 1)));
        }
        return object;
    }
    return value;
}

QJsonValue block(const QList<Line> &lines, int *index, int indent, QString *error);

void mapEntry(QJsonObject *object, const QList<Line> &lines, int *index,
              int indent, const QString &text, QString *error) {
    const auto colon = separator(text, QLatin1Char(':'));
    if (colon <= 0) { if (error) *error = QStringLiteral("YAML line %1 has no mapping colon").arg(lines.at(*index).number); ++*index; return; }
    auto key = text.left(colon).trimmed();
    if ((key.startsWith(QLatin1Char('"')) && key.endsWith(QLatin1Char('"')))
        || (key.startsWith(QLatin1Char('\'')) && key.endsWith(QLatin1Char('\'')))) key = key.mid(1, key.size() - 2);
    const auto remainder = text.mid(colon + 1).trimmed();
    ++*index;
    if (remainder == QStringLiteral("|") || remainder == QStringLiteral(">")) {
        QStringList textLines;
        while (*index < lines.size() && lines.at(*index).indent > indent)
            textLines.append(lines.at((*index)++).text);
        object->insert(key, textLines.join(remainder == QStringLiteral(">") ? QLatin1Char(' ') : QLatin1Char('\n')));
    } else if (!remainder.isEmpty()) {
        object->insert(key, scalar(remainder));
    } else if (*index < lines.size() && lines.at(*index).indent > indent) {
        object->insert(key, block(lines, index, lines.at(*index).indent, error));
    } else {
        object->insert(key, QJsonObject{});
    }
}

QJsonValue block(const QList<Line> &lines, int *index, int indent, QString *error) {
    const bool sequence = lines.at(*index).indent == indent
        && lines.at(*index).text.startsWith(QStringLiteral("- "));
    if (sequence) {
        QJsonArray array;
        while (*index < lines.size() && lines.at(*index).indent == indent
               && lines.at(*index).text.startsWith(QLatin1Char('-'))) {
            const auto rest = lines.at(*index).text.mid(1).trimmed();
            if (rest.isEmpty()) {
                ++*index;
                array.append(*index < lines.size() && lines.at(*index).indent > indent
                                 ? block(lines, index, lines.at(*index).indent, error) : QJsonValue{});
            } else if (separator(rest, QLatin1Char(':')) > 0) {
                QJsonObject object;
                mapEntry(&object, lines, index, indent, rest, error);
                if (*index < lines.size() && lines.at(*index).indent > indent) {
                    const auto extra = block(lines, index, lines.at(*index).indent, error).toObject();
                    for (auto it = extra.begin(); it != extra.end(); ++it) object.insert(it.key(), it.value());
                }
                array.append(object);
            } else {
                array.append(scalar(rest)); ++*index;
            }
        }
        return array;
    }
    QJsonObject object;
    while (*index < lines.size() && lines.at(*index).indent == indent
           && !lines.at(*index).text.startsWith(QLatin1Char('-'))) {
        const auto text = lines.at(*index).text;
        mapEntry(&object, lines, index, indent, text, error);
        if (error && !error->isEmpty()) break;
    }
    return object;
}
}

QJsonDocument SimpleYaml::parse(const QByteArray &data, QString *error) {
    QList<Line> lines; int number = 0;
    for (auto raw : QString::fromUtf8(data).split(QLatin1Char('\n'))) {
        ++number; if (raw.endsWith(QLatin1Char('\r'))) raw.chop(1);
        int indent = 0; while (indent < raw.size() && raw.at(indent) == QLatin1Char(' ')) ++indent;
        auto text = raw.mid(indent);
        if (text.trimmed().isEmpty() || text.trimmed().startsWith(QLatin1Char('#'))
            || text.trimmed() == QStringLiteral("---") || text.trimmed() == QStringLiteral("...")) continue;
        const auto comment = separator(text, QLatin1Char('#'));
        if (comment >= 0 && (comment == 0 || text.at(comment - 1).isSpace())) text = text.left(comment).trimmed();
        lines.append({indent, text, number});
    }
    if (lines.isEmpty()) { if (error) *error = QStringLiteral("Empty YAML document"); return {}; }
    int index = 0;
    const auto root = block(lines, &index, lines.first().indent, error);
    return root.isObject() ? QJsonDocument(root.toObject()) : QJsonDocument(root.toArray());
}
