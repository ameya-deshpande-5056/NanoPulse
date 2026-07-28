#include "swagger_importer.h"

#include "../network/request.h"
#include "../storage/sqlite_manager.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMap>
#include <QSet>

#include <optional>

namespace {
struct ImportedBody {
    QByteArray data;
    QString contentType;
};

QString baseUrl(const QJsonObject &root) {
    if (root.contains(QStringLiteral("openapi"))) {
        const auto servers = root.value(QStringLiteral("servers")).toArray();
        if (servers.isEmpty())
            return {};
        const auto server = servers.first().toObject();
        auto url = server.value(QStringLiteral("url")).toString();
        const auto variables = server.value(QStringLiteral("variables")).toObject();
        for (auto it = variables.begin(); it != variables.end(); ++it)
            url.replace(QStringLiteral("{%1}").arg(it.key()),
                        it.value().toObject()
                            .value(QStringLiteral("default")).toString());
        return url;
    }
    const auto host = root.value(QStringLiteral("host")).toString();
    const auto path = root.value(QStringLiteral("basePath")).toString();
    if (host.isEmpty())
        return path;
    const auto schemes = root.value(QStringLiteral("schemes")).toArray();
    const auto scheme = schemes.isEmpty() ? QStringLiteral("http")
                                           : schemes.first().toString();
    return scheme + QStringLiteral("://") + host + path;
}

QJsonArray combinedParameters(const QJsonObject &pathObject,
                              const QJsonObject &operation) {
    QJsonArray result = pathObject.value(QStringLiteral("parameters")).toArray();
    for (const auto &parameter :
         operation.value(QStringLiteral("parameters")).toArray())
        result.append(parameter);
    return result;
}

QString endpointUrl(QString base, const QString &path) {
    if (base.endsWith(QLatin1Char('/')) && path.startsWith(QLatin1Char('/')))
        base.chop(1);
    else if (!base.endsWith(QLatin1Char('/'))
             && !path.startsWith(QLatin1Char('/')))
        base.append(QLatin1Char('/'));
    return base + path;
}

QJsonValue resolveReference(const QJsonObject &root, QJsonValue value) {
    for (int depth = 0; depth < 32 && value.isObject(); ++depth) {
        const auto reference =
            value.toObject().value(QStringLiteral("$ref")).toString();
        if (!reference.startsWith(QStringLiteral("#/")))
            break;
        value = root;
        const auto parts = reference.mid(2).split(QLatin1Char('/'));
        for (auto part : parts) {
            part.replace(QStringLiteral("~1"), QStringLiteral("/"));
            part.replace(QStringLiteral("~0"), QStringLiteral("~"));
            if (!value.isObject())
                return {};
            value = value.toObject().value(part);
        }
    }
    return value;
}

std::optional<QJsonValue> schemaExample(const QJsonObject &root,
                                        QJsonValue schema, int depth = 0) {
    if (depth >= 32)
        return {};
    schema = resolveReference(root, schema);
    if (!schema.isObject())
        return {};
    const auto object = schema.toObject();
    if (object.contains(QStringLiteral("example")))
        return object.value(QStringLiteral("example"));
    if (object.contains(QStringLiteral("default")))
        return object.value(QStringLiteral("default"));

    const auto values = object.value(QStringLiteral("enum")).toArray();
    if (!values.isEmpty())
        return values.first();

    const auto properties = object.value(QStringLiteral("properties")).toObject();
    if (!properties.isEmpty()) {
        QJsonObject result;
        for (auto it = properties.begin(); it != properties.end(); ++it) {
            const auto example = schemaExample(root, it.value(), depth + 1);
            if (example)
                result.insert(it.key(), *example);
        }
        if (!result.isEmpty())
            return result;
    }

    const auto item = schemaExample(
        root, object.value(QStringLiteral("items")), depth + 1);
    if (item)
        return QJsonArray{*item};
    return {};
}

std::optional<QJsonValue> mediaExample(const QJsonObject &root,
                                       QJsonValue mediaValue) {
    mediaValue = resolveReference(root, mediaValue);
    if (!mediaValue.isObject())
        return {};
    const auto media = mediaValue.toObject();
    if (media.contains(QStringLiteral("example")))
        return media.value(QStringLiteral("example"));

    const auto examples = media.value(QStringLiteral("examples")).toObject();
    for (auto it = examples.begin(); it != examples.end(); ++it) {
        const auto example = resolveReference(root, it.value());
        if (example.isObject()
            && example.toObject().contains(QStringLiteral("value")))
            return example.toObject().value(QStringLiteral("value"));
    }
    return schemaExample(root, media.value(QStringLiteral("schema")));
}

QByteArray encodeExample(const QJsonValue &example,
                         const QString &contentType) {
    if (example.isObject())
        return QJsonDocument(example.toObject()).toJson(QJsonDocument::Indented);
    if (example.isArray())
        return QJsonDocument(example.toArray()).toJson(QJsonDocument::Indented);
    if (example.isString() && !contentType.contains(QStringLiteral("json"),
                                                    Qt::CaseInsensitive))
        return example.toString().toUtf8();
    auto encoded =
        QJsonDocument(QJsonArray{example}).toJson(QJsonDocument::Compact);
    return encoded.mid(1, encoded.size() - 2);
}

ImportedBody openApiBody(const QJsonObject &root,
                         const QJsonObject &operation) {
    auto requestBody =
        resolveReference(root, operation.value(QStringLiteral("requestBody")));
    if (!requestBody.isObject())
        return {};
    const auto content =
        requestBody.toObject().value(QStringLiteral("content")).toObject();
    if (content.isEmpty())
        return {};

    auto contentType = QStringLiteral("application/json");
    if (!content.contains(contentType)) {
        contentType.clear();
        for (auto it = content.begin(); it != content.end(); ++it) {
            if (it.key().contains(QStringLiteral("json"),
                                  Qt::CaseInsensitive)) {
                contentType = it.key();
                break;
            }
        }
        if (contentType.isEmpty())
            contentType = content.begin().key();
    }
    const auto example = mediaExample(root, content.value(contentType));
    return example ? ImportedBody{encodeExample(*example, contentType),
                                  contentType}
                   : ImportedBody{};
}

QString swaggerContentType(const QJsonObject &root,
                           const QJsonObject &operation) {
    auto consumes = operation.value(QStringLiteral("consumes")).toArray();
    if (consumes.isEmpty())
        consumes = root.value(QStringLiteral("consumes")).toArray();
    return consumes.isEmpty() ? QStringLiteral("application/json")
                              : consumes.first().toString();
}

ImportedBody swaggerBody(const QJsonObject &root,
                         const QJsonObject &pathObject,
                         const QJsonObject &operation) {
    const auto contentType = swaggerContentType(root, operation);
    for (const auto &value : combinedParameters(pathObject, operation)) {
        const auto resolved = resolveReference(root, value);
        if (!resolved.isObject())
            continue;
        const auto parameter = resolved.toObject();
        if (parameter.value(QStringLiteral("in")).toString()
            != QStringLiteral("body"))
            continue;
        std::optional<QJsonValue> example;
        if (parameter.contains(QStringLiteral("x-example")))
            example = parameter.value(QStringLiteral("x-example"));
        else
            example =
                schemaExample(root, parameter.value(QStringLiteral("schema")));
        if (example)
            return {encodeExample(*example, contentType), contentType};
    }
    return {};
}
}

