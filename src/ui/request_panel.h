#pragma once

#include "../network/request.h"

#include <QWidget>

class QComboBox;
class QCheckBox;
class QLineEdit;
class QPlainTextEdit;
class QStackedWidget;
class QTableWidget;

class RequestPanel : public QWidget {
    Q_OBJECT
public:
    explicit RequestPanel(QWidget *parent = nullptr);
    ApiRequest request() const;
    void setRequest(const ApiRequest &request);

private:
    void updateBodyView();
    static QList<QPair<QString, QString>> tablePairs(const QTableWidget *table);
    static void setTablePairs(QTableWidget *table,
                              const QList<QPair<QString, QString>> &pairs);

    QTableWidget *m_params;
    QTableWidget *m_headers;
    QPlainTextEdit *m_body;
    QComboBox *m_bodyType;
    QStackedWidget *m_bodyStack;
    QTableWidget *m_urlEncoded;
    QTableWidget *m_formData;
    QLineEdit *m_binaryPath;
    QCheckBox *m_pretty;
    QComboBox *m_authType;
    QLineEdit *m_authKey;
    QLineEdit *m_authValue;
    QLineEdit *m_authExtra;
    QString m_rawBody;
    bool m_updatingBody = false;
};
