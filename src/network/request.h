#pragma once

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>
#include <QUrlQuery>

enum class RequestBodyMode {
    None,
    Raw,
    UrlEncoded,
    Multipart,
    Binary
};

struct RequestBodyEntry {
    QString key;
    QString value;
    QString filePath;
    QString contentType;
    bool isFile = false;
    bool enabled = true;
};

struct ApiRequest {
    QString method = QStringLiteral("GET");
    QString url;
    QList<QPair<QString, QString>> headers;
    QList<QPair<QString, QString>> params;
    QByteArray body;
    RequestBodyMode bodyMode = RequestBodyMode::Raw;
    QString bodyContentType = QStringLiteral("application/json");
    QList<RequestBodyEntry> bodyEntries;
    QString binaryFilePath;
    int timeoutMs = 30000;
    bool followRedirects = true;
    bool verifyTls = true;
    QString proxyUrl;
    QString clientCertificatePath;
    QString clientPrivateKeyPath;

    QString resolvedUrl() const {
        QUrl target(url);
        QUrlQuery query(target);
        for (const auto &param : params) {
            if (!param.first.isEmpty())
                query.addQueryItem(param.first, param.second);
        }
        target.setQuery(query);
        return target.toString();
    }
};
