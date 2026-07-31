#include "source_formatter.h"

#include "json_helper.h"

#include <QRegularExpression>
#include <QStringList>

namespace {
void appendLine(QStringList &lines, int indentation, const QString &text) {
    const auto trimmed = text.trimmed();
    if (!trimmed.isEmpty())
        lines.append(QString(indentation * 2, QLatin1Char(' ')) + trimmed);
}

void appendText(QStringList &lines, int indentation, const QString &text) {
    for (const auto &line : text.split(QLatin1Char('\n')))
        appendLine(lines, indentation, line);
}

QString formatBracedSource(const QString &source, bool css);

QString formatMarkup(const QString &source) {
    static const QRegularExpression voidElement(QStringLiteral(
        R"(^<\s*(?:area|base|br|col|embed|hr|img|input|link|meta|param|source|track|wbr)\b)"),
        QRegularExpression::CaseInsensitiveOption);

    QStringList lines;
    int indentation = 0;
    qsizetype position = 0;
    while (position < source.size()) {
        const auto opening = source.indexOf(QLatin1Char('<'), position);
        if (opening < 0) {
            appendText(lines, indentation, source.mid(position));
            break;
        }
        appendText(lines, indentation, source.mid(position, opening - position));

        qsizetype closing = -1;
        if (source.mid(opening, 4) == QStringLiteral("<!--")) {
            const auto commentEnd = source.indexOf(QStringLiteral("-->"), opening + 4);
            closing = commentEnd < 0 ? source.size() - 1 : commentEnd + 2;
        } else if (source.mid(opening, 9) == QStringLiteral("<![CDATA[")) {
            const auto cdataEnd = source.indexOf(QStringLiteral("]]>"), opening + 9);
            closing = cdataEnd < 0 ? source.size() - 1 : cdataEnd + 2;
        } else {
            QChar quote;
            for (qsizetype index = opening + 1; index < source.size(); ++index) {
                const auto character = source.at(index);
                if (!quote.isNull()) {
                    if (character == quote && source.at(index - 1) != QLatin1Char('\\'))
                        quote = {};
                } else if (character == QLatin1Char('\'')
                           || character == QLatin1Char('"')) {
                    quote = character;
                } else if (character == QLatin1Char('>')) {
                    closing = index;
                    break;
                }
            }
            if (closing < 0)
                closing = source.size() - 1;
        }

        const auto tag = source.mid(opening, closing - opening + 1).trimmed();
        const bool closingTag = tag.startsWith(QStringLiteral("</"));
        const bool declaration = tag.startsWith(QStringLiteral("<!"))
            || tag.startsWith(QStringLiteral("<?"));
        const bool selfClosing = tag.endsWith(QStringLiteral("/>"))
            || voidElement.match(tag).hasMatch();
        if (closingTag)
            indentation = qMax(0, indentation - 1);
        appendLine(lines, indentation, tag);
        if (!closingTag && !declaration && !selfClosing) {
            ++indentation;
            const auto nameMatch = QRegularExpression(QStringLiteral(
                R"(^<\s*([A-Za-z][\w:-]*))")).match(tag);
            const auto name = nameMatch.captured(1).toLower();
            if (name == QStringLiteral("script") || name == QStringLiteral("style")) {
                const auto closingStart = source.indexOf(
                    QStringLiteral("</") + name, closing + 1,
                    Qt::CaseInsensitive);
                if (closingStart >= 0) {
                    const auto nested = formatBracedSource(
                        source.mid(closing + 1, closingStart - closing - 1),
                        name == QStringLiteral("style"));
                    for (const auto &nestedLine : nested.split(QLatin1Char('\n'))) {
                        if (!nestedLine.trimmed().isEmpty())
                            lines.append(QString(indentation * 2, QLatin1Char(' '))
                                         + nestedLine);
                    }
                    const auto closingEnd = source.indexOf(QLatin1Char('>'),
                                                           closingStart + 2);
                    indentation = qMax(0, indentation - 1);
                    if (closingEnd < 0) {
                        appendLine(lines, indentation, source.mid(closingStart));
                        position = source.size();
                    } else {
                        appendLine(lines, indentation,
                                   source.mid(closingStart,
                                              closingEnd - closingStart + 1));
                        position = closingEnd + 1;
                    }
                    continue;
                }
            }
        }
        position = closing + 1;
    }
    return lines.join(QLatin1Char('\n'));
}

QString formatBracedSource(const QString &source, bool css) {
    enum class State { Normal, SingleQuote, DoubleQuote, Template, LineComment,
                       BlockComment };
    State state = State::Normal;
    QStringList lines;
    QString line;
    int indentation = 0;
    int parentheses = 0;
    bool escaped = false;

    const auto flush = [&] {
        appendLine(lines, indentation, line);
        line.clear();
    };

    for (qsizetype index = 0; index < source.size(); ++index) {
        const auto character = source.at(index);
        const auto next = index + 1 < source.size() ? source.at(index + 1) : QChar();

        if (state == State::LineComment) {
            if (character == QLatin1Char('\n')) {
                flush();
                state = State::Normal;
            } else {
                line += character;
            }
            continue;
        }
        if (state == State::BlockComment) {
            line += character;
            if (character == QLatin1Char('*') && next == QLatin1Char('/')) {
                line += next;
                ++index;
                state = State::Normal;
            } else if (character == QLatin1Char('\n')) {
                flush();
            }
            continue;
        }
        if (state != State::Normal) {
            line += character;
            if (escaped) {
                escaped = false;
            } else if (character == QLatin1Char('\\')) {
                escaped = true;
            } else if ((state == State::SingleQuote && character == QLatin1Char('\''))
                       || (state == State::DoubleQuote
                           && character == QLatin1Char('"'))
                       || (state == State::Template
                           && character == QLatin1Char('`'))) {
                state = State::Normal;
            }
            continue;
        }

        if (character == QLatin1Char('/') && next == QLatin1Char('/')) {
            line += QStringLiteral("//");
            ++index;
            state = State::LineComment;
        } else if (character == QLatin1Char('/') && next == QLatin1Char('*')) {
            line += QStringLiteral("/*");
            ++index;
            state = State::BlockComment;
        } else if (character == QLatin1Char('\'')) {
            line += character;
            state = State::SingleQuote;
        } else if (character == QLatin1Char('"')) {
            line += character;
            state = State::DoubleQuote;
        } else if (character == QLatin1Char('`')) {
            line += character;
            state = State::Template;
        } else if (character == QLatin1Char('(')) {
            ++parentheses;
            line += character;
        } else if (character == QLatin1Char(')')) {
            parentheses = qMax(0, parentheses - 1);
            line += character;
        } else if (character == QLatin1Char('{')) {
            line = line.trimmed();
            line += QStringLiteral(" {");
            flush();
            ++indentation;
        } else if (character == QLatin1Char('}')) {
            flush();
            indentation = qMax(0, indentation - 1);
            line = QStringLiteral("}");
        } else if (character == QLatin1Char(';')) {
            line += character;
            if (css || parentheses == 0)
                flush();
        } else if (character.isSpace()) {
            if (!line.isEmpty() && !line.endsWith(QLatin1Char(' ')))
                line += QLatin1Char(' ');
        } else {
            line += character;
        }
    }
    flush();
    return lines.join(QLatin1Char('\n'));
}
}

QString SourceFormatter::pretty(const QString &source, Language language,
                                bool *formatted) {
    QString result = source;
    bool supported = true;
    switch (language) {
    case Language::Json: {
        bool valid = false;
        result = JsonHelper::pretty(source.toUtf8(), &valid);
        supported = valid;
        break;
    }
    case Language::Xml:
    case Language::Html:
        result = formatMarkup(source);
        supported = source.contains(QLatin1Char('<'));
        break;
    case Language::Css:
        result = formatBracedSource(source, true);
        supported = source.contains(QLatin1Char('{'));
        break;
    case Language::JavaScript:
        result = formatBracedSource(source, false);
        supported = source.contains(QLatin1Char('{'))
            || source.contains(QLatin1Char(';'));
        break;
    case Language::PlainText:
        supported = false;
        break;
    }
    if (formatted)
        *formatted = supported;
    return supported ? result : source;
}
