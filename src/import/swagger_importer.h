#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

class SqliteManager;

struct SwaggerImportResult {
    int requestCount = 0;
    QString error;
};

struct SwaggerDocument {
    QJsonObject root;
    QString title;
    QString baseUrl;
    QString error;
};

class SwaggerImporter {
public:
    static SwaggerDocument parseFile(const QString &filePath);
    static SwaggerDocument parse(const QByteArray &data, const QString &sourceName);
    static SwaggerImportResult importDocument(const SwaggerDocument &document,
                                              const QString &baseUrl,
                                              SqliteManager &storage);
};
