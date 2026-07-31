#pragma once

#include <QString>

namespace SourceFormatter {
enum class Language { PlainText, Json, Xml, Html, Css, JavaScript };

QString pretty(const QString &source, Language language,
               bool *formatted = nullptr);
}
