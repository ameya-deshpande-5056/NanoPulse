#include "external_collection_importer.h"

#include "../network/request.h"
#include "../storage/sqlite_manager.h"

#include <QJsonArray>
#include <QMap>
#include <QObject>

namespace {
QList<QPair<QString, QString>> pairs(const QJsonArray &array) {
    QList<QPair<QString, QString>> result;
    for (const auto &value : array) {
        const auto object = value.toObject();
        if (object.value(QStringLiteral("disabled")).toBool())
            continue;
        const auto key = object.value(QStringLiteral("key")).toString(
            object.value(QStringLiteral("name")).toString());
        if (!key.isEmpty())
            result.append({key, object.value(QStringLiteral("value")).toVariant().toString()});
    }
    return result;
}

void contentTypeFromHeaders(ApiRequest *request) {
    for (const auto &header : request->headers) {
        if (header.first.compare(QStringLiteral("Content-Type"), Qt::CaseInsensitive) == 0) {
            request->bodyContentType = header.second;
            return;
        }
    }
}

void applyAuth(const QJsonObject &auth, ApiRequest *request) {
    if (auth.value(QStringLiteral("disabled")).toBool())
        return;
    const auto type = auth.value(QStringLiteral("type")).toString().toLower();
    if (type.contains(QStringLiteral("bearer"))) {
        auto token = auth.value(QStringLiteral("token")).toString();
        if (token.isEmpty())
            token = auth.value(QStringLiteral("bearer")).toString();
        request->headers.append({QStringLiteral("Authorization"),
                                 QStringLiteral("Bearer ") + token});
    } else if (type.contains(QStringLiteral("basic"))) {
        const auto credentials = auth.value(QStringLiteral("username")).toString()
            + QLatin1Char(':') + auth.value(QStringLiteral("password")).toString();
        request->headers.append({QStringLiteral("Authorization"),
                                 QStringLiteral("Basic ")
                                     + QString::fromLatin1(credentials.toUtf8().toBase64())});
    }
}

ApiRequest insomniaRequest(const QJsonObject &object) {
    ApiRequest request;
    request.method = object.value(QStringLiteral("method")).toString(QStringLiteral("GET"));
    request.url = object.value(QStringLiteral("url")).toString();
    request.headers = pairs(object.value(QStringLiteral("headers")).toArray());
    request.params = pairs(object.value(QStringLiteral("parameters")).toArray());
    const auto body = object.value(QStringLiteral("body")).toObject();
    request.bodyContentType = body.value(QStringLiteral("mimeType")).toString();
    const auto parameters = body.value(QStringLiteral("params")).toArray();
    if (!parameters.isEmpty()) {
        request.bodyMode = request.bodyContentType.contains(QStringLiteral("multipart"))
            ? RequestBodyMode::Multipart : RequestBodyMode::UrlEncoded;
        for (const auto &value : parameters) {
            const auto part = value.toObject();
            RequestBodyEntry entry;
            entry.key = part.value(QStringLiteral("name")).toString();
            entry.value = part.value(QStringLiteral("value")).toString();
            entry.filePath = part.value(QStringLiteral("fileName")).toString();
            entry.isFile = !entry.filePath.isEmpty();
            entry.enabled = !part.value(QStringLiteral("disabled")).toBool();
            request.bodyEntries.append(entry);
        }
    } else if (!body.value(QStringLiteral("text")).toString().isEmpty()) {
        request.bodyMode = RequestBodyMode::Raw;
        request.body = body.value(QStringLiteral("text")).toString().toUtf8();
    } else {
        request.bodyMode = RequestBodyMode::None;
    }
    contentTypeFromHeaders(&request);
    applyAuth(object.value(QStringLiteral("authentication")).toObject(), &request);
    return request;
}

ApiRequest genericRequest(const QJsonObject &object) {
    ApiRequest request;
    request.method = object.value(QStringLiteral("method")).toString(QStringLiteral("GET"));
    request.url = object.value(QStringLiteral("endpoint")).toString(
        object.value(QStringLiteral("url")).toString());
    request.headers = pairs(object.value(QStringLiteral("headers")).toArray());
    request.params = pairs(object.value(QStringLiteral("params")).toArray());
    auto body = object.value(QStringLiteral("body")).toObject();
    auto raw = body.value(QStringLiteral("body")).toString(
        body.value(QStringLiteral("raw")).toString());
    request.bodyContentType = body.value(QStringLiteral("contentType")).toString(
        body.value(QStringLiteral("type")).toString());
    const auto form = body.value(QStringLiteral("form")).toArray();
    if (!form.isEmpty()) {
        request.bodyMode = request.bodyContentType.contains(QStringLiteral("multipart"))
            || request.bodyContentType == QStringLiteral("formdata")
            ? RequestBodyMode::Multipart : RequestBodyMode::UrlEncoded;
        for (const auto &pair : pairs(form))
            request.bodyEntries.append({pair.first, pair.second});
    } else if (!raw.isEmpty()) {
        request.bodyMode = RequestBodyMode::Raw;
        request.body = raw.toUtf8();
    } else {
        request.bodyMode = RequestBodyMode::None;
    }
    contentTypeFromHeaders(&request);
    applyAuth(object.value(QStringLiteral("auth")).toObject(), &request);
    return request;
}

void importHoppscotchFolder(const QJsonObject &object, const QString &parent,
                            SqliteManager &storage, int *count) {
    const auto folder = storage.createFolder(object.value(QStringLiteral("name")).toString(), parent);
    if (folder.isEmpty())
        return;
    for (const auto &requestValue : object.value(QStringLiteral("requests")).toArray()) {
        const auto source = requestValue.toObject();
        if (storage.saveRequest(source.value(QStringLiteral("name")).toString(),
                                genericRequest(source), folder))
            ++*count;
    }
    for (const auto &child : object.value(QStringLiteral("folders")).toArray())
        importHoppscotchFolder(child.toObject(), folder, storage, count);
}
}

