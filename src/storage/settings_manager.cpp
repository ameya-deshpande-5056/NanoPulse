#include "settings_manager.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>

SettingsManager::SettingsManager() {
    const auto directory = QStandardPaths::writableLocation(
        QStandardPaths::AppConfigLocation);
    QDir().mkpath(directory);
    m_filePath = directory + QStringLiteral("/settings.json");
}

QJsonObject SettingsManager::load() const {
    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

bool SettingsManager::save(const QJsonObject &settings) const {
    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(QJsonDocument(settings).toJson(QJsonDocument::Indented)) >= 0;
}

QString SettingsManager::filePath() const {
    return m_filePath;
}
