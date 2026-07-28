#include "json_helper.h"

#include <nlohmann/json.hpp>

QString JsonHelper::pretty(const QByteArray &data, bool *valid) {
    const auto document = nlohmann::json::parse(
        data.constBegin(), data.constEnd(), nullptr, false);
    const bool ok = !document.is_discarded();
    if (valid)
        *valid = ok;
    return ok ? QString::fromStdString(document.dump(2)) : QString::fromUtf8(data);
}