bool ExternalCollectionImporter::recognizes(const QJsonObject &root) {
    return root.value(QStringLiteral("_type")).toString() == QStringLiteral("export")
        || root.value(QStringLiteral("resources")).isArray()
        || root.value(QStringLiteral("client")).toString().contains(QStringLiteral("Thunder Client"))
        || (root.value(QStringLiteral("folders")).isArray()
            && root.value(QStringLiteral("requests")).isArray());
}

ExternalCollectionImportResult ExternalCollectionImporter::importCollection(
    const QJsonObject &root, const QString &fallbackName, SqliteManager &storage) {
    ExternalCollectionImportResult result;
    if (!recognizes(root)) {
        result.error = QObject::tr("The collection format is not recognized.");
        return result;
    }
    if (root.value(QStringLiteral("resources")).isArray()) {
        result.format = QStringLiteral("Insomnia");
        QMap<QString, QJsonObject> resources;
        for (const auto &value : root.value(QStringLiteral("resources")).toArray()) {
            const auto object = value.toObject();
            resources.insert(object.value(QStringLiteral("_id")).toString(), object);
        }
        QMap<QString, QString> folderIds;
        const auto rootFolder = storage.createFolder(fallbackName);
        bool changed = true;
        while (changed) {
            changed = false;
            for (auto it = resources.cbegin(); it != resources.cend(); ++it) {
                const auto object = it.value();
                if (object.value(QStringLiteral("_type")).toString() != QStringLiteral("request_group")
                    || folderIds.contains(it.key()))
                    continue;
                const auto parentSource = object.value(QStringLiteral("parentId")).toString();
                if (!parentSource.isEmpty() && resources.value(parentSource)
                        .value(QStringLiteral("_type")).toString() == QStringLiteral("request_group")
                    && !folderIds.contains(parentSource))
                    continue;
                folderIds.insert(it.key(), storage.createFolder(
                    object.value(QStringLiteral("name")).toString(),
                    folderIds.value(parentSource, rootFolder)));
                changed = true;
            }
        }
        for (const auto &object : resources) {
            if (object.value(QStringLiteral("_type")).toString() != QStringLiteral("request"))
                continue;
            if (storage.saveRequest(object.value(QStringLiteral("name")).toString(),
                                    insomniaRequest(object),
                                    folderIds.value(object.value(QStringLiteral("parentId")).toString(), rootFolder)))
                ++result.requestCount;
        }
        return result;
    }
    if (root.value(QStringLiteral("client")).toString().contains(QStringLiteral("Thunder Client"))) {
        result.format = QStringLiteral("Thunder Client");
        const auto rootFolder = storage.createFolder(root.value(QStringLiteral("collectionName")).toString(fallbackName));
        QMap<QString, QString> folders;
        for (const auto &value : root.value(QStringLiteral("folders")).toArray()) {
            const auto object = value.toObject();
            folders.insert(object.value(QStringLiteral("_id")).toString(),
                           storage.createFolder(object.value(QStringLiteral("name")).toString(), rootFolder));
        }
        for (const auto &value : root.value(QStringLiteral("requests")).toArray()) {
            const auto object = value.toObject();
            if (storage.saveRequest(object.value(QStringLiteral("name")).toString(), genericRequest(object),
                                    folders.value(object.value(QStringLiteral("containerId")).toString(),
                                                  folders.value(object.value(QStringLiteral("colId")).toString(), rootFolder))))
                ++result.requestCount;
        }
        return result;
    }
    result.format = QStringLiteral("Hoppscotch");
    QJsonObject collection = root;
    if (!collection.contains(QStringLiteral("name")))
        collection.insert(QStringLiteral("name"), fallbackName);
    importHoppscotchFolder(collection, {}, storage, &result.requestCount);
    return result;
}
