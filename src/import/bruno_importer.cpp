#include "bruno_importer.h"

#include "../storage/sqlite_manager.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QObject>
#include <QRegularExpression>

namespace {
QString section(const QString &source, const QString &name) {
    const QRegularExpression opening(QStringLiteral("(?m)^\\s*")
        + QRegularExpression::escape(name) + QStringLiteral(R"(\s*\{)"));
    const auto match = opening.match(source);
    if (!match.hasMatch())
        return {};
    int depth = 1;
    bool quoted = false;
    QChar quote;
    bool escaped = false;
    for (int index = match.capturedEnd(); index < source.size(); ++index) {
        const auto character = source.at(index);
        if (escaped) { escaped = false; continue; }
        if (character == QLatin1Char('\\')) { escaped = true; continue; }
        if (quoted) { if (character == quote) quoted = false; continue; }
        if (character == QLatin1Char('"') || character == QLatin1Char('\'')) {
            quoted = true; quote = character; continue;
        }
        if (character == QLatin1Char('{')) ++depth;
        else if (character == QLatin1Char('}') && --depth == 0)
            return source.mid(match.capturedEnd(), index - match.capturedEnd()).trimmed();
    }
    return {};
}

QMap<QString, QString> fields(const QString &block) {
    QMap<QString, QString> result;
    for (const auto &line : block.split(QLatin1Char('\n'))) {
        const auto colon = line.indexOf(QLatin1Char(':'));
        if (colon <= 0)
            continue;
        auto key = line.left(colon).trimmed();
        if (key.startsWith(QLatin1Char('~')))
            continue;
        result.insert(key, line.mid(colon + 1).trimmed());
    }
    return result;
}
}

ApiRequest BrunoImporter::parseFile(const QString &path, QString *name,
                                    QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return {};
    }
    const auto source = QString::fromUtf8(file.readAll());
    const auto meta = fields(section(source, QStringLiteral("meta")));
    if (name)
        *name = meta.value(QStringLiteral("name"), QFileInfo(path).completeBaseName());
    const QRegularExpression methodExpression(QStringLiteral(
        R"((?mi)^\s*(get|post|put|patch|delete|head|options|connect|trace)\s*\{)"));
    const auto methodMatch = methodExpression.match(source);
    if (!methodMatch.hasMatch()) {
        if (error) *error = QObject::tr("No HTTP request section was found in %1.")
                                .arg(QFileInfo(path).fileName());
        return {};
    }
    ApiRequest request;
    request.method = methodMatch.captured(1).toUpper();
    const auto requestFields = fields(section(source, methodMatch.captured(1)));
    request.url = requestFields.value(QStringLiteral("url"));
    const auto headerFields = fields(section(source, QStringLiteral("headers")));
    for (auto it = headerFields.cbegin(); it != headerFields.cend(); ++it)
        request.headers.append({it.key(), it.value()});
    const auto queryFields = fields(section(source, QStringLiteral("query")));
    for (auto it = queryFields.cbegin(); it != queryFields.cend(); ++it)
        request.params.append({it.key(), it.value()});
    const QStringList rawTypes{QStringLiteral("json"), QStringLiteral("xml"),
                               QStringLiteral("text"), QStringLiteral("sparql"),
                               QStringLiteral("graphql")};
    for (const auto &type : rawTypes) {
        const auto body = section(source, QStringLiteral("body:") + type);
        if (body.isEmpty())
            continue;
        request.bodyMode = RequestBodyMode::Raw;
        request.body = body.toUtf8();
        request.bodyContentType = type == QStringLiteral("json") ? QStringLiteral("application/json")
            : type == QStringLiteral("xml") ? QStringLiteral("application/xml")
            : QStringLiteral("text/plain");
        break;
    }
    const auto urlEncoded = fields(section(source, QStringLiteral("body:form-urlencoded")));
    if (!urlEncoded.isEmpty()) {
        request.bodyMode = RequestBodyMode::UrlEncoded;
        for (auto it = urlEncoded.cbegin(); it != urlEncoded.cend(); ++it)
            request.bodyEntries.append({it.key(), it.value()});
    }
    const auto multipart = fields(section(source, QStringLiteral("body:multipart-form")));
    if (!multipart.isEmpty()) {
        request.bodyMode = RequestBodyMode::Multipart;
        for (auto it = multipart.cbegin(); it != multipart.cend(); ++it) {
            RequestBodyEntry entry;
            entry.key = it.key(); entry.value = it.value();
            entry.isFile = entry.value.startsWith(QLatin1Char('@'));
            if (entry.isFile) { entry.filePath = entry.value.mid(1); entry.value.clear(); }
            request.bodyEntries.append(entry);
        }
    }
    const auto basic = fields(section(source, QStringLiteral("auth:basic")));
    if (!basic.isEmpty()) {
        const auto credentials = basic.value(QStringLiteral("username")) + QLatin1Char(':')
            + basic.value(QStringLiteral("password"));
        request.headers.append({QStringLiteral("Authorization"), QStringLiteral("Basic ")
            + QString::fromLatin1(credentials.toUtf8().toBase64())});
    }
    const auto bearer = fields(section(source, QStringLiteral("auth:bearer")));
    if (!bearer.isEmpty())
        request.headers.append({QStringLiteral("Authorization"), QStringLiteral("Bearer ")
            + bearer.value(QStringLiteral("token"))});
    if (request.body.isEmpty() && request.bodyEntries.isEmpty())
        request.bodyMode = RequestBodyMode::None;
    return request;
}

BrunoImportResult BrunoImporter::importDirectory(const QString &path,
                                                  SqliteManager &storage) {
    BrunoImportResult result;
    const QDir root(path);
    if (!root.exists()) {
        result.error = QObject::tr("The Bruno collection directory does not exist.");
        return result;
    }
    const auto rootId = storage.createFolder(root.dirName());
    if (rootId.isEmpty()) {
        result.error = QObject::tr("Could not create the Bruno collection.");
        return result;
    }
    QMap<QString, QString> folders{{QString(), rootId}};
    QDirIterator iterator(path, {QStringLiteral("*.bru")}, QDir::Files,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const auto filePath = iterator.next();
        const auto relativeDirectory = root.relativeFilePath(QFileInfo(filePath).path());
        QString parentId = rootId;
        QString accumulated;
        for (const auto &part : relativeDirectory.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
            accumulated = accumulated.isEmpty() ? part : accumulated + QLatin1Char('/') + part;
            if (!folders.contains(accumulated))
                folders.insert(accumulated, storage.createFolder(part, parentId));
            parentId = folders.value(accumulated);
        }
        QString name;
        QString error;
        const auto request = parseFile(filePath, &name, &error);
        if (!request.url.isEmpty() && storage.saveRequest(name, request, parentId))
            ++result.requestCount;
    }
    return result;
}
