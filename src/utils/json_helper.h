#pragma once

#include <QByteArray>
#include <QString>

class JsonHelper {
public:
    static QString pretty(const QByteArray &data, bool *valid = nullptr);
};
