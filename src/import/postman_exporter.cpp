#include "postman_exporter.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QMap>

namespace {
QJsonArray pairs(const QJsonArray &values) {
    QJsonArray result;
    for (const auto &value : values) {
        const auto item = value.toObject();
        result.append(QJsonObject{{QStringLiteral("key"), item.value(QStringLiteral("key")).toString()},
                                  {QStringLiteral("value"), item.value(QStringLiteral("value")).toString()}});
    }
    return result;
}

QJsonObject requestItem(const QJsonObject &item) {
    const auto options = item.value(QStringLiteral("body_options")).toObject();
    const auto mode = options.value(QStringLiteral("mode")).toInt(1);
    QJsonObject request{{QStringLiteral("method"), item.value(QStringLiteral("method")).toString()},
                        {QStringLiteral("header"), pairs(item.value(QStringLiteral("headers")).toArray())},
                        {QStringLiteral("url"), item.value(QStringLiteral("url")).toString()}};
    QJsonObject body;
    if (mode == 1) {
        body.insert(QStringLiteral("mode"), QStringLiteral("raw"));
        body.insert(QStringLiteral("raw"), item.value(QStringLiteral("body")).toString());
        const auto type = options.value(QStringLiteral("content_type")).toString();
        const auto language = type.contains(QStringLiteral("json")) ? QStringLiteral("json")
            : type.contains(QStringLiteral("xml")) ? QStringLiteral("xml")
            : type.contains(QStringLiteral("html")) ? QStringLiteral("html")
            : type.contains(QStringLiteral("javascript")) ? QStringLiteral("javascript")
            : QStringLiteral("text");
        body.insert(QStringLiteral("options"), QJsonObject{{QStringLiteral("raw"),
            QJsonObject{{QStringLiteral("language"), language}}}});
    } else if (mode == 2 || mode == 3) {
        body.insert(QStringLiteral("mode"), mode == 2 ? QStringLiteral("urlencoded")
                                                       : QStringLiteral("formdata"));
        QJsonArray entries;
        for (const auto &value : options.value(QStringLiteral("entries")).toArray()) {
            const auto entry = value.toObject();
            QJsonObject output{{QStringLiteral("key"), entry.value(QStringLiteral("key")).toString()},
                               {QStringLiteral("disabled"), !entry.value(QStringLiteral("enabled")).toBool(true)}};
            if (mode == 3 && entry.value(QStringLiteral("is_file")).toBool()) {
                output.insert(QStringLiteral("type"), QStringLiteral("file"));
                output.insert(QStringLiteral("src"), entry.value(QStringLiteral("file_path")).toString());
            } else {
                output.insert(QStringLiteral("type"), QStringLiteral("text"));
                output.insert(QStringLiteral("value"), entry.value(QStringLiteral("value")).toString());
            }
            entries.append(output);
        }
        body.insert(body.value(QStringLiteral("mode")).toString(), entries);
    } else if (mode == 4) {
        body.insert(QStringLiteral("mode"), QStringLiteral("file"));
        body.insert(QStringLiteral("file"), QJsonObject{{QStringLiteral("src"),
            options.value(QStringLiteral("binary_file")).toString()}});
    }
    if (!body.isEmpty())
        request.insert(QStringLiteral("body"), body);
    return QJsonObject{{QStringLiteral("name"), item.value(QStringLiteral("name")).toString()},
                       {QStringLiteral("request"), request}};
}

QJsonArray children(const QString &parentId, const QMap<QString, QJsonArray> &byParent) {
    QJsonArray result;
    for (const auto &value : byParent.value(parentId)) {
        const auto item = value.toObject();
        if (item.value(QStringLiteral("is_folder")).toBool()) {
            QJsonObject folder{{QStringLiteral("name"), item.value(QStringLiteral("name")).toString()},
                               {QStringLiteral("item"), children(item.value(QStringLiteral("id")).toString(), byParent)}};
            result.append(folder);
        } else {
            result.append(requestItem(item));
        }
    }
    return result;
}
}

QJsonObject PostmanExporter::collection(const QJsonArray &nanoPulseItems,
                                        const QString &collectionName) {
    QMap<QString, QJsonArray> byParent;
    for (const auto &value : nanoPulseItems) {
        const auto item = value.toObject();
        byParent[item.value(QStringLiteral("folder_id")).toString()].append(item);
    }
    QJsonObject info{{QStringLiteral("name"), collectionName},
                     {QStringLiteral("schema"),
                      QStringLiteral("https://schema.getpostman.com/json/collection/v2.1.0/collection.json")}};
    return QJsonObject{{QStringLiteral("info"), info},
                       {QStringLiteral("item"), children({}, byParent)}};
}
