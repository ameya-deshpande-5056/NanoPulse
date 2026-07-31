#include "sqlite_manager.h"

#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>

namespace {
QJsonArray pairsToJson(const QList<QPair<QString, QString>> &pairs) {
    QJsonArray array;
    for (const auto &pair : pairs)
        array.append(QJsonObject{{QStringLiteral("key"), pair.first},
                                 {QStringLiteral("value"), pair.second}});
    return array;
}

QList<QPair<QString, QString>> jsonToPairs(const QString &value) {
    QList<QPair<QString, QString>> pairs;
    const auto array = QJsonDocument::fromJson(value.toUtf8()).array();
    for (const auto &entry : array) {
        const auto object = entry.toObject();
        pairs.append({object.value(QStringLiteral("key")).toString(),
                      object.value(QStringLiteral("value")).toString()});
    }
    return pairs;
}

QJsonObject bodyOptionsToJson(const ApiRequest &request) {
    QJsonObject object;
    object.insert(QStringLiteral("mode"), static_cast<int>(request.bodyMode));
    object.insert(QStringLiteral("content_type"), request.bodyContentType);
    object.insert(QStringLiteral("binary_file"), request.binaryFilePath);
    object.insert(QStringLiteral("follow_redirects"), request.followRedirects);
    object.insert(QStringLiteral("verify_tls"), request.verifyTls);
    object.insert(QStringLiteral("proxy_url"), request.proxyUrl);
    object.insert(QStringLiteral("client_certificate"), request.clientCertificatePath);
    object.insert(QStringLiteral("client_private_key"), request.clientPrivateKeyPath);
    QJsonArray entries;
    for (const auto &entry : request.bodyEntries) {
        entries.append(QJsonObject{{QStringLiteral("key"), entry.key},
                                   {QStringLiteral("value"), entry.value},
                                   {QStringLiteral("file_path"), entry.filePath},
                                   {QStringLiteral("content_type"), entry.contentType},
                                   {QStringLiteral("is_file"), entry.isFile},
                                   {QStringLiteral("enabled"), entry.enabled}});
    }
    object.insert(QStringLiteral("entries"), entries);
    return object;
}

void bodyOptionsFromJson(const QString &json, ApiRequest *request) {
    const auto object = QJsonDocument::fromJson(json.toUtf8()).object();
    if (object.isEmpty())
        return;
    request->bodyMode = static_cast<RequestBodyMode>(object.value(QStringLiteral("mode")).toInt());
    request->bodyContentType = object.value(QStringLiteral("content_type")).toString();
    request->binaryFilePath = object.value(QStringLiteral("binary_file")).toString();
    request->followRedirects = object.value(QStringLiteral("follow_redirects")).toBool(true);
    request->verifyTls = object.value(QStringLiteral("verify_tls")).toBool(true);
    request->proxyUrl = object.value(QStringLiteral("proxy_url")).toString();
    request->clientCertificatePath = object.value(QStringLiteral("client_certificate")).toString();
    request->clientPrivateKeyPath = object.value(QStringLiteral("client_private_key")).toString();
    for (const auto &value : object.value(QStringLiteral("entries")).toArray()) {
        const auto entryObject = value.toObject();
        RequestBodyEntry entry;
        entry.key = entryObject.value(QStringLiteral("key")).toString();
        entry.value = entryObject.value(QStringLiteral("value")).toString();
        entry.filePath = entryObject.value(QStringLiteral("file_path")).toString();
        entry.contentType = entryObject.value(QStringLiteral("content_type")).toString();
        entry.isFile = entryObject.value(QStringLiteral("is_file")).toBool();
        entry.enabled = entryObject.value(QStringLiteral("enabled")).toBool(true);
        request->bodyEntries.append(entry);
    }
}
}

SqliteManager::SqliteManager()
    : m_connectionName(QStringLiteral("nanopulse-%1")
                           .arg(QUuid::createUuid().toString(QUuid::WithoutBraces))) {}

