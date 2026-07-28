#pragma once

#include <QMap>
#include <QString>

class VariableSubstitution {
public:
    static QString apply(const QString &text, const QMap<QString, QString> &variables);
};
