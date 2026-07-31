#include "api_client.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QMimeDatabase>
#include <QNetworkProxy>
#include <QNetworkCookie>
#include <QNetworkCookieJar>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QUrlQuery>

namespace {
QByteArray urlEncodedBody(const QList<RequestBodyEntry> &entries) {
    QUrlQuery query;
    for (const auto &entry : entries) {
        if (entry.enabled && !entry.key.isEmpty())
            query.addQueryItem(entry.key, entry.value);
    }
    return query.query(QUrl::FullyEncoded).toUtf8();
}

QHttpMultiPart *multipartBody(const QList<RequestBodyEntry> &entries,
                              QList<QFile *> *files) {
    auto *multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QMimeDatabase mimeDatabase;
    for (const auto &entry : entries) {
        if (!entry.enabled || entry.key.isEmpty())
            continue;
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QStringLiteral("form-data; name=\"%1\"").arg(entry.key));
        if (entry.isFile) {
            auto *file = new QFile(entry.filePath, multipart);
            if (!file->open(QIODevice::ReadOnly)) {
                file->deleteLater();
                continue;
            }
            const auto contentType = entry.contentType.isEmpty()
                ? mimeDatabase.mimeTypeForFile(entry.filePath).name()
                : entry.contentType;
            part.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
            part.setHeader(QNetworkRequest::ContentDispositionHeader,
                           QStringLiteral("form-data; name=\"%1\"; filename=\"%2\"")
                               .arg(entry.key, QFileInfo(entry.filePath).fileName()));
            part.setBodyDevice(file);
            files->append(file);
        } else {
            part.setBody(entry.value.toUtf8());
        }
        multipart->append(part);
    }
    return multipart;
}
}

ApiClient::ApiClient(QObject *parent) : QObject(parent) {
    m_manager.setCookieJar(new QNetworkCookieJar(&m_manager));
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (m_reply) {
            m_reply->setProperty("timedOut", true);
            m_reply->abort();
        }
    });
}

QList<QNetworkCookie> ApiClient::cookiesForUrl(const QUrl &url) const {
    return m_manager.cookieJar()->cookiesForUrl(url);
}

void ApiClient::setCookiesForUrl(const QUrl &url,
                                 const QList<QNetworkCookie> &cookies) {
    m_manager.cookieJar()->setCookiesFromUrl(cookies, url);
}

bool ApiClient::busy() const {
    return !m_reply.isNull();
}