SwaggerDocument SwaggerImporter::parseFile(const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return {{}, {}, {}, file.errorString()};

    QByteArray owned;
    uchar *mapped = nullptr;
    const auto size = file.size();
    if (size > 10 * 1024 * 1024)
        mapped = file.map(0, size);
    const auto bytes = mapped
        ? QByteArray::fromRawData(reinterpret_cast<const char *>(mapped),
                                  static_cast<qsizetype>(size))
        : (owned = file.readAll());

    const auto result = parse(bytes, QFileInfo(filePath).baseName());
    if (mapped)
        file.unmap(mapped);
    return result;
}

SwaggerDocument SwaggerImporter::parse(const QByteArray &data,
                                       const QString &sourceName) {
    QJsonParseError parseError;
    const auto parsed = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return {{}, {}, {}, parseError.errorString()};
    if (!parsed.isObject())
        return {{}, {}, {},
                QStringLiteral("The document root must be a JSON object")};

    const auto root = parsed.object();
    if (!root.contains(QStringLiteral("swagger"))
        && !root.contains(QStringLiteral("openapi")))
        return {{}, {}, {},
                QStringLiteral("Not a Swagger 2.0 or OpenAPI 3.x document")};

    const auto title = root.value(QStringLiteral("info")).toObject()
                           .value(QStringLiteral("title"))
                           .toString(sourceName);
    return {root, title, baseUrl(root), {}};
}

