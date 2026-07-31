#pragma once

#include <QJsonObject>
#include <QString>

class SqliteManager;

struct ExternalCollectionImportResult {
    int requestCount = 0;
    QString format;
    QString error;
};

class ExternalCollectionImporter {
public:
    static bool recognizes(const QJsonObject &root);
    static ExternalCollectionImportResult importCollection(
        const QJsonObject &root, const QString &fallbackName,
        SqliteManager &storage);
};