void ApiClient::send(const ApiRequest &request) {
    if (m_reply)
        return;
    QNetworkRequest networkRequest(QUrl(request.resolvedUrl()));
    networkRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                request.followRedirects
                                    ? QNetworkRequest::NoLessSafeRedirectPolicy
                                    : QNetworkRequest::ManualRedirectPolicy);
    if (request.proxyUrl.trimmed().isEmpty()) {
        m_manager.setProxy(QNetworkProxy::DefaultProxy);
    } else {
        const QUrl proxyUrl(request.proxyUrl);
        m_manager.setProxy(QNetworkProxy(proxyUrl.scheme().compare(QStringLiteral("socks5"),
                                                                    Qt::CaseInsensitive) == 0
                                         ? QNetworkProxy::Socks5Proxy : QNetworkProxy::HttpProxy,
                                         proxyUrl.host(), proxyUrl.port(),
                                         QUrl::fromPercentEncoding(proxyUrl.userName().toUtf8()),
                                         QUrl::fromPercentEncoding(proxyUrl.password().toUtf8())));
    }
    if (!request.clientCertificatePath.isEmpty()) {
        auto ssl = QSslConfiguration::defaultConfiguration();
        QFile certificateFile(request.clientCertificatePath);
        if (certificateFile.open(QIODevice::ReadOnly))
            ssl.setLocalCertificate(QSslCertificate(&certificateFile));
        if (!request.clientPrivateKeyPath.isEmpty()) {
            QFile keyFile(request.clientPrivateKeyPath);
            if (keyFile.open(QIODevice::ReadOnly))
                ssl.setPrivateKey(QSslKey(&keyFile, QSsl::Rsa));
        }
        networkRequest.setSslConfiguration(ssl);
    }
    for (const auto &header : request.headers)
        networkRequest.setRawHeader(header.first.toUtf8(), header.second.toUtf8());

    const auto verb = request.method.toUtf8();
    QHttpMultiPart *multipart = nullptr;
    QByteArray payload = request.body;
    if (request.bodyMode == RequestBodyMode::UrlEncoded) {
        payload = urlEncodedBody(request.bodyEntries);
        if (!networkRequest.hasRawHeader("Content-Type"))
            networkRequest.setHeader(QNetworkRequest::ContentTypeHeader,
                                     QStringLiteral("application/x-www-form-urlencoded"));
    } else if (request.bodyMode == RequestBodyMode::Multipart) {
        multipart = multipartBody(request.bodyEntries, &m_uploadFiles);
    } else if (request.bodyMode == RequestBodyMode::Binary) {
        auto *file = new QFile(request.binaryFilePath, this);
        if (file->open(QIODevice::ReadOnly)) {
            m_uploadFiles.append(file);
            if (!request.bodyContentType.isEmpty() && !networkRequest.hasRawHeader("Content-Type"))
                networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, request.bodyContentType);
        } else {
            file->deleteLater();
        }
    }

    if (verb == "GET")
        m_reply = m_manager.get(networkRequest);
    else if (verb == "HEAD")
        m_reply = m_manager.head(networkRequest);
    else if (multipart && verb == "POST")
        m_reply = m_manager.post(networkRequest, multipart);
    else if (multipart && verb == "PUT")
        m_reply = m_manager.put(networkRequest, multipart);
    else if (multipart)
        m_reply = m_manager.sendCustomRequest(networkRequest, verb, multipart);
    else if (request.bodyMode == RequestBodyMode::Binary && !m_uploadFiles.isEmpty())
        m_reply = m_manager.sendCustomRequest(networkRequest, verb, m_uploadFiles.constLast());
    else if (verb == "POST")
        m_reply = m_manager.post(networkRequest, payload);
    else if (verb == "PUT")
        m_reply = m_manager.put(networkRequest, payload);
    else if (verb == "DELETE")
        m_reply = m_manager.sendCustomRequest(networkRequest, verb, payload);
    else
        m_reply = m_manager.sendCustomRequest(networkRequest, verb, payload);

    m_bytes = 0;
    m_elapsed.start();
    m_timeout.start(request.timeoutMs);
    emit started();

    if (!request.verifyTls) {
        connect(m_reply, &QNetworkReply::sslErrors, m_reply,
                [reply = m_reply](const QList<QSslError> &) { reply->ignoreSslErrors(); });
    }

    connect(m_reply, &QNetworkReply::readyRead, this, [this] {
        if (!m_reply)
            return;
        const auto chunk = m_reply->readAll();
        m_bytes += chunk.size();
        emit chunkReceived(chunk);
    });
    connect(m_reply, &QNetworkReply::finished, this, [this] {
        if (!m_reply)
            return;
        m_timeout.stop();
        const auto tail = m_reply->readAll();
        if (!tail.isEmpty()) {
            m_bytes += tail.size();
            emit chunkReceived(tail);
        }
        const int status = m_reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString error;
        if (m_reply->property("timedOut").toBool())
            error = tr("Request timed out");
        else if (m_reply->error() != QNetworkReply::NoError)
            error = m_reply->errorString();
        const auto headers = m_reply->rawHeaderPairs();
        const auto elapsed = m_elapsed.elapsed();
        for (auto *file : m_uploadFiles)
            file->deleteLater();
        m_uploadFiles.clear();
        m_reply->deleteLater();
        m_reply = nullptr;
        emit completed(status, headers, elapsed, m_bytes, error);
    });
}

void ApiClient::cancel() {
    if (m_reply)
        m_reply->abort();
}
