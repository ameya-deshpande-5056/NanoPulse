#pragma once

#include "../network/request.h"

class HttpFileImporter {
public:
    static QList<ApiRequest> parse(const QString &source, QString *error = nullptr);
};
