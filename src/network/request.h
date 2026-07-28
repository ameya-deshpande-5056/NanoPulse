#pragma once

#include <QList>
#include <QPair>
#include <QString>
#include <QUrlQuery>

struct ApiRequest {
    QString method = QStringLiteral("GET");
    QString url;
    QList<QPair<QString, QString>> headers;
    QList<QPair<QString, QString>> params;
    QByteArray body;
    int timeoutMs = 30000;

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
