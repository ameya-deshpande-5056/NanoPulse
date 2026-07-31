#pragma once

#include "request.h"

#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>

class QFile;

class ApiClient : public QObject {
    Q_OBJECT
public:
    explicit ApiClient(QObject *parent = nullptr);
    bool busy() const;
    QList<QNetworkCookie> cookiesForUrl(const QUrl &url) const;
    void setCookiesForUrl(const QUrl &url, const QList<QNetworkCookie> &cookies);

public slots:
    void send(const ApiRequest &request);
    void cancel();

signals:
    void started();
    void chunkReceived(const QByteArray &chunk);
    void completed(int statusCode, const QList<QPair<QByteArray, QByteArray>> &headers,
                   qint64 elapsedMs, qint64 bytes, const QString &error);

private:
    QNetworkAccessManager m_manager;
    QPointer<QNetworkReply> m_reply;
    QElapsedTimer m_elapsed;
    QTimer m_timeout;
    QList<QFile *> m_uploadFiles;
    qint64 m_bytes = 0;
};
