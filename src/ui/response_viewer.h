#pragma once

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTableWidget;

class ResponseViewer : public QWidget {
    Q_OBJECT
public:
    explicit ResponseViewer(QWidget *parent = nullptr);
    void begin();
    void appendChunk(const QByteArray &chunk);
    void finish(int statusCode,
                const QList<QPair<QByteArray, QByteArray>> &headers,
                qint64 elapsedMs, qint64 bytes, const QString &error);
    QByteArray responseData() const;

signals:
    void assertionsEvaluated(bool passed);

private:
    void filterHeaders(const QString &text);

    QLabel *m_status;
    QLabel *m_time;
    QLabel *m_size;
    QLabel *m_assertion;
    QPlainTextEdit *m_body;
    QTableWidget *m_headers;
    QLineEdit *m_headerSearch;
    QLineEdit *m_expectedStatus;
    QLineEdit *m_maxTime;
    QByteArray m_data;
    bool m_streamingLarge = false;
};
