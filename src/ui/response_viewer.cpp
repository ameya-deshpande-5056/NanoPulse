#include "response_viewer.h"
#include "text_search_bar.h"

#include "../utils/json_helper.h"
#include "../utils/source_formatter.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QTextCharFormat>
#include <QVBoxLayout>

class ResponseSyntaxHighlighter final : public QSyntaxHighlighter {
public:
    enum class Language { PlainText, Json, Xml, Html, Css, JavaScript };

    explicit ResponseSyntaxHighlighter(QTextDocument *document)
        : QSyntaxHighlighter(document) {}

    void setLanguage(Language language) {
        if (m_language == language)
            return;
        m_language = language;
        rehighlight();
    }

protected:
    void highlightBlock(const QString &text) override {
        setCurrentBlockState(0);
        switch (m_language) {
        case Language::Json:
            highlightJson(text);
            break;
        case Language::Html:
        case Language::Xml:
            highlightHtml(text);
            break;
        case Language::Css:
            highlightCss(text);
            break;
        case Language::JavaScript:
            highlightJavaScript(text);
            break;
        case Language::PlainText:
            break;
        }
    }

private:
    static QTextCharFormat format(const char *color, bool bold = false) {
        QTextCharFormat result;
        result.setForeground(QColor(QString::fromLatin1(color)));
        if (bold)
            result.setFontWeight(QFont::DemiBold);
        return result;
    }

    void apply(const QString &text, const QString &pattern,
               const QTextCharFormat &textFormat, int capture = 0,
               qsizetype offset = 0) {
        const QRegularExpression expression(pattern);
        auto matches = expression.globalMatch(text);
        while (matches.hasNext()) {
            const auto match = matches.next();
            setFormat(offset + match.capturedStart(capture),
                      match.capturedLength(capture),
                      textFormat);
        }
    }

    void applyMultilineComment(const QString &text, const QString &opening,
                               const QString &closing,
                               const QTextCharFormat &commentFormat) {
        int start = previousBlockState() == 1 ? 0 : text.indexOf(opening);
        while (start >= 0) {
            const int searchFrom = start + (previousBlockState() == 1 ? 0
                                                                       : opening.size());
            const int end = text.indexOf(closing, searchFrom);
            if (end < 0) {
                setFormat(start, text.size() - start, commentFormat);
                setCurrentBlockState(1);
                return;
            }
            const int length = end - start + closing.size();
            setFormat(start, length, commentFormat);
            start = text.indexOf(opening, start + length);
        }
    }

