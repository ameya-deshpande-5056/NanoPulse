#pragma once

#include "../network/request.h"

class SqliteManager;

struct BrunoImportResult {
    int requestCount = 0;
    QString error;
};

class BrunoImporter {
public:
    static ApiRequest parseFile(const QString &path, QString *name = nullptr,
                                QString *error = nullptr);
    static BrunoImportResult importDirectory(const QString &path,
                                             SqliteManager &storage);
};
