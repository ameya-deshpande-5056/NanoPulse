#pragma once

#include "../network/request.h"

#include <QMainWindow>
#include <QMap>

class ApiClient;
class CollectionsSidebar;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class RequestPanel;
class ResponseViewer;
class SettingsManager;
class SqliteManager;
struct SwaggerDocument;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(SqliteManager *storage, SettingsManager *settings,
               QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void sendRequest();
    void saveRequest(const QString &folderId = {});
    void loadRequest(const QString &id);
    void loadEnvironments();
    QMap<QString, QString> activeVariables() const;
    ApiRequest currentRequest(bool substitute) const;
    void importSwagger();
    void importSwaggerFromUrl();
    void importSwaggerDocument(const SwaggerDocument &document,
                               bool alwaysPromptForBaseUrl);
    bool promptForBaseUrl(QString &baseUrl);
    void importCollections();
    void exportCollections();
    void saveResponse();
    void exportCurl();
    void applyTheme(bool dark);
    void updateMemoryUsage();
    void restoreSettings();
    void persistSettings();

    SqliteManager *m_storage;
    SettingsManager *m_settings;
    ApiClient *m_client;
    CollectionsSidebar *m_sidebar;
    QComboBox *m_method;
    QComboBox *m_url;
    QComboBox *m_environment;
    QSpinBox *m_timeout;
    QPushButton *m_send;
    QPushButton *m_cancel;
    RequestPanel *m_requestPanel;
    ResponseViewer *m_responseViewer;
    QLabel *m_memory;
    ApiRequest m_lastSent;
    QByteArray m_lightStyle;
    QByteArray m_darkStyle;
    int m_historyLimit = 50;
};
