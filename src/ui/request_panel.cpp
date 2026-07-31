#include "request_panel.h"
#include "text_search_bar.h"

#include "../utils/source_formatter.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTabWidget>
#include <QStackedWidget>
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
    m_bodyType->addItems({tr("None"), tr("Raw JSON"), tr("Raw XML"),
                          tr("Raw HTML"), tr("Raw JavaScript"), tr("Raw Text"),
                          tr("x-www-form-urlencoded"), tr("form-data"),
                          tr("Binary file")});
    m_pretty = new QCheckBox(tr("Pretty"), bodyPage);
    m_pretty->setToolTip(tr("Show a formatted preview without changing the payload sent"));
    m_body = new QPlainTextEdit(bodyPage);
    m_body->setPlaceholderText(tr("Request body"));
    m_body->setLineWrapMode(QPlainTextEdit::NoWrap);
    new JsonHighlighter(m_body->document());
    m_urlEncoded = createPairTable(bodyPage);
    m_formData = new QTableWidget(1, 3, bodyPage);
    m_formData->setHorizontalHeaderLabels({tr("Key"), tr("Value / file"), tr("Type")});
    m_formData->horizontalHeader()->setStretchLastSection(true);
    m_formData->verticalHeader()->setVisible(false);
    connect(m_formData, &QTableWidget::cellChanged, m_formData,
            [this](int row, int) {
                if (row != m_formData->rowCount() - 1)
                    return;
                bool used = false;
                for (int column = 0; column < m_formData->columnCount(); ++column) {
                    const auto *item = m_formData->item(row, column);
                    used |= item && !item->text().isEmpty();
                }
                if (used) {
                    const auto nextRow = m_formData->rowCount();
                    m_formData->insertRow(nextRow);
                }
            });
    m_formData->setItem(0, 2, new QTableWidgetItem(tr("Text")));
    auto *formPage = new QWidget(bodyPage);
    auto *formLayout = new QVBoxLayout(formPage);
    formLayout->setContentsMargins(0, 0, 0, 0);
    auto *selectFile = new QPushButton(tr("Set selected value to file…"), formPage);
    formLayout->addWidget(selectFile, 0, Qt::AlignLeft);
    formLayout->addWidget(m_formData);
    connect(selectFile, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, tr("Choose upload file"));
        if (path.isEmpty())
            return;
        const auto row = qMax(0, m_formData->currentRow());
        if (!m_formData->item(row, 1))
            m_formData->setItem(row, 1, new QTableWidgetItem);
        m_formData->item(row, 1)->setText(path);
        if (!m_formData->item(row, 2))
            m_formData->setItem(row, 2, new QTableWidgetItem);
        m_formData->item(row, 2)->setText(tr("File"));
    });
    auto *binaryPage = new QWidget(bodyPage);
    auto *binaryLayout = new QHBoxLayout(binaryPage);
    binaryLayout->setContentsMargins(0, 0, 0, 0);
    m_binaryPath = new QLineEdit(binaryPage);
    m_binaryPath->setPlaceholderText(tr("Choose a binary request body"));
    auto *browseBinary = new QPushButton(tr("Browse…"), binaryPage);
    binaryLayout->addWidget(m_binaryPath);
    binaryLayout->addWidget(browseBinary);
    connect(browseBinary, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, tr("Choose binary request body"));
        if (!path.isEmpty())
            m_binaryPath->setText(path);
    });
    m_bodyStack = new QStackedWidget(bodyPage);
    m_bodyStack->addWidget(m_body);
    m_bodyStack->addWidget(m_urlEncoded);
    m_bodyStack->addWidget(formPage);
    m_bodyStack->addWidget(binaryPage);

    auto *bodyControls = new QHBoxLayout;
    auto *wrap = new QCheckBox(tr("Wrap"), bodyPage);
    auto *find = new QPushButton(tr("Find"), bodyPage);
    bodyControls->addWidget(m_bodyType);
    bodyControls->addWidget(m_pretty);
    bodyControls->addStretch();
    bodyControls->addWidget(find);
    bodyControls->addWidget(wrap);
    bodyLayout->addLayout(bodyControls);
    auto *search = new TextSearchBar(m_body, true, bodyPage);
    bodyLayout->addWidget(m_bodyStack);
    connect(wrap, &QCheckBox::toggled, this, [this](bool enabled) {
        m_body->setLineWrapMode(enabled ? QPlainTextEdit::WidgetWidth
                                        : QPlainTextEdit::NoWrap);
    });
    connect(find, &QPushButton::clicked, search,
            [search] { search->showFind(); });
    connect(m_body, &QPlainTextEdit::textChanged, this, [this] {
        if (!m_updatingBody && !m_pretty->isChecked())
            m_rawBody = m_body->toPlainText();
    });
    connect(m_pretty, &QCheckBox::toggled, this,
            [this] { updateBodyView(); });
    connect(m_bodyType, &QComboBox::currentIndexChanged, this, [this](int index) {
        const bool raw = index >= 1 && index <= 5;
        if (!raw && m_pretty->isChecked())
            m_pretty->setChecked(false);
        m_pretty->setEnabled(raw && index != 5);
        m_bodyStack->setCurrentIndex(index >= 1 && index <= 5 ? 0
                                        : index == 6 ? 1 : index == 7 ? 2
                                        : index == 8 ? 3 : 0);
        m_body->setEnabled(raw);
        if (m_pretty->isChecked())
            updateBodyView();
    });
    m_bodyType->setCurrentIndex(0);

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

