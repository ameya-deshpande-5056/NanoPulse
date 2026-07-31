#pragma once

#include "../network/request.h"

class CurlImporter {
public:
    static ApiRequest parse(const QString &command, QString *error = nullptr);
};