    void highlightJson(const QString &text) {
        apply(text, QStringLiteral(R"("(?:\\.|[^"\\])*")"),
              format("#34a853"));
        apply(text, QStringLiteral("\"(?:\\\\.|[^\"\\\\])*\"\\s*:"),
              format("#4c8bf5", true));
        apply(text, QStringLiteral(
                        R"(\b(?:true|false|null|-?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?)\b)"),
              format("#d79b00"));
    }

    void highlightHtml(const QString &text) {
        const QRegularExpression tags(QStringLiteral(
            R"(<(?:[^>"']|"[^"]*"|'[^']*')*>)"));
        auto tagMatches = tags.globalMatch(text);
        while (tagMatches.hasNext()) {
            const auto tagMatch = tagMatches.next();
            const auto tag = tagMatch.captured();
            const auto offset = tagMatch.capturedStart();
            apply(tag, QStringLiteral(R"(<!DOCTYPE\b[^>]*>)"),
                  format("#a142f4"), 0, offset);
            apply(tag, QStringLiteral(R"(</?\s*[A-Za-z][\w:-]*)"),
                  format("#4c8bf5", true), 0, offset);
            apply(tag, QStringLiteral(R"(\b[A-Za-z_:][\w:.-]*(?=\s*=))"),
                  format("#a142f4"), 0, offset);
            apply(tag, QStringLiteral(R"("[^"\r\n]*"|'[^'\r\n]*')"),
                  format("#34a853"), 0, offset);
            highlightInlineAttributes(tag, offset);
        }
        apply(text, QStringLiteral(R"(&(?:#\d+|#x[\da-fA-F]+|\w+);)"),
              format("#d79b00"));

        const auto previousState = previousBlockState();
        if (previousState == ScriptState) {
            if (!highlightEmbedded(text, QStringLiteral("script"), ScriptState,
                                   true))
                highlightEmbedded(text, QStringLiteral("style"), StyleState,
                                  false);
        } else if (previousState == StyleState) {
            if (!highlightEmbedded(text, QStringLiteral("style"), StyleState,
                                   false))
                highlightEmbedded(text, QStringLiteral("script"), ScriptState,
                                  true);
        } else {
            if (!highlightEmbedded(text, QStringLiteral("script"), ScriptState,
                                   true))
                highlightEmbedded(text, QStringLiteral("style"), StyleState,
                                  false);
        }
        applyMultilineComment(text, QStringLiteral("<!--"), QStringLiteral("-->"),
                              format("#80868b"));
    }

    void highlightInlineAttributes(const QString &tag, qsizetype tagOffset) {
        const QRegularExpression inlineCss(QStringLiteral(
            R"(\bstyle\s*=\s*(["'])(.*?)\1)"),
            QRegularExpression::CaseInsensitiveOption);
        auto cssMatches = inlineCss.globalMatch(tag);
        while (cssMatches.hasNext()) {
            const auto match = cssMatches.next();
            const auto offset = tagOffset + match.capturedStart(2);
            setFormat(offset, match.capturedLength(2), QTextCharFormat());
            highlightCss(match.captured(2), offset, false);
        }

        const QRegularExpression inlineJavaScript(QStringLiteral(
            R"(\bon[\w:-]+\s*=\s*(["'])(.*?)\1)"),
            QRegularExpression::CaseInsensitiveOption);
        auto scriptMatches = inlineJavaScript.globalMatch(tag);
        while (scriptMatches.hasNext()) {
            const auto match = scriptMatches.next();
            const auto offset = tagOffset + match.capturedStart(2);
            setFormat(offset, match.capturedLength(2), QTextCharFormat());
            highlightJavaScript(match.captured(2), offset, false);
        }
    }

    bool highlightEmbedded(const QString &text, const QString &tagName,
                           int state, bool javaScript) {
        qsizetype cursor = 0;
        if (previousBlockState() == state) {
            const auto closing = text.indexOf(QStringLiteral("</") + tagName, 0,
                                               Qt::CaseInsensitive);
            const auto length = closing < 0 ? text.size() : closing;
            setFormat(0, length, QTextCharFormat());
            if (javaScript)
                highlightJavaScript(text.left(length), 0, false);
            else
                highlightCss(text.left(length), 0, false);
            if (closing < 0) {
                setCurrentBlockState(state);
                return true;
            }
            const auto closingEnd = text.indexOf(QLatin1Char('>'), closing);
            cursor = closingEnd < 0 ? text.size() : closingEnd + 1;
        }

        const QRegularExpression openingTag(
            QStringLiteral("<\\s*") + tagName + QStringLiteral(R"(\b[^>]*>)"),
            QRegularExpression::CaseInsensitiveOption);
        while (cursor < text.size()) {
            const auto opening = openingTag.match(text, cursor);
            if (!opening.hasMatch())
                break;
            const auto contentStart = opening.capturedEnd();
            const auto closing = text.indexOf(QStringLiteral("</") + tagName,
                                               contentStart, Qt::CaseInsensitive);
            const auto contentEnd = closing < 0 ? text.size() : closing;
            const auto content = text.mid(contentStart, contentEnd - contentStart);
            setFormat(contentStart, content.size(), QTextCharFormat());
            if (javaScript)
                highlightJavaScript(content, contentStart, false);
            else
                highlightCss(content, contentStart, false);
            if (closing < 0) {
                setCurrentBlockState(state);
                return true;
            }
            const auto closingEnd = text.indexOf(QLatin1Char('>'), closing);
            cursor = closingEnd < 0 ? text.size() : closingEnd + 1;
        }
        return false;
    }

    void highlightCss(const QString &text, qsizetype offset = 0,
                      bool trackMultilineComments = true) {
        apply(text, QStringLiteral(R"(@[-\w]+)"), format("#a142f4", true), 0,
              offset);
        apply(text, QStringLiteral(R"((^|[;{])\s*([-\w]+)\s*(?=:))"),
              format("#4c8bf5", true), 2, offset);
        apply(text, QStringLiteral(R"(#[\da-fA-F]{3,8}\b)"), format("#d79b00"),
              0, offset);
        apply(text, QStringLiteral(
                        R"(\b-?(?:\d+\.?\d*|\.\d+)(?:%|[a-zA-Z]+)?\b)"),
              format("#d79b00"), 0, offset);
        apply(text, QStringLiteral(R"("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')"),
              format("#34a853"), 0, offset);
        apply(text, QStringLiteral(R"(!important\b)"), format("#ea4335", true),
              0, offset);
        if (trackMultilineComments) {
            applyMultilineComment(text, QStringLiteral("/*"), QStringLiteral("*/"),
                                  format("#80868b"));
        } else {
            apply(text, QStringLiteral(R"(/\*.*?\*/)"), format("#80868b"), 0,
                  offset);
        }
    }

    void highlightJavaScript(const QString &text, qsizetype offset = 0,
                             bool trackMultilineComments = true) {
        apply(text, QStringLiteral(
                        R"(\b(?:async|await|break|case|catch|class|const|continue|debugger|default|delete|do|else|export|extends|false|finally|for|from|function|get|if|import|in|instanceof|let|new|null|of|return|set|static|super|switch|this|throw|true|try|typeof|undefined|var|void|while|with|yield)\b)"),
              format("#a142f4", true), 0, offset);
        apply(text, QStringLiteral(R"(\b[A-Za-z_$][\w$]*(?=\s*\())"),
              format("#4c8bf5"), 0, offset);
        apply(text, QStringLiteral(R"(\b(?:0[xX][\da-fA-F]+|\d+(?:\.\d+)?)\b)"),
              format("#d79b00"), 0, offset);
        apply(text, QStringLiteral(
                        R"("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|`(?:\\.|[^`\\])*`)"),
              format("#34a853"), 0, offset);
        apply(text, QStringLiteral(R"(//.*$)"), format("#80868b"), 0, offset);
        if (trackMultilineComments) {
            applyMultilineComment(text, QStringLiteral("/*"), QStringLiteral("*/"),
                                  format("#80868b"));
        } else {
            apply(text, QStringLiteral(R"(/\*.*?\*/)"), format("#80868b"), 0,
                  offset);
        }
    }

    static constexpr int ScriptState = 2;
    static constexpr int StyleState = 3;
    Language m_language = Language::PlainText;
};

namespace {
ResponseSyntaxHighlighter::Language responseLanguage(
    const QList<QPair<QByteArray, QByteArray>> &headers, const QByteArray &data,
    bool validJson) {
    QByteArray contentType;
    for (const auto &header : headers) {
        if (header.first.compare("Content-Type", Qt::CaseInsensitive) == 0) {
            contentType = header.second.toLower();
            break;
        }
    }

    if (contentType.contains("json"))
        return ResponseSyntaxHighlighter::Language::Json;
    if (contentType.contains("html") || contentType.contains("xhtml"))
        return ResponseSyntaxHighlighter::Language::Html;
    if (contentType.contains("xml"))
        return ResponseSyntaxHighlighter::Language::Xml;
    if (contentType.contains("css"))
        return ResponseSyntaxHighlighter::Language::Css;
    if (contentType.contains("javascript") || contentType.contains("ecmascript"))
        return ResponseSyntaxHighlighter::Language::JavaScript;
    if (validJson)
        return ResponseSyntaxHighlighter::Language::Json;

    const auto trimmed = data.trimmed();
    const auto lowered = trimmed.toLower();
    if (lowered.startsWith("<!doctype html") || lowered.startsWith("<html")
        || QRegularExpression(QStringLiteral(R"(^</?[A-Za-z][\w:-]*(?:\s|>|/))"))
               .match(QString::fromUtf8(trimmed.left(1024))).hasMatch())
        return ResponseSyntaxHighlighter::Language::Html;

    const auto sample = QString::fromUtf8(trimmed.left(8192));
    if (QRegularExpression(QStringLiteral(
            R"(^\s*(?:@(?:charset|import|namespace)\b|[.#]?[A-Za-z][^{};]*\{[^{}]*[-\w]+\s*:))"))
            .match(sample).hasMatch())
        return ResponseSyntaxHighlighter::Language::Css;
    if (QRegularExpression(QStringLiteral(
            R"(^\s*(?:(?:const|let|var|function|class|import|export|async)\b|(?:\([^)]*\)|[A-Za-z_$][\w$]*)\s*=>))"))
            .match(sample).hasMatch())
        return ResponseSyntaxHighlighter::Language::JavaScript;

    return ResponseSyntaxHighlighter::Language::PlainText;
}

SourceFormatter::Language formatterLanguage(
    ResponseSyntaxHighlighter::Language language) {
    switch (language) {
    case ResponseSyntaxHighlighter::Language::Json:
        return SourceFormatter::Language::Json;
    case ResponseSyntaxHighlighter::Language::Xml:
        return SourceFormatter::Language::Xml;
    case ResponseSyntaxHighlighter::Language::Html:
        return SourceFormatter::Language::Html;
    case ResponseSyntaxHighlighter::Language::Css:
        return SourceFormatter::Language::Css;
    case ResponseSyntaxHighlighter::Language::JavaScript:
        return SourceFormatter::Language::JavaScript;
    case ResponseSyntaxHighlighter::Language::PlainText:
        return SourceFormatter::Language::PlainText;
    }
    return SourceFormatter::Language::PlainText;
}
}

ResponseViewer::ResponseViewer(QWidget *parent) : QWidget(parent) {
    auto *summary = new QHBoxLayout;
    m_status = new QLabel(tr("Status: —"), this);
    m_time = new QLabel(tr("Time: —"), this);
    m_size = new QLabel(tr("Size: —"), this);
    auto *copy = new QPushButton(tr("Copy"), this);
    auto *find = new QPushButton(tr("Find"), this);
    m_pretty = new QCheckBox(tr("Pretty"), this);
    m_pretty->setChecked(true);
    m_pretty->setEnabled(false);
    auto *wrap = new QCheckBox(tr("Wrap"), this);
    summary->addWidget(m_status);
    summary->addWidget(m_time);
    summary->addWidget(m_size);
    summary->addStretch();
    summary->addWidget(m_pretty);
    summary->addWidget(wrap);
    summary->addWidget(find);
    summary->addWidget(copy);

    auto *tabs = new QTabWidget(this);
    auto *bodyPage = new QWidget(tabs);
    m_body = new QPlainTextEdit(bodyPage);
    m_body->setReadOnly(true);
    m_body->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_syntaxHighlighter = new ResponseSyntaxHighlighter(m_body->document());
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
    connect(m_pretty, &QCheckBox::toggled,
            this, &ResponseViewer::updateBodyView);
    connect(m_headerSearch, &QLineEdit::textChanged,
            this, &ResponseViewer::filterHeaders);
}

void ResponseViewer::begin() {
    m_data.clear();
    m_rawBody.clear();
    m_prettyBody.clear();
    m_streamingLarge = false;
    m_pretty->setEnabled(false);
    m_syntaxHighlighter->setLanguage(
        ResponseSyntaxHighlighter::Language::PlainText);
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
    bool validJson = false;
    if (!m_streamingLarge) {
        JsonHelper::pretty(m_data, &validJson);
        const auto language = responseLanguage(headers, m_data, validJson);
        m_rawBody = QString::fromUtf8(m_data);
        bool formatted = false;
        m_prettyBody = SourceFormatter::pretty(
            m_rawBody, formatterLanguage(language), &formatted);
        m_syntaxHighlighter->setLanguage(language);
        m_pretty->setEnabled(formatted);
        updateBodyView();
    }
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

void ResponseViewer::updateBodyView() {
    if (m_streamingLarge)
        return;
    m_body->setPlainText(m_pretty->isChecked() && m_pretty->isEnabled()
                             ? m_prettyBody : m_rawBody);
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