void RequestPanel::updateBodyView() {
    m_updatingBody = true;
    if (m_pretty->isChecked()) {
        SourceFormatter::Language language = SourceFormatter::Language::PlainText;
        switch (m_bodyType->currentIndex()) {
        case 1: language = SourceFormatter::Language::Json; break;
        case 2: language = SourceFormatter::Language::Xml; break;
        case 3: language = SourceFormatter::Language::Html; break;
        case 4: language = SourceFormatter::Language::JavaScript; break;
        default: break;
        }
        m_body->setPlainText(SourceFormatter::pretty(m_rawBody, language));
        m_body->setReadOnly(true);
    } else {
        m_body->setReadOnly(false);
        m_body->setPlainText(m_rawBody);
    }
    m_updatingBody = false;
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
    const auto bodyType = m_bodyType->currentIndex();
    if (bodyType == 0) {
        result.bodyMode = RequestBodyMode::None;
    } else if (bodyType >= 1 && bodyType <= 5) {
        result.bodyMode = RequestBodyMode::Raw;
        result.body = m_rawBody.toUtf8();
        static const QStringList contentTypes = {
            QString(), QStringLiteral("application/json"), QStringLiteral("application/xml"),
            QStringLiteral("text/html"), QStringLiteral("application/javascript"),
            QStringLiteral("text/plain")};
        result.bodyContentType = contentTypes.at(bodyType);
    } else if (bodyType == 6) {
        result.bodyMode = RequestBodyMode::UrlEncoded;
        for (const auto &pair : tablePairs(m_urlEncoded))
            result.bodyEntries.append({pair.first, pair.second});
        result.bodyContentType = QStringLiteral("application/x-www-form-urlencoded");
    } else if (bodyType == 7) {
        result.bodyMode = RequestBodyMode::Multipart;
        for (int row = 0; row < m_formData->rowCount(); ++row) {
            const auto *key = m_formData->item(row, 0);
            if (!key || key->text().trimmed().isEmpty())
                continue;
            const auto *value = m_formData->item(row, 1);
            const auto *type = m_formData->item(row, 2);
            RequestBodyEntry entry;
            entry.key = key->text().trimmed();
            entry.value = value ? value->text() : QString();
            entry.isFile = type && type->text().compare(tr("File"), Qt::CaseInsensitive) == 0;
            if (entry.isFile)
                entry.filePath = entry.value;
            result.bodyEntries.append(entry);
        }
        result.bodyContentType = QStringLiteral("multipart/form-data");
    } else {
        result.bodyMode = RequestBodyMode::Binary;
        result.binaryFilePath = m_binaryPath->text();
        result.bodyContentType = QStringLiteral("application/octet-stream");
    }
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
    if ((result.bodyMode == RequestBodyMode::Raw && !result.body.isEmpty())
        || result.bodyMode == RequestBodyMode::Binary) {
        bool hasContentType = false;
        for (const auto &header : result.headers)
            hasContentType |= header.first.compare(QStringLiteral("Content-Type"),
                                                   Qt::CaseInsensitive) == 0;
        if (!hasContentType)
            result.headers.append({QStringLiteral("Content-Type"), result.bodyContentType});
    }
    return result;
}

void RequestPanel::setRequest(const ApiRequest &request) {
    setTablePairs(m_params, request.params);
    setTablePairs(m_headers, request.headers);
    int bodyType = 0;
    QString contentType = request.bodyContentType;
    for (const auto &header : request.headers) {
        if (header.first.compare(QStringLiteral("Content-Type"), Qt::CaseInsensitive) == 0) {
            contentType = header.second;
            break;
        }
    }
    contentType = contentType.toLower();
    if (request.bodyMode == RequestBodyMode::UrlEncoded)
        bodyType = 6;
    else if (request.bodyMode == RequestBodyMode::Multipart)
        bodyType = 7;
    else if (request.bodyMode == RequestBodyMode::Binary)
        bodyType = 8;
    else if (request.bodyMode == RequestBodyMode::Raw || !request.body.isEmpty())
        bodyType = contentType.contains(QStringLiteral("xml")) ? 2
            : contentType.contains(QStringLiteral("html")) ? 3
            : contentType.contains(QStringLiteral("javascript")) ? 4
            : contentType.contains(QStringLiteral("json")) ? 1 : 5;
    m_bodyType->setCurrentIndex(bodyType);
    m_rawBody = QString::fromUtf8(request.body);
    QList<QPair<QString, QString>> encoded;
    for (const auto &entry : request.bodyEntries)
        encoded.append({entry.key, entry.value});
    setTablePairs(m_urlEncoded, encoded);
    m_formData->blockSignals(true);
    m_formData->setRowCount(request.bodyEntries.size() + 1);
    for (int row = 0; row < request.bodyEntries.size(); ++row) {
        const auto &entry = request.bodyEntries.at(row);
        m_formData->setItem(row, 0, new QTableWidgetItem(entry.key));
        m_formData->setItem(row, 1, new QTableWidgetItem(entry.isFile ? entry.filePath : entry.value));
        m_formData->setItem(row, 2, new QTableWidgetItem(entry.isFile ? tr("File") : tr("Text")));
    }
    m_formData->setItem(request.bodyEntries.size(), 2, new QTableWidgetItem(tr("Text")));
    m_formData->blockSignals(false);
    m_binaryPath->setText(request.binaryFilePath);
    updateBodyView();
    m_authType->setCurrentIndex(0);
    m_authKey->clear();
    m_authValue->clear();
    m_authExtra->clear();
}
