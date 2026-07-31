#include "curl_importer.h"

#include <QObject>

namespace {
QStringList tokens(const QString &input) {
    QStringList result;
    QString token;
    QChar quote;
    bool escaped = false;
    for (const auto character : input) {
        if (escaped) { token += character; escaped = false; continue; }
        if (character == QLatin1Char('\\') && quote != QLatin1Char('\'')) { escaped = true; continue; }
        if (!quote.isNull()) { if (character == quote) quote = {}; else token += character; continue; }
        if (character == QLatin1Char('\'') || character == QLatin1Char('"')) { quote = character; continue; }
        if (character.isSpace()) { if (!token.isEmpty()) { result += token; token.clear(); } continue; }
        token += character;
    }
    if (!token.isEmpty()) result += token;
    return result;
}
}

ApiRequest CurlImporter::parse(const QString &command, QString *error) {
    ApiRequest request;
    const auto args = tokens(command);
    if (args.isEmpty() || args.first() != QStringLiteral("curl")) {
        if (error) *error = QObject::tr("Paste a cURL command beginning with ‘curl’.");
        return {};
    }
    QStringList rawData;
    QList<RequestBodyEntry> form;
    for (int index = 1; index < args.size(); ++index) {
        const auto argument = args.at(index);
        const auto next = [&]() { return ++index < args.size() ? args.at(index) : QString(); };
        if (argument == QStringLiteral("-X") || argument == QStringLiteral("--request")) request.method = next().toUpper();
        else if (argument == QStringLiteral("-H") || argument == QStringLiteral("--header")) {
            const auto header = next(); const auto colon = header.indexOf(QLatin1Char(':'));
            if (colon > 0) request.headers.append({header.left(colon).trimmed(), header.mid(colon + 1).trimmed()});
        } else if (argument == QStringLiteral("-u") || argument == QStringLiteral("--user")) {
            request.headers.append({QStringLiteral("Authorization"), QStringLiteral("Basic ") + QString::fromLatin1(next().toUtf8().toBase64())});
        } else if (argument == QStringLiteral("-F") || argument == QStringLiteral("--form")) {
            const auto part = next(); const auto equal = part.indexOf(QLatin1Char('='));
            if (equal > 0) { RequestBodyEntry entry; entry.key = part.left(equal); entry.value = part.mid(equal + 1);
                entry.isFile = entry.value.startsWith(QLatin1Char('@')); if (entry.isFile) { entry.filePath = entry.value.mid(1); entry.value.clear(); } form += entry; }
        } else if (argument == QStringLiteral("--data-urlencode")) {
            const auto part = next(); const auto equal = part.indexOf(QLatin1Char('='));
            request.bodyEntries.append({equal < 0 ? part : part.left(equal), equal < 0 ? QString() : part.mid(equal + 1)});
            request.bodyMode = RequestBodyMode::UrlEncoded;
        } else if (argument == QStringLiteral("-d") || argument == QStringLiteral("--data") || argument == QStringLiteral("--data-raw")) rawData += next();
        else if (argument == QStringLiteral("--url")) request.url = next();
        else if (!argument.startsWith(QLatin1Char('-')) && (argument.startsWith(QStringLiteral("http://")) || argument.startsWith(QStringLiteral("https://")))) request.url = argument;
    }
    if (!form.isEmpty()) { request.bodyMode = RequestBodyMode::Multipart; request.bodyEntries = form; request.bodyContentType = QStringLiteral("multipart/form-data"); }
    else if (!rawData.isEmpty()) { request.bodyMode = RequestBodyMode::Raw; request.body = rawData.join(QLatin1Char('&')).toUtf8(); request.bodyContentType = QStringLiteral("text/plain"); if (request.method == QStringLiteral("GET")) request.method = QStringLiteral("POST"); }
    if (request.url.isEmpty() && error) *error = QObject::tr("The cURL command has no HTTP URL.");
    return request;
}
