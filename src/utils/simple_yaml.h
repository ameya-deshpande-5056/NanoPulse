#pragma once

#include <QByteArray>
#include <QJsonDocument>
#include <QString>

namespace SimpleYaml {
QJsonDocument parse(const QByteArray &data, QString *error = nullptr);
}
