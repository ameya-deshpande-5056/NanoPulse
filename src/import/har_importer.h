#pragma once

#include <QJsonObject>
#include <QString>

class SqliteManager;

struct HarImportResult {
    int requestCount = 0;
    QString error;
};

class HarImporter {
public:
    static bool isHar(const QJsonObject &root);
    static HarImportResult importArchive(const QJsonObject &root,
                                         const QString &name,
                                         SqliteManager &storage);
};
