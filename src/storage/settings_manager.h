#pragma once

#include <QJsonObject>
#include <QString>

class SettingsManager {
public:
    SettingsManager();
    QJsonObject load() const;
    bool save(const QJsonObject &settings) const;
    QString filePath() const;

private:
    QString m_filePath;
};
