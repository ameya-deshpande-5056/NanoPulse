#pragma once

#include <QJsonArray>
#include <QJsonObject>

class PostmanExporter {
public:
    static QJsonObject collection(const QJsonArray &nanoPulseItems,
                                  const QString &collectionName);
};
