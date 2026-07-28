#pragma once

#include "../network/request.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QList>
#include <QMap>
#include <QSqlDatabase>
#include <QString>

struct CollectionItem {
    QString id;
    QString name;
    QString method;
    QString url;
    QString folderId;
    bool folder = false;
};

struct HistoryItem {
    QString method;
    QString url;
    QString timestamp;
    int statusCode = 0;
    qint64 responseTimeMs = 0;
};

class SqliteManager {
public:
    SqliteManager();
    ~SqliteManager();

    bool open();
    QString lastError() const;
    QList<CollectionItem> collectionChildren(const QString &folderId) const;
    bool saveRequest(const QString &name, const ApiRequest &request,
                     const QString &folderId = {});
    QString createFolder(const QString &name, const QString &parentId = {});
    bool renameItem(const QString &id, const QString &name);
    bool deleteItem(const QString &id);
    ApiRequest request(const QString &id) const;
    void addHistory(const ApiRequest &request, int statusCode, qint64 responseTimeMs,
                    int limit = 50);
    QList<HistoryItem> history(int limit = 50) const;
    void clearHistory();
    QList<QPair<QString, QMap<QString, QString>>> environments() const;
    bool saveEnvironment(const QString &name, const QMap<QString, QString> &variables);
    bool deleteEnvironment(const QString &name);
    QJsonArray exportCollections() const;
    int importCollections(const QJsonArray &items);

private:
    bool initialize();
    QSqlDatabase m_database;
    QString m_connectionName;
    QString m_error;
};
