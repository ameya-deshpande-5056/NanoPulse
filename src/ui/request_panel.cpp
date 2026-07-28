#include "request_panel.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextCharFormat>
#include <QVBoxLayout>

namespace {
QTableWidget *createPairTable(QWidget *parent) {
    auto *table = new QTableWidget(1, 2, parent);
    table->setHorizontalHeaderLabels(
        {QObject::tr("Key"), QObject::tr("Value")});
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    QObject::connect(table, &QTableWidget::cellChanged, table,
                     [table](int row, int) {
        if (row == table->rowCount() - 1) {
            const auto *key = table->item(row, 0);
            const auto *value = table->item(row, 1);
            if ((key && !key->text().isEmpty())
                || (value && !value->text().isEmpty()))
                table->insertRow(table->rowCount());
        }
    });
    return table;
}

class JsonHighlighter final : public QSyntaxHighlighter {
public:
    explicit JsonHighlighter(QTextDocument *document) : QSyntaxHighlighter(document) {}

protected:
    void highlightBlock(const QString &text) override {
        QTextCharFormat key;
        key.setForeground(QColor(QStringLiteral("#8ab4f8")));
        QTextCharFormat string;
        string.setForeground(QColor(QStringLiteral("#81c995")));
        QTextCharFormat literal;
        literal.setForeground(QColor(QStringLiteral("#fdd663")));
        apply(text, QRegularExpression(QStringLiteral(R"("(?:\\.|[^"\\])*"\s*:)")), key);
        apply(text, QRegularExpression(QStringLiteral(R"("(?:\\.|[^"\\])*")")), string);
        apply(text, QRegularExpression(QStringLiteral(
                        R"(\b(?:true|false|null|-?\d+(?:\.\d+)?)\b)")), literal);
    }

private:
    void apply(const QString &text, const QRegularExpression &expression,
               const QTextCharFormat &format) {
        auto matches = expression.globalMatch(text);
        while (matches.hasNext()) {
            const auto match = matches.next();
            setFormat(match.capturedStart(), match.capturedLength(), format);
        }
    }
};
}

RequestPanel::RequestPanel(QWidget *parent) : QWidget(parent) {
    auto *tabs = new QTabWidget(this);
    m_params = createPairTable(tabs);
    m_headers = createPairTable(tabs);

    auto *bodyPage = new QWidget(tabs);
    auto *bodyLayout = new QVBoxLayout(bodyPage);
    m_bodyType = new QComboBox(bodyPage);
    m_bodyType->addItems({tr("JSON"), tr("XML"), tr("Text")});
    m_body = new QPlainTextEdit(bodyPage);
    m_body->setPlaceholderText(tr("Request body"));
    m_body->setLineWrapMode(QPlainTextEdit::NoWrap);
    new JsonHighlighter(m_body->document());
    bodyLayout->addWidget(m_bodyType, 0, Qt::AlignLeft);
    bodyLayout->addWidget(m_body);

    auto *authPage = new QWidget(tabs);
    auto *authLayout = new QFormLayout(authPage);
    m_authType = new QComboBox(authPage);
    m_authType->addItems(
        {tr("None"), tr("Basic Auth"), tr("Bearer Token"), tr("API Key")});
    m_authKey = new QLineEdit(authPage);
    m_authValue = new QLineEdit(authPage);
    m_authExtra = new QLineEdit(authPage);
    m_authValue->setEchoMode(QLineEdit::Password);
    authLayout->addRow(tr("Type"), m_authType);
    authLayout->addRow(tr("Username / Key"), m_authKey);
    authLayout->addRow(tr("Password / Value"), m_authValue);
    authLayout->addRow(tr("API key location"), m_authExtra);

    tabs->addTab(m_params, tr("Params"));
    tabs->addTab(m_headers, tr("Headers"));
    tabs->addTab(bodyPage, tr("Body"));
    tabs->addTab(authPage, tr("Auth"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(tabs);
}

QList<QPair<QString, QString>>
RequestPanel::tablePairs(const QTableWidget *table) {
    QList<QPair<QString, QString>> result;
    for (int row = 0; row < table->rowCount(); ++row) {
        const auto *key = table->item(row, 0);
        const auto *value = table->item(row, 1);
        if (key && !key->text().trimmed().isEmpty())
            result.append({key->text().trimmed(), value ? value->text() : QString()});
    }
    return result;
}

void RequestPanel::setTablePairs(
    QTableWidget *table, const QList<QPair<QString, QString>> &pairs) {
    table->blockSignals(true);
    table->setRowCount(pairs.size() + 1);
    for (int row = 0; row < pairs.size(); ++row) {
        table->setItem(row, 0, new QTableWidgetItem(pairs.at(row).first));
        table->setItem(row, 1, new QTableWidgetItem(pairs.at(row).second));
    }
    table->blockSignals(false);
}

ApiRequest RequestPanel::request() const {
    ApiRequest result;
    result.params = tablePairs(m_params);
    result.headers = tablePairs(m_headers);
    result.body = m_body->toPlainText().toUtf8();
    const auto auth = m_authType->currentIndex();
    if (auth == 1) {
        result.headers.append(
            {QStringLiteral("Authorization"),
             QStringLiteral("Basic ")
                 + QString::fromLatin1(
                     (m_authKey->text() + QStringLiteral(":") + m_authValue->text())
                         .toUtf8().toBase64())});
    } else if (auth == 2) {
        result.headers.append(
            {QStringLiteral("Authorization"),
             QStringLiteral("Bearer ") + m_authValue->text()});
    } else if (auth == 3 && !m_authKey->text().isEmpty()) {
        if (m_authExtra->text().compare(QStringLiteral("query"),
                                        Qt::CaseInsensitive) == 0)
            result.params.append({m_authKey->text(), m_authValue->text()});
        else
            result.headers.append({m_authKey->text(), m_authValue->text()});
    }
    if (!result.body.isEmpty()) {
        const auto contentType = m_bodyType->currentIndex() == 0
            ? QStringLiteral("application/json")
            : m_bodyType->currentIndex() == 1 ? QStringLiteral("application/xml")
                                               : QStringLiteral("text/plain");
        bool hasContentType = false;
        for (const auto &header : result.headers)
            hasContentType |= header.first.compare(QStringLiteral("Content-Type"),
                                                   Qt::CaseInsensitive) == 0;
        if (!hasContentType)
            result.headers.append({QStringLiteral("Content-Type"), contentType});
    }
    return result;
}

void RequestPanel::setRequest(const ApiRequest &request) {
    setTablePairs(m_params, request.params);
    setTablePairs(m_headers, request.headers);
    m_body->setPlainText(QString::fromUtf8(request.body));
    m_authType->setCurrentIndex(0);
    m_authKey->clear();
    m_authValue->clear();
    m_authExtra->clear();
}
