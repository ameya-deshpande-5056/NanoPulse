#include "har_importer.h"

#include "../network/request.h"
#include "../storage/sqlite_manager.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QObject>
#include <QUrl>

bool HarImporter::isHar(const QJsonObject &root) {
    return root.value(QStringLiteral("log")).toObject()
        .value(QStringLiteral("entries")).isArray();
}

HarImportResult HarImporter::importArchive(const QJsonObject &root,
                                            const QString &name,
                                            SqliteManager &storage) {
    HarImportResult result;
    if (!isHar(root)) {
        result.error = QObject::tr("This file is not a valid HAR archive.");
        return result;
    }
    const auto folder = storage.createFolder(name.isEmpty() ? QObject::tr("Imported HAR") : name);
    if (folder.isEmpty()) {
        result.error = QObject::tr("Could not create the HAR collection.");
        return result;
    }
    int sequence = 1;
    for (const auto &value : root.value(QStringLiteral("log")).toObject()
                                  .value(QStringLiteral("entries")).toArray()) {
        const auto object = value.toObject().value(QStringLiteral("request")).toObject();
        ApiRequest request;
        request.method = object.value(QStringLiteral("method")).toString(QStringLiteral("GET"));
        request.url = object.value(QStringLiteral("url")).toString();
        for (const auto &headerValue : object.value(QStringLiteral("headers")).toArray()) {
            const auto header = headerValue.toObject();
            request.headers.append({header.value(QStringLiteral("name")).toString(),
                                    header.value(QStringLiteral("value")).toString()});
        }
        const auto postData = object.value(QStringLiteral("postData")).toObject();
        const auto mimeType = postData.value(QStringLiteral("mimeType")).toString();
        if (mimeType.contains(QStringLiteral("application/x-www-form-urlencoded"))) {
            request.bodyMode = RequestBodyMode::UrlEncoded;
            request.bodyContentType = mimeType;
            for (const auto &parameterValue : postData.value(QStringLiteral("params")).toArray()) {
                const auto parameter = parameterValue.toObject();
                request.bodyEntries.append({parameter.value(QStringLiteral("name")).toString(),
                                            parameter.value(QStringLiteral("value")).toString()});
            }
        } else if (!postData.value(QStringLiteral("text")).toString().isEmpty()) {
            request.bodyMode = RequestBodyMode::Raw;
            request.bodyContentType = mimeType;
            request.body = postData.value(QStringLiteral("text")).toString().toUtf8();
        } else {
            request.bodyMode = RequestBodyMode::None;
        }
        const QUrl url(request.url);
        auto requestName = QStringLiteral("%1 %2").arg(request.method, url.path());
        if (url.path().isEmpty())
            requestName = QStringLiteral("Request %1").arg(sequence);
        if (storage.saveRequest(requestName, request, folder))
            ++result.requestCount;
        ++sequence;
    }
    return result;
}