SqliteManager::~SqliteManager() {
    if (m_database.isValid())
        m_database.close();
    m_database = {};
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool SqliteManager::open() {
    const auto directory = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation);
    QDir().mkpath(directory);
    m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_database.setDatabaseName(directory + QStringLiteral("/nanopulse.db"));
    if (!m_database.open()) {
        m_error = m_database.lastError().text();
        return false;
    }
    QSqlQuery pragma(m_database);
    pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    pragma.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    return initialize();
}

bool SqliteManager::initialize() {
    const QStringList statements = {
        QStringLiteral("CREATE TABLE IF NOT EXISTS collections("
                       "id TEXT PRIMARY KEY,name TEXT NOT NULL,method TEXT,url TEXT,"
                       "headers TEXT,params TEXT,body TEXT,auth_type TEXT,"
                       "folder_id TEXT,created_at TEXT,is_folder INTEGER DEFAULT 0)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_collections_folder "
                       "ON collections(folder_id)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS environments("
                       "id TEXT PRIMARY KEY,name TEXT UNIQUE NOT NULL,variables TEXT,"
                       "is_active INTEGER DEFAULT 0,created_at TEXT)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS request_history("
                       "id TEXT PRIMARY KEY,method TEXT,url TEXT,timestamp TEXT,"
                       "status_code INTEGER,response_time_ms INTEGER)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_history_timestamp "
                       "ON request_history(timestamp DESC)")
    };
    for (const auto &statement : statements) {
        QSqlQuery query(m_database);
        if (!query.exec(statement)) {
            m_error = query.lastError().text();
            return false;
        }
    }
    // Existing local databases predate structured request body storage. SQLite's
    // ADD COLUMN is non-destructive; a duplicate-column error simply means the
    // database has already been migrated.
    QSqlQuery migration(m_database);
    migration.exec(QStringLiteral("ALTER TABLE collections ADD COLUMN body_options TEXT"));
    return true;
}

QString SqliteManager::lastError() const {
    return m_error;
}

QList<CollectionItem> SqliteManager::collectionChildren(const QString &folderId) const {
    QList<CollectionItem> items;
    QSqlQuery query(m_database);
    if (folderId.isEmpty())
        query.prepare(QStringLiteral("SELECT id,name,method,url,folder_id,is_folder "
                                     "FROM collections WHERE folder_id IS NULL OR folder_id='' "
                                     "ORDER BY is_folder DESC,name"));
    else {
        query.prepare(QStringLiteral("SELECT id,name,method,url,folder_id,is_folder "
                                     "FROM collections WHERE folder_id=? "
                                     "ORDER BY is_folder DESC,name"));
        query.addBindValue(folderId);
    }
    if (!query.exec())
        return items;
    while (query.next())
        items.append({query.value(0).toString(), query.value(1).toString(),
                      query.value(2).toString(), query.value(3).toString(),
                      query.value(4).toString(), query.value(5).toBool()});
    return items;
}

bool SqliteManager::saveRequest(const QString &name, const ApiRequest &request,
                                const QString &folderId) {
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO collections(id,name,method,url,headers,params,body,auth_type,"
        "folder_id,created_at,is_folder,body_options) VALUES(?,?,?,?,?,?,?,?,?,?,0,?)"));
    query.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
    query.addBindValue(name);
    query.addBindValue(request.method);
    query.addBindValue(request.url);
    query.addBindValue(QString::fromUtf8(
        QJsonDocument(pairsToJson(request.headers)).toJson(QJsonDocument::Compact)));
    query.addBindValue(QString::fromUtf8(
        QJsonDocument(pairsToJson(request.params)).toJson(QJsonDocument::Compact)));
    query.addBindValue(QString::fromUtf8(request.body));
    query.addBindValue(QStringLiteral("none"));
    query.addBindValue(folderId);
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    query.addBindValue(QString::fromUtf8(
        QJsonDocument(bodyOptionsToJson(request)).toJson(QJsonDocument::Compact)));
    return query.exec();
}

QString SqliteManager::createFolder(const QString &name, const QString &parentId) {
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO collections(id,name,folder_id,created_at,is_folder) VALUES(?,?,?,?,1)"));
    query.addBindValue(id);
    query.addBindValue(name);
    query.addBindValue(parentId);
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    return query.exec() ? id : QString();
}

