#pragma once

#include <QJsonObject>
#include <QString>

class SqliteManager;

struct PostmanImportResult {
    int requestCount = 0;
    QString error;
};

class PostmanImporter {
public:
    static bool isCollection(const QJsonObject &root);
    static PostmanImportResult importCollection(const QJsonObject &root,
                                                SqliteManager &storage);
};