SwaggerImportResult SwaggerImporter::importDocument(
    const SwaggerDocument &document, const QString &baseUrl,
    SqliteManager &storage) {
    if (!document.error.isEmpty())
        return {0, document.error};
    const auto collectionId = storage.createFolder(document.title);
    if (collectionId.isEmpty())
        return {0, QStringLiteral("Could not create collection")};

    QMap<QString, QString> tagFolders;
    const auto rootBase = baseUrl;
    const QSet<QString> methods{
        QStringLiteral("get"), QStringLiteral("post"), QStringLiteral("put"),
        QStringLiteral("patch"), QStringLiteral("delete"), QStringLiteral("head"),
        QStringLiteral("options")};
    int count = 0;
    const auto paths = document.root.value(QStringLiteral("paths")).toObject();
    for (auto pathIt = paths.begin(); pathIt != paths.end(); ++pathIt) {
        const auto pathObject = pathIt.value().toObject();
        for (auto operationIt = pathObject.begin();
             operationIt != pathObject.end(); ++operationIt) {
            if (!methods.contains(operationIt.key().toLower())
                || !operationIt.value().isObject())
                continue;
            const auto operation = operationIt.value().toObject();
            const auto tags = operation.value(QStringLiteral("tags")).toArray();
            const auto tag = tags.isEmpty() ? QStringLiteral("Untagged")
                                             : tags.first().toString();
            if (!tagFolders.contains(tag))
                tagFolders.insert(tag, storage.createFolder(tag, collectionId));

            ApiRequest request;
            request.method = operationIt.key().toUpper();
            request.url = endpointUrl(rootBase, pathIt.key());
            for (const auto &value : combinedParameters(pathObject, operation)) {
                const auto parameter =
                    resolveReference(document.root, value).toObject();
                const auto name = parameter.value(QStringLiteral("name")).toString();
                const auto location = parameter.value(QStringLiteral("in")).toString();
                const auto placeholder = QStringLiteral("{{%1}}").arg(name);
                if (location == QStringLiteral("query"))
                    request.params.append({name, placeholder});
                else if (location == QStringLiteral("path"))
                    request.url.replace(QStringLiteral("{%1}").arg(name), placeholder);
            }
            request.headers.append(
                {QStringLiteral("Accept"), QStringLiteral("application/json")});
            const auto body = document.root.contains(QStringLiteral("openapi"))
                ? openApiBody(document.root, operation)
                : swaggerBody(document.root, pathObject, operation);
            if (!body.data.isEmpty()) {
                request.body = body.data;
                request.headers.append(
                    {QStringLiteral("Content-Type"), body.contentType});
            }
            const auto name = operation.value(QStringLiteral("summary"))
                                  .toString(operation.value(QStringLiteral("operationId"))
                                                .toString(request.method
                                                          + QStringLiteral(" ")
                                                          + pathIt.key()));
            if (storage.saveRequest(name, request, tagFolders.value(tag)))
                ++count;
        }
    }
    return {count, {}};
}
