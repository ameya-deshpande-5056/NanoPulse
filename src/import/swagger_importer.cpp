#include "swagger_importer.h"

#include "../network/request.h"
#include "../storage/sqlite_manager.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMap>
#include <QSet>
#include <QUrlQuery>

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

std::optional<QJsonValue> namedExample(const QJsonObject &root,
                                       QJsonValue examples,
                                       bool allowRawObjects = false) {
    if (examples.isArray())
        return examples.toArray().isEmpty()
            ? std::optional<QJsonValue>{}
            : std::optional<QJsonValue>{examples.toArray().first()};
    if (!examples.isObject())
        return {};
    for (auto it = examples.toObject().begin();
         it != examples.toObject().end(); ++it) {
        const auto example = resolveReference(root, it.value());
        if (example.isObject()
            && example.toObject().contains(QStringLiteral("value")))
            return example.toObject().value(QStringLiteral("value"));
        if ((!example.isObject() || allowRawObjects)
            && !example.isUndefined() && !example.isNull())
            return example;
    }
    return {};
}

std::optional<QJsonValue> schemaExample(const QJsonObject &root,
                                        QJsonValue schema, int depth = 0) {
    if (depth >= 32)
        return {};
    if (!schema.isObject())
        return {};
    auto object = schema.toObject();
    if (object.contains(QStringLiteral("example")))
        return object.value(QStringLiteral("example"));
    if (object.contains(QStringLiteral("default")))
        return object.value(QStringLiteral("default"));
    if (object.contains(QStringLiteral("const")))
        return object.value(QStringLiteral("const"));
    if (const auto example = namedExample(
            root, object.value(QStringLiteral("examples"))))
        return example;
    if (object.contains(QStringLiteral("x-example")))
        return object.value(QStringLiteral("x-example"));
    if (const auto example = namedExample(
            root, object.value(QStringLiteral("x-examples")), true))
        return example;

    schema = resolveReference(root, schema);
    if (!schema.isObject())
        return {};
    object = schema.toObject();
    if (object.contains(QStringLiteral("example")))
        return object.value(QStringLiteral("example"));
    if (object.contains(QStringLiteral("default")))
        return object.value(QStringLiteral("default"));
    if (object.contains(QStringLiteral("const")))
        return object.value(QStringLiteral("const"));
    if (const auto example = namedExample(
            root, object.value(QStringLiteral("examples"))))
        return example;
    if (object.contains(QStringLiteral("x-example")))
        return object.value(QStringLiteral("x-example"));
    if (const auto example = namedExample(
            root, object.value(QStringLiteral("x-examples")), true))
        return example;

    const auto values = object.value(QStringLiteral("enum")).toArray();
    if (!values.isEmpty())
        return values.first();

    QJsonObject combined;
    for (const auto &part : object.value(QStringLiteral("allOf")).toArray()) {
        const auto example = schemaExample(root, part, depth + 1);
        if (!example)
            continue;
        if (!example->isObject())
            return example;
        const auto partObject = example->toObject();
        for (auto it = partObject.begin(); it != partObject.end(); ++it)
            combined.insert(it.key(), it.value());
    }
    const auto properties = object.value(QStringLiteral("properties")).toObject();
    if (!properties.isEmpty()) {
        for (auto it = properties.begin(); it != properties.end(); ++it) {
            const auto example = schemaExample(root, it.value(), depth + 1);
            if (example)
                combined.insert(it.key(), *example);
        }
    }
    if (!combined.isEmpty())
        return combined;

    for (const auto key : {QStringLiteral("oneOf"), QStringLiteral("anyOf")}) {
        for (const auto &part : object.value(key).toArray()) {
            const auto example = schemaExample(root, part, depth + 1);
            if (example)
                return example;
        }
    }

    const auto item = schemaExample(
        root, object.value(QStringLiteral("items")), depth + 1);
    if (item)
        return QJsonArray{*item};
    const auto type = object.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("array"))
        return QJsonArray{};
    if (type == QStringLiteral("object")) {
        const auto additional = schemaExample(
            root, object.value(QStringLiteral("additionalProperties")), depth + 1);
        return additional ? QJsonObject{{QStringLiteral("additionalProp1"), *additional}}
                          : QJsonObject{};
    }
    if (type == QStringLiteral("boolean"))
        return true;
    if (type == QStringLiteral("integer") || type == QStringLiteral("number")) {
        const auto format = object.value(QStringLiteral("format"))
                                .toString().toLower();
        if (format == QStringLiteral("unix-time")
            || format == QStringLiteral("unix-timestamp")
            || format == QStringLiteral("timestamp"))
            return static_cast<double>(QDateTime::currentSecsSinceEpoch());
        return 0;
    }
    if (type == QStringLiteral("string")) {
        const auto format = object.value(QStringLiteral("format"))
                                .toString().toLower();
        const auto now = QDateTime::currentDateTimeUtc();
        if (format == QStringLiteral("date-time")
            || format == QStringLiteral("datetime")
            || format == QStringLiteral("timestamp"))
            return now.toString(Qt::ISODateWithMs);
        if (format == QStringLiteral("date"))
            return now.date().toString(Qt::ISODate);
        if (format == QStringLiteral("time"))
            return now.time().toString(QStringLiteral("HH:mm:ss.zzz"));
        return QStringLiteral("string");
    }
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

    if (const auto example = namedExample(
            root, media.value(QStringLiteral("examples"))))
        return example;
    if (media.contains(QStringLiteral("x-example")))
        return media.value(QStringLiteral("x-example"));
    if (const auto example = namedExample(
            root, media.value(QStringLiteral("x-examples")), true))
        return example;
    return schemaExample(root, media.value(QStringLiteral("schema")));
}

QString formValue(const QJsonValue &value) {
    if (value.isString())
        return value.toString();
    if (value.isBool())
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (value.isDouble())
        return QString::number(value.toDouble(), 'g', 15);
    if (value.isNull())
        return {};
    if (value.isObject())
        return QString::fromUtf8(
            QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    return QString::fromUtf8(
        QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
}

QByteArray formExample(const QJsonObject &example) {
    QUrlQuery form;
    for (auto it = example.begin(); it != example.end(); ++it) {
        if (it.value().isArray()) {
            for (const auto &item : it.value().toArray())
                form.addQueryItem(it.key(), formValue(item));
        } else {
            form.addQueryItem(it.key(), formValue(it.value()));
        }
    }
    return form.query(QUrl::FullyEncoded).toUtf8();
}

QByteArray encodeExample(const QJsonValue &example,
                         const QString &contentType) {
    if (contentType.startsWith(
            QStringLiteral("application/x-www-form-urlencoded"),
            Qt::CaseInsensitive) && example.isObject())
        return formExample(example.toObject());
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
        if (parameter.contains(QStringLiteral("example")))
            example = parameter.value(QStringLiteral("example"));
        else if (parameter.contains(QStringLiteral("x-example")))
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
