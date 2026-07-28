#include "api_client.h"

#include <QNetworkReply>
#include <QNetworkRequest>

ApiClient::ApiClient(QObject *parent) : QObject(parent) {
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (m_reply) {
            m_reply->setProperty("timedOut", true);
            m_reply->abort();
        }
    });
}

bool ApiClient::busy() const {
    return !m_reply.isNull();
}

void ApiClient::send(const ApiRequest &request) {
    if (m_reply)
        return;
    QNetworkRequest networkRequest(QUrl(request.resolvedUrl()));
    networkRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                QNetworkRequest::NoLessSafeRedirectPolicy);
    for (const auto &header : request.headers)
        networkRequest.setRawHeader(header.first.toUtf8(), header.second.toUtf8());

    const auto verb = request.method.toUtf8();
    if (verb == "GET")
        m_reply = m_manager.get(networkRequest);
    else if (verb == "HEAD")
        m_reply = m_manager.head(networkRequest);
    else if (verb == "POST")
        m_reply = m_manager.post(networkRequest, request.body);
    else if (verb == "PUT")
        m_reply = m_manager.put(networkRequest, request.body);
    else if (verb == "DELETE")
        m_reply = m_manager.sendCustomRequest(networkRequest, verb, request.body);
    else
        m_reply = m_manager.sendCustomRequest(networkRequest, verb, request.body);

    m_bytes = 0;
    m_elapsed.start();
    m_timeout.start(request.timeoutMs);
    emit started();

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
        m_reply->deleteLater();
        m_reply = nullptr;
        emit completed(status, headers, elapsed, m_bytes, error);
    });
}

void ApiClient::cancel() {
    if (m_reply)
        m_reply->abort();
}