bool SqliteManager::renameItem(const QString &id, const QString &name) {
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE collections SET name=? WHERE id=?"));
    query.addBindValue(name);
    query.addBindValue(id);
    return query.exec();
}

bool SqliteManager::deleteItem(const QString &id) {
    QSet<QString> ids{id};
    QList<QString> pending{id};
    while (!pending.isEmpty()) {
        const auto parent = pending.takeFirst();
        QSqlQuery children(m_database);
        children.prepare(QStringLiteral("SELECT id FROM collections WHERE folder_id=?"));
        children.addBindValue(parent);
        children.exec();
        while (children.next()) {
            const auto child = children.value(0).toString();
            if (!ids.contains(child)) {
                ids.insert(child);
                pending.append(child);
            }
        }
    }
    m_database.transaction();
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM collections WHERE id=?"));
    for (const auto &itemId : ids) {
        query.bindValue(0, itemId);
        if (!query.exec()) {
            m_database.rollback();
            return false;
        }
    }
    return m_database.commit();
}

ApiRequest SqliteManager::request(const QString &id) const {
    ApiRequest result;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT method,url,headers,params,body,body_options FROM collections WHERE id=? AND is_folder=0"));
    query.addBindValue(id);
    if (query.exec() && query.next()) {
        result.method = query.value(0).toString();
        result.url = query.value(1).toString();
        result.headers = jsonToPairs(query.value(2).toString());
        result.params = jsonToPairs(query.value(3).toString());
        result.body = query.value(4).toString().toUtf8();
        bodyOptionsFromJson(query.value(5).toString(), &result);
    }
    return result;
}

void SqliteManager::addHistory(const ApiRequest &request, int statusCode,
                               qint64 responseTimeMs, int limit) {
    QSqlQuery insert(m_database);
    insert.prepare(QStringLiteral(
        "INSERT INTO request_history VALUES(?,?,?,?,?,?)"));
    insert.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
    insert.addBindValue(request.method);
    insert.addBindValue(request.resolvedUrl());
    insert.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    insert.addBindValue(statusCode);
    insert.addBindValue(responseTimeMs);
    insert.exec();
    QSqlQuery trim(m_database);
    trim.prepare(QStringLiteral(
        "DELETE FROM request_history WHERE id NOT IN "
        "(SELECT id FROM request_history ORDER BY timestamp DESC LIMIT ?)"));
    trim.addBindValue(limit);
    trim.exec();
}

QList<HistoryItem> SqliteManager::history(int limit) const {
    QList<HistoryItem> items;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT method,url,timestamp,status_code,response_time_ms "
        "FROM request_history ORDER BY timestamp DESC LIMIT ?"));
    query.addBindValue(limit);
    if (!query.exec())
        return items;
    while (query.next())
        items.append({query.value(0).toString(), query.value(1).toString(),
                      query.value(2).toString(), query.value(3).toInt(),
                      query.value(4).toLongLong()});
    return items;
}

void SqliteManager::clearHistory() {
    QSqlQuery(m_database).exec(QStringLiteral("DELETE FROM request_history"));
}

QList<QPair<QString, QMap<QString, QString>>> SqliteManager::environments() const {
    QList<QPair<QString, QMap<QString, QString>>> result;
    QSqlQuery query(QStringLiteral("SELECT name,variables FROM environments ORDER BY name"),
                    m_database);
    while (query.next()) {
        QMap<QString, QString> variables;
        const auto object = QJsonDocument::fromJson(query.value(1).toByteArray()).object();
        for (auto it = object.begin(); it != object.end(); ++it)
            variables.insert(it.key(), it.value().toString());
        result.append({query.value(0).toString(), variables});
    }
    return result;
}

bool SqliteManager::saveEnvironment(const QString &name,
                                    const QMap<QString, QString> &variables) {
    QJsonObject object;
    for (auto it = variables.begin(); it != variables.end(); ++it)
        object.insert(it.key(), it.value());
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO environments(id,name,variables,created_at) VALUES(?,?,?,?) "
        "ON CONFLICT(name) DO UPDATE SET variables=excluded.variables"));
    query.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
    query.addBindValue(name);
    query.addBindValue(QString::fromUtf8(
        QJsonDocument(object).toJson(QJsonDocument::Compact)));
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    return query.exec();
}

