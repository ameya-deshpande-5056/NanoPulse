#include "response_viewer.h"
#include "text_search_bar.h"

#include "../utils/json_helper.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QVBoxLayout>

ResponseViewer::ResponseViewer(QWidget *parent) : QWidget(parent) {
    auto *summary = new QHBoxLayout;
    m_status = new QLabel(tr("Status: —"), this);
    m_time = new QLabel(tr("Time: —"), this);
    m_size = new QLabel(tr("Size: —"), this);
    auto *copy = new QPushButton(tr("Copy"), this);
    auto *find = new QPushButton(tr("Find"), this);
    auto *wrap = new QCheckBox(tr("Wrap"), this);
    summary->addWidget(m_status);
    summary->addWidget(m_time);
    summary->addWidget(m_size);
    summary->addStretch();
    summary->addWidget(wrap);
    summary->addWidget(find);
    summary->addWidget(copy);

    auto *tabs = new QTabWidget(this);
    auto *bodyPage = new QWidget(tabs);
    m_body = new QPlainTextEdit(bodyPage);
    m_body->setReadOnly(true);
    m_body->setLineWrapMode(QPlainTextEdit::NoWrap);
    auto *search = new TextSearchBar(m_body, false, bodyPage);
    auto *bodyLayout = new QVBoxLayout;
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->addWidget(m_body);
    bodyPage->setLayout(bodyLayout);
    tabs->addTab(bodyPage, tr("Body"));

    auto *headersPage = new QWidget(tabs);
    auto *headersLayout = new QVBoxLayout(headersPage);
    m_headerSearch = new QLineEdit(headersPage);
    m_headerSearch->setPlaceholderText(tr("Filter response headers"));
    m_headers = new QTableWidget(0, 2, headersPage);
    m_headers->setHorizontalHeaderLabels({tr("Header"), tr("Value")});
    m_headers->horizontalHeader()->setStretchLastSection(true);
    m_headers->verticalHeader()->setVisible(false);
    m_headers->setEditTriggers(QAbstractItemView::NoEditTriggers);
    headersLayout->addWidget(m_headerSearch);
    headersLayout->addWidget(m_headers);
    tabs->addTab(headersPage, tr("Headers"));

    auto *assertions = new QHBoxLayout;
    m_expectedStatus = new QLineEdit(this);
    m_expectedStatus->setPlaceholderText(tr("Expected status"));
    m_expectedStatus->setMaximumWidth(130);
    m_maxTime = new QLineEdit(this);
    m_maxTime->setPlaceholderText(tr("Max time (ms)"));
    m_maxTime->setMaximumWidth(130);
    m_assertion = new QLabel(this);
    assertions->addWidget(new QLabel(tr("Assertions:"), this));
    assertions->addWidget(m_expectedStatus);
    assertions->addWidget(m_maxTime);
    assertions->addWidget(m_assertion);
    assertions->addStretch();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(summary);
    layout->addWidget(tabs);
    layout->addLayout(assertions);

    connect(copy, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(m_body->toPlainText());
    });
    connect(wrap, &QCheckBox::toggled, this, [this](bool enabled) {
        m_body->setLineWrapMode(enabled ? QPlainTextEdit::WidgetWidth
                                        : QPlainTextEdit::NoWrap);
    });
    connect(find, &QPushButton::clicked, search,
            [search] { search->showFind(); });
    connect(m_headerSearch, &QLineEdit::textChanged,
            this, &ResponseViewer::filterHeaders);
}

void ResponseViewer::begin() {
    m_data.clear();
    m_streamingLarge = false;
    m_body->clear();
    m_headers->setRowCount(0);
    m_status->setText(tr("Status: waiting"));
    m_time->setText(tr("Time: —"));
    m_size->setText(tr("Size: —"));
    m_assertion->clear();
}

void ResponseViewer::appendChunk(const QByteArray &chunk) {
    constexpr qsizetype displayBufferLimit = 16 * 1024 * 1024;
    if (!m_streamingLarge && m_data.size() + chunk.size() <= displayBufferLimit) {
        m_data.append(chunk);
        return;
    }
    if (!m_streamingLarge) {
        m_streamingLarge = true;
        m_body->setPlainText(QString::fromUtf8(m_data));
        m_data.clear();
    }
    m_body->moveCursor(QTextCursor::End);
    m_body->insertPlainText(QString::fromUtf8(chunk));
}

void ResponseViewer::finish(
    int statusCode, const QList<QPair<QByteArray, QByteArray>> &headers,
    qint64 elapsedMs, qint64 bytes, const QString &error) {
    if (!m_streamingLarge)
        m_body->setPlainText(JsonHelper::pretty(m_data));
    m_data.clear();
    const auto statusStyle = statusCode >= 200 && statusCode < 300
        ? QStringLiteral("statusSuccess")
        : statusCode >= 300 && statusCode < 400 ? QStringLiteral("statusRedirect")
                                                 : QStringLiteral("statusError");
    m_status->setProperty("responseClass", statusStyle);
    m_status->style()->unpolish(m_status);
    m_status->style()->polish(m_status);
    m_status->setText(error.isEmpty()
                          ? tr("Status: %1").arg(statusCode)
                          : tr("Error: %1").arg(error));
    m_time->setText(tr("Time: %1 ms").arg(elapsedMs));
    m_size->setText(bytes < 1024
                        ? tr("Size: %1 B").arg(bytes)
                        : tr("Size: %1 KB").arg(bytes / 1024.0, 0, 'f', 1));

    m_headers->setRowCount(headers.size());
    for (int row = 0; row < headers.size(); ++row) {
        m_headers->setItem(
            row, 0, new QTableWidgetItem(QString::fromLatin1(headers.at(row).first)));
        m_headers->setItem(
            row, 1, new QTableWidgetItem(QString::fromLatin1(headers.at(row).second)));
    }

    bool passed = true;
    if (!m_expectedStatus->text().isEmpty())
        passed &= m_expectedStatus->text().toInt() == statusCode;
    if (!m_maxTime->text().isEmpty())
        passed &= elapsedMs <= m_maxTime->text().toLongLong();
    if (!m_expectedStatus->text().isEmpty() || !m_maxTime->text().isEmpty())
        m_assertion->setText(passed ? tr("PASS") : tr("FAIL"));
    emit assertionsEvaluated(passed);
}

QByteArray ResponseViewer::responseData() const {
    return m_body->toPlainText().toUtf8();
}

void ResponseViewer::filterHeaders(const QString &text) {
    for (int row = 0; row < m_headers->rowCount(); ++row) {
        const auto *name = m_headers->item(row, 0);
        const auto *value = m_headers->item(row, 1);
        m_headers->setRowHidden(
            row, !(text.isEmpty()
                   || (name && name->text().contains(text, Qt::CaseInsensitive))
                   || (value && value->text().contains(text, Qt::CaseInsensitive))));
    }
}
