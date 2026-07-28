#include "variable_substitution.h"

#include <QRegularExpression>

QString VariableSubstitution::apply(const QString &text,
                                    const QMap<QString, QString> &variables) {
    QString result = text;
    static const QRegularExpression expression(
        QStringLiteral(R"(\{\{\s*([A-Za-z_][A-Za-z0-9_.-]*)\s*\}\})"));
    auto match = expression.globalMatch(result);
    QList<QRegularExpressionMatch> matches;
    while (match.hasNext())
        matches.prepend(match.next());
    for (const auto &item : matches) {
        const auto key = item.captured(1);
        if (variables.contains(key))
            result.replace(item.capturedStart(), item.capturedLength(), variables.value(key));
    }
    return result;
}