bool SqliteManager::deleteEnvironment(const QString &name) {
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM environments WHERE name=?"));
    query.addBindValue(name);
    return query.exec();
}

QJsonArray SqliteManager::exportCollections() const {
    QJsonArray result;
    QSqlQuery query(QStringLiteral(
        "SELECT id,name,method,url,headers,params,body,auth_type,folder_id,"
        "created_at,is_folder,body_options FROM collections ORDER BY created_at"), m_database);
    while (query.next()) {
        QJsonObject item;
        item.insert(QStringLiteral("id"), query.value(0).toString());
        item.insert(QStringLiteral("name"), query.value(1).toString());
        item.insert(QStringLiteral("method"), query.value(2).toString());
        item.insert(QStringLiteral("url"), query.value(3).toString());
        item.insert(QStringLiteral("headers"),
                    QJsonDocument::fromJson(query.value(4).toByteArray()).array());
        item.insert(QStringLiteral("params"),
                    QJsonDocument::fromJson(query.value(5).toByteArray()).array());
        item.insert(QStringLiteral("body"), query.value(6).toString());
        item.insert(QStringLiteral("auth_type"), query.value(7).toString());
        item.insert(QStringLiteral("folder_id"), query.value(8).toString());
        item.insert(QStringLiteral("created_at"), query.value(9).toString());
        item.insert(QStringLiteral("is_folder"), query.value(10).toBool());
        item.insert(QStringLiteral("body_options"),
                    QJsonDocument::fromJson(query.value(11).toByteArray()).object());
        result.append(item);
    }
    return result;
}

int SqliteManager::importCollections(const QJsonArray &items) {
    QMap<QString, QString> ids;
    for (const auto &value : items) {
        const auto oldId = value.toObject().value(QStringLiteral("id")).toString();
        if (!oldId.isEmpty())
            ids.insert(oldId,
                       QUuid::createUuid().toString(QUuid::WithoutBraces));
    }
    int count = 0;
    m_database.transaction();
    for (const auto &value : items) {
        const auto item = value.toObject();
        const auto oldId = item.value(QStringLiteral("id")).toString();
        if (!ids.contains(oldId))
            continue;
        QSqlQuery query(m_database);
        query.prepare(QStringLiteral(
            "INSERT INTO collections(id,name,method,url,headers,params,body,"
            "auth_type,folder_id,created_at,is_folder,body_options) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)"));
        query.addBindValue(ids.value(oldId));
        query.addBindValue(item.value(QStringLiteral("name")).toString());
        query.addBindValue(item.value(QStringLiteral("method")).toString());
        query.addBindValue(item.value(QStringLiteral("url")).toString());
        query.addBindValue(QString::fromUtf8(QJsonDocument(
            item.value(QStringLiteral("headers")).toArray())
                                                .toJson(QJsonDocument::Compact)));
        query.addBindValue(QString::fromUtf8(QJsonDocument(
            item.value(QStringLiteral("params")).toArray())
                                                .toJson(QJsonDocument::Compact)));
        query.addBindValue(item.value(QStringLiteral("body")).toString());
        query.addBindValue(item.value(QStringLiteral("auth_type"))
                               .toString(QStringLiteral("none")));
        query.addBindValue(ids.value(
            item.value(QStringLiteral("folder_id")).toString()));
        query.addBindValue(item.value(QStringLiteral("created_at"))
                               .toString(QDateTime::currentDateTimeUtc()
                                             .toString(Qt::ISODateWithMs)));
        query.addBindValue(item.value(QStringLiteral("is_folder")).toBool());
        query.addBindValue(QString::fromUtf8(QJsonDocument(
            item.value(QStringLiteral("body_options")).toObject())
                                               .toJson(QJsonDocument::Compact)));
        if (query.exec())
            ++count;
    }
    if (!m_database.commit())
        return 0;
    return count;
}
