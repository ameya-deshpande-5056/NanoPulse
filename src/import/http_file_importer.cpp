#include "http_file_importer.h"

#include <QMap>
#include <QObject>
#include <QRegularExpression>

namespace {
QString substitute(QString value, const QMap<QString, QString> &variables) {
    for (auto it = variables.cbegin(); it != variables.cend(); ++it)
        value.replace(QStringLiteral("{{%1}}").arg(it.key()), it.value());
    return value;
}

ApiRequest parseBlock(const QStringList &lines, const QMap<QString, QString> &variables) {
    ApiRequest request;
    int lineIndex = 0;
    while (lineIndex < lines.size()) {
        const auto line = lines.at(lineIndex).trimmed();
        if (!line.isEmpty() && !line.startsWith(QLatin1Char('#'))
            && !line.startsWith(QStringLiteral("//")))
            break;
        ++lineIndex;
    }
    if (lineIndex >= lines.size())
        return request;
    const auto first = lines.at(lineIndex).trimmed();
    const QRegularExpression requestLine(QStringLiteral(
        R"(^([A-Za-z][A-Za-z0-9_-]*)\s+(\S+)(?:\s+HTTP/\d(?:\.\d)?)?$)"));
    const auto match = requestLine.match(first);
    if (match.hasMatch()) {
        request.method = match.captured(1).toUpper();
        request.url = substitute(match.captured(2), variables);
    } else if (first.startsWith(QStringLiteral("http://"))
               || first.startsWith(QStringLiteral("https://"))) {
        request.method = QStringLiteral("GET");
        request.url = substitute(first, variables);
    } else {
        return request;
    }
    ++lineIndex;
    for (; lineIndex < lines.size(); ++lineIndex) {
        const auto line = lines.at(lineIndex);
        if (line.trimmed().isEmpty()) { ++lineIndex; break; }
        if (line.startsWith(QLatin1Char('#')) || line.startsWith(QStringLiteral("//")))
            continue;
        const auto colon = line.indexOf(QLatin1Char(':'));
        if (colon > 0)
            request.headers.append({line.left(colon).trimmed(),
                                    substitute(line.mid(colon + 1).trimmed(), variables)});
    }
    QStringList bodyLines;
    for (; lineIndex < lines.size(); ++lineIndex)
        bodyLines.append(lines.at(lineIndex));
    while (!bodyLines.isEmpty() && bodyLines.constLast().trimmed().isEmpty())
        bodyLines.removeLast();
    if (!bodyLines.isEmpty()) {
        request.bodyMode = RequestBodyMode::Raw;
        request.body = substitute(bodyLines.join(QLatin1Char('\n')), variables).toUtf8();
        request.bodyContentType = QStringLiteral("text/plain");
        for (const auto &header : request.headers) {
            if (header.first.compare(QStringLiteral("Content-Type"), Qt::CaseInsensitive) == 0) {
                request.bodyContentType = header.second;
                break;
            }
        }
    } else {
        request.bodyMode = RequestBodyMode::None;
    }
    return request;
}
}

QList<ApiRequest> HttpFileImporter::parse(const QString &source, QString *error) {
    QMap<QString, QString> variables;
    QStringList filtered;
    const QRegularExpression variableLine(QStringLiteral(R"(^\s*@([\w.-]+)\s*=\s*(.*)$)"));
    for (const auto &line : source.split(QLatin1Char('\n'))) {
        const auto match = variableLine.match(line);
        if (match.hasMatch())
            variables.insert(match.captured(1), match.captured(2).trimmed());
        else
            filtered.append(line);
    }
    QList<ApiRequest> result;
    QStringList block;
    const auto flush = [&] {
        const auto request = parseBlock(block, variables);
        if (!request.url.isEmpty())
            result.append(request);
        block.clear();
    };
    for (const auto &line : filtered) {
        if (line.trimmed().startsWith(QStringLiteral("###")))
            flush();
        else
            block.append(line);
    }
    flush();
    if (result.isEmpty() && error)
        *error = QObject::tr("No valid HTTP requests were found in the file.");
    return result;
}
