#include "postman_importer.h"

#include "../network/request.h"
#include "../storage/sqlite_manager.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QUrl>
#include <QUrlQuery>

namespace {
QString requestUrl(const QJsonValue &value) {
    if (value.isString())
        return value.toString();
    const auto object = value.toObject();
    const auto raw = object.value(QStringLiteral("raw")).toString();
    if (!raw.isEmpty())
        return raw;
    QString url = object.value(QStringLiteral("protocol")).toString();
    const auto host = object.value(QStringLiteral("host")).toVariant().toStringList().join(QLatin1Char('.'));
    if (!url.isEmpty())
        url += QStringLiteral("://");
    url += host;
    const auto path = object.value(QStringLiteral("path")).toVariant().toStringList().join(QLatin1Char('/'));
    if (!path.isEmpty())
        url += QLatin1Char('/') + path;
    return url;
}

void applyAuth(const QJsonObject &auth, ApiRequest *request) {
    const auto type = auth.value(QStringLiteral("type")).toString();
    const auto values = auth.value(type).toArray();
    QMap<QString, QString> entries;
    for (const auto &value : values) {
        const auto object = value.toObject();
        entries.insert(object.value(QStringLiteral("key")).toString(),
                       object.value(QStringLiteral("value")).toString());
    }
    if (type == QStringLiteral("basic")) {
        request->headers.append({QStringLiteral("Authorization"), QStringLiteral("Basic ")
            + QString::fromLatin1((entries.value(QStringLiteral("username")) + QLatin1Char(':')
                + entries.value(QStringLiteral("password"))).toUtf8().toBase64())});
    } else if (type == QStringLiteral("bearer")) {
        request->headers.append({QStringLiteral("Authorization"),
                                 QStringLiteral("Bearer ") + entries.value(QStringLiteral("token"))});
    } else if (type == QStringLiteral("apikey")) {
        const auto key = entries.value(QStringLiteral("key"));
        const auto value = entries.value(QStringLiteral("value"));
        if (entries.value(QStringLiteral("in")) == QStringLiteral("query"))
            request->params.append({key, value});
        else
            request->headers.append({key, value});
    }
}

ApiRequest parseRequest(const QJsonObject &item) {
    const auto object = item.value(QStringLiteral("request")).toObject();
    ApiRequest request;
    request.method = object.value(QStringLiteral("method")).toString(QStringLiteral("GET"));
    request.url = requestUrl(object.value(QStringLiteral("url")));
    for (const auto &value : object.value(QStringLiteral("header")).toArray()) {
        const auto header = value.toObject();
        if (!header.value(QStringLiteral("disabled")).toBool())
            request.headers.append({header.value(QStringLiteral("key")).toString(),
                                    header.value(QStringLiteral("value")).toString()});
    }
    const auto url = object.value(QStringLiteral("url")).toObject();
    for (const auto &value : url.value(QStringLiteral("query")).toArray()) {
        const auto query = value.toObject();
        if (!query.value(QStringLiteral("disabled")).toBool())
            request.params.append({query.value(QStringLiteral("key")).toString(),
                                   query.value(QStringLiteral("value")).toString()});
    }
    const auto body = object.value(QStringLiteral("body")).toObject();
    const auto mode = body.value(QStringLiteral("mode")).toString();
    if (mode == QStringLiteral("urlencoded")) {
        request.bodyMode = RequestBodyMode::UrlEncoded;
        request.bodyContentType = QStringLiteral("application/x-www-form-urlencoded");
        for (const auto &value : body.value(mode).toArray()) {
            const auto part = value.toObject();
            request.bodyEntries.append({part.value(QStringLiteral("key")).toString(),
                                        part.value(QStringLiteral("value")).toString(), {}, {}, false,
                                        !part.value(QStringLiteral("disabled")).toBool()});
        }
    } else if (mode == QStringLiteral("formdata")) {
        request.bodyMode = RequestBodyMode::Multipart;
        request.bodyContentType = QStringLiteral("multipart/form-data");
        for (const auto &value : body.value(mode).toArray()) {
            const auto part = value.toObject();
            const bool file = part.value(QStringLiteral("type")).toString() == QStringLiteral("file");
            request.bodyEntries.append({part.value(QStringLiteral("key")).toString(),
                                        file ? QString() : part.value(QStringLiteral("value")).toString(),
                                        file ? part.value(QStringLiteral("src")).toString() : QString(),
                                        part.value(QStringLiteral("contentType")).toString(), file,
                                        !part.value(QStringLiteral("disabled")).toBool()});
        }
    } else if (mode == QStringLiteral("file")) {
        request.bodyMode = RequestBodyMode::Binary;
        request.binaryFilePath = body.value(mode).toObject().value(QStringLiteral("src")).toString();
        request.bodyContentType = QStringLiteral("application/octet-stream");
    } else if (mode == QStringLiteral("raw")) {
        request.bodyMode = RequestBodyMode::Raw;
        request.body = body.value(QStringLiteral("raw")).toString().toUtf8();
        const auto language = body.value(QStringLiteral("options")).toObject()
                                  .value(QStringLiteral("raw")).toObject()
                                  .value(QStringLiteral("language")).toString();
        request.bodyContentType = language == QStringLiteral("json") ? QStringLiteral("application/json")
            : language == QStringLiteral("xml") ? QStringLiteral("application/xml")
            : language == QStringLiteral("html") ? QStringLiteral("text/html")
            : language == QStringLiteral("javascript") ? QStringLiteral("application/javascript")
            : QStringLiteral("text/plain");
    } else {
        request.bodyMode = RequestBodyMode::None;
    }
    applyAuth(object.value(QStringLiteral("auth")).toObject(), &request);
    return request;
}

void importItems(const QJsonArray &items, const QString &parentId, SqliteManager &storage,
                 PostmanImportResult *result) {
    for (const auto &value : items) {
        const auto item = value.toObject();
        const auto children = item.value(QStringLiteral("item")).toArray();
        if (!children.isEmpty()) {
            const auto folder = storage.createFolder(item.value(QStringLiteral("name")).toString(), parentId);
            if (!folder.isEmpty())
                importItems(children, folder, storage, result);
        } else if (item.value(QStringLiteral("request")).isObject()) {
            if (storage.saveRequest(item.value(QStringLiteral("name")).toString(), parseRequest(item), parentId))
                ++result->requestCount;
        }
    }
}
}

bool PostmanImporter::isCollection(const QJsonObject &root) {
    return root.value(QStringLiteral("info")).toObject().value(QStringLiteral("schema"))
        .toString().contains(QStringLiteral("postman_collection"), Qt::CaseInsensitive);
}

PostmanImportResult PostmanImporter::importCollection(const QJsonObject &root,
                                                       SqliteManager &storage) {
    PostmanImportResult result;
    if (!isCollection(root)) {
        result.error = QObject::tr("This is not a Postman Collection v2.1 file.");
        return result;
    }
    const auto name = root.value(QStringLiteral("info")).toObject().value(QStringLiteral("name")).toString();
    const auto folder = storage.createFolder(name.isEmpty() ? QObject::tr("Imported Postman Collection") : name);
    if (folder.isEmpty()) {
        result.error = QObject::tr("Could not create the imported collection.");
        return result;
    }
    importItems(root.value(QStringLiteral("item")).toArray(), folder, storage, &result);
    return result;
}
