#include "mainwindow.h"

#include "collections_sidebar.h"
#include "environment_manager.h"
#include "request_panel.h"
#include "response_viewer.h"
#include "../import/swagger_importer.h"
#include "../network/api_client.h"
#include "../storage/settings_manager.h"
#include "../storage/sqlite_manager.h"
#include "../utils/variable_substitution.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QClipboard>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressDialog>
#include <QPushButton>
#include <QSharedPointer>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#elif defined(Q_OS_LINUX)
#include <unistd.h>
#endif

namespace {
constexpr qsizetype MaximumSwaggerDocumentSize = 64 * 1024 * 1024;
}

MainWindow::MainWindow(SqliteManager *storage, SettingsManager *settings,
                       QWidget *parent)
    : QMainWindow(parent), m_storage(storage), m_settings(settings) {
    setWindowTitle(tr("NanoPulse REST Client"));
    resize(1180, 760);
    m_client = new ApiClient(this);
    m_sidebar = new CollectionsSidebar(storage, this);
    m_sidebar->setMinimumWidth(210);
    m_method = new QComboBox(this);
    m_method->addItems({QStringLiteral("GET"), QStringLiteral("POST"),
                        QStringLiteral("PUT"), QStringLiteral("PATCH"),
                        QStringLiteral("DELETE"), QStringLiteral("HEAD"),
                        QStringLiteral("OPTIONS")});
    m_url = new QComboBox(this);
    m_url->setEditable(true);
    m_url->setInsertPolicy(QComboBox::InsertAtTop);
    m_url->setMaxCount(20);
    m_url->lineEdit()->setPlaceholderText(tr("https://api.example.com/resource"));
    m_environment = new QComboBox(this);
    m_timeout = new QSpinBox(this);
    m_timeout->setRange(1, 600);
    m_timeout->setValue(30);
    m_timeout->setSuffix(tr(" s"));
    m_send = new QPushButton(tr("Send"), this);
    m_cancel = new QPushButton(tr("Cancel"), this);
    m_cancel->setEnabled(false);
    m_requestPanel = new RequestPanel(this);
    m_responseViewer = new ResponseViewer(this);

    auto *topBar = new QHBoxLayout;
    topBar->addWidget(m_method);
    topBar->addWidget(m_url, 1);
    topBar->addWidget(new QLabel(tr("Timeout:"), this));
    topBar->addWidget(m_timeout);
    topBar->addWidget(m_environment);
    topBar->addWidget(m_send);
    topBar->addWidget(m_cancel);
    auto *vertical = new QSplitter(Qt::Vertical, this);
    vertical->addWidget(m_requestPanel);
    vertical->addWidget(m_responseViewer);
    vertical->setStretchFactor(0, 1);
    vertical->setStretchFactor(1, 1);
    auto *right = new QWidget(this);
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->addLayout(topBar);
    rightLayout->addWidget(vertical);
    auto *mainSplitter = new QSplitter(Qt::Horizontal, this);
    mainSplitter->addWidget(m_sidebar);
    mainSplitter->addWidget(right);
    mainSplitter->setStretchFactor(1, 1);
    setCentralWidget(mainSplitter);

    auto *fileMenu = menuBar()->addMenu(tr("&File"));
    auto *importAction = fileMenu->addAction(tr("Import Swagger/OpenAPI…"));
    auto *importUrlAction =
        fileMenu->addAction(tr("Import Swagger/OpenAPI from URL..."));
    auto *importCollectionsAction = fileMenu->addAction(tr("Import Collections…"));
    auto *exportCollectionsAction = fileMenu->addAction(tr("Export Collections…"));
    auto *saveAction = fileMenu->addAction(tr("Save Request…"));
    saveAction->setShortcut(QKeySequence::Save);
    auto *curlAction = fileMenu->addAction(tr("Copy as cURL"));
    auto *saveResponseAction = fileMenu->addAction(tr("Save Response…"));
    fileMenu->addSeparator();
    auto *quitAction = fileMenu->addAction(tr("Quit"));
    auto *toolsMenu = menuBar()->addMenu(tr("&Tools"));
    auto *environmentsAction = toolsMenu->addAction(tr("Manage Environments…"));
    auto *historyLimitAction = toolsMenu->addAction(tr("History Limit…"));
    auto *themeAction = menuBar()->addAction(tr("Light theme"));
    themeAction->setCheckable(true);
    auto *helpMenu = menuBar()->addMenu(tr("&Help"));
    auto *aboutAction = helpMenu->addAction(tr("About"));

    m_memory = new QLabel(this);
    statusBar()->addPermanentWidget(m_memory);

    connect(m_send, &QPushButton::clicked, this, &MainWindow::sendRequest);
    connect(m_cancel, &QPushButton::clicked, m_client, &ApiClient::cancel);
    connect(m_client, &ApiClient::started, this, [this] {
        m_send->setEnabled(false);
        m_cancel->setEnabled(true);
        m_responseViewer->begin();
    });
    connect(m_client, &ApiClient::chunkReceived,
            m_responseViewer, &ResponseViewer::appendChunk);
    connect(m_client, &ApiClient::completed, this,
            [this](int status, const QList<QPair<QByteArray, QByteArray>> &headers,
                   qint64 elapsed, qint64 bytes, const QString &error) {
        m_send->setEnabled(true);
        m_cancel->setEnabled(false);
        m_responseViewer->finish(status, headers, elapsed, bytes, error);
        m_storage->addHistory(m_lastSent, status, elapsed, m_historyLimit);
        m_sidebar->refreshHistory();
    });
    connect(m_sidebar, &CollectionsSidebar::requestSelected,
            this, &MainWindow::loadRequest);
    connect(m_sidebar, &CollectionsSidebar::historySelected, this,
            [this](const QString &method, const QString &url) {
        m_method->setCurrentText(method);
        m_url->setCurrentText(url);
    });
    connect(m_sidebar, &CollectionsSidebar::saveCurrentRequested,
            this, &MainWindow::saveRequest);
    connect(importAction, &QAction::triggered, this, &MainWindow::importSwagger);
    connect(importUrlAction, &QAction::triggered,
            this, &MainWindow::importSwaggerFromUrl);
    connect(importCollectionsAction, &QAction::triggered,
            this, &MainWindow::importCollections);
    connect(exportCollectionsAction, &QAction::triggered,
            this, &MainWindow::exportCollections);
    connect(saveAction, &QAction::triggered, this, [this] { saveRequest(); });
    connect(curlAction, &QAction::triggered, this, &MainWindow::exportCurl);
    connect(saveResponseAction, &QAction::triggered,
            this, &MainWindow::saveResponse);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);
    connect(environmentsAction, &QAction::triggered, this, [this] {
        EnvironmentManager dialog(m_storage, this);
        connect(&dialog, &EnvironmentManager::environmentsChanged,
                this, &MainWindow::loadEnvironments);
        dialog.exec();
    });
    connect(historyLimitAction, &QAction::triggered, this, [this] {
        bool accepted = false;
        const int value = QInputDialog::getInt(
            this, tr("History Limit"), tr("Maximum saved requests:"),
            m_historyLimit, 1, 50, 1, &accepted);
        if (accepted)
            m_historyLimit = value;
    });
    connect(themeAction, &QAction::toggled, this, [this](bool light) {
        applyTheme(!light);
    });
    connect(aboutAction, &QAction::triggered, this, [this] {
        QMessageBox::about(this, tr("NanoPulse"),
                           tr("A lightweight, offline REST client."));
    });
    auto *sendShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
    connect(sendShortcut, &QShortcut::activated, this, &MainWindow::sendRequest);
    auto *urlShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_L), this);
    connect(urlShortcut, &QShortcut::activated, m_url->lineEdit(),
            qOverload<>(&QWidget::setFocus));

    QFile dark(QStringLiteral(":/style.qss"));
    if (dark.open(QIODevice::ReadOnly))
        m_darkStyle = dark.readAll();
    QFile light(QStringLiteral(":/style-light.qss"));
    if (light.open(QIODevice::ReadOnly))
        m_lightStyle = light.readAll();
    loadEnvironments();
    restoreSettings();
    for (const auto &item : m_storage->history(20))
        if (m_url->findText(item.url) < 0)
            m_url->addItem(item.url);
    QTimer::singleShot(0, this, [this] {
        updateMemoryUsage();
        auto *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, &MainWindow::updateMemoryUsage);
        timer->start(3000);
    });
}

MainWindow::~MainWindow() = default;

void MainWindow::sendRequest() {
    if (m_client->busy())
        return;
    m_lastSent = currentRequest(true);
    if (!QUrl(m_lastSent.resolvedUrl()).isValid()
        || m_lastSent.url.trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("Invalid URL"), tr("Enter a valid URL."));
        return;
    }
    if (m_url->findText(m_url->currentText()) < 0)
        m_url->insertItem(0, m_url->currentText());
    m_client->send(m_lastSent);
}

ApiRequest MainWindow::currentRequest(bool substitute) const {
    auto request = m_requestPanel->request();
    request.method = m_method->currentText();
    request.url = m_url->currentText();
    request.timeoutMs = m_timeout->value() * 1000;
    if (!substitute)
        return request;
    const auto variables = activeVariables();
    request.url = VariableSubstitution::apply(request.url, variables);
    for (auto &header : request.headers) {
        header.first = VariableSubstitution::apply(header.first, variables);
        header.second = VariableSubstitution::apply(header.second, variables);
    }
    for (auto &param : request.params) {
        param.first = VariableSubstitution::apply(param.first, variables);
        param.second = VariableSubstitution::apply(param.second, variables);
    }
    request.body = VariableSubstitution::apply(
                       QString::fromUtf8(request.body), variables).toUtf8();
    return request;
}

void MainWindow::saveRequest(const QString &folderId) {
    bool accepted = false;
    const auto name = QInputDialog::getText(
        this, tr("Save Request"), tr("Name:"), QLineEdit::Normal, {}, &accepted);
    if (accepted && !name.trimmed().isEmpty()) {
        m_storage->saveRequest(name.trimmed(), currentRequest(false), folderId);
        m_sidebar->refresh();
    }
}

void MainWindow::loadRequest(const QString &id) {
    const auto request = m_storage->request(id);
    m_method->setCurrentText(request.method);
    m_url->setCurrentText(request.url);
    m_requestPanel->setRequest(request);
}

void MainWindow::loadEnvironments() {
    const auto selected = m_environment->currentText();
    m_environment->clear();
    m_environment->addItem(tr("No environment"));
    for (const auto &environment : m_storage->environments())
        m_environment->addItem(environment.first);
    const auto index = m_environment->findText(selected);
    if (index >= 0)
        m_environment->setCurrentIndex(index);
}

QMap<QString, QString> MainWindow::activeVariables() const {
    for (const auto &environment : m_storage->environments())
        if (environment.first == m_environment->currentText())
            return environment.second;
    return {};
}

void MainWindow::importSwagger() {
    const auto path = QFileDialog::getOpenFileName(
        this, tr("Import Swagger/OpenAPI"), {}, tr("JSON files (*.json)"));
    if (path.isEmpty())
        return;
    importSwaggerDocument(SwaggerImporter::parseFile(path), true);
}

void MainWindow::importSwaggerFromUrl() {
    bool accepted = false;
    const auto input = QInputDialog::getText(
        this, tr("Import Swagger/OpenAPI from URL"), tr("Specification URL:"),
        QLineEdit::Normal, {}, &accepted).trimmed();
    if (!accepted || input.isEmpty())
        return;

    const auto url = QUrl::fromUserInput(input);
    if (!url.isValid() || url.host().isEmpty()
        || (url.scheme() != QStringLiteral("http")
            && url.scheme() != QStringLiteral("https"))) {
        QMessageBox::warning(this, tr("Invalid URL"),
                             tr("Enter a valid HTTP or HTTPS URL."));
        return;
    }

    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(30000);
    auto *manager = new QNetworkAccessManager(this);
    auto *reply = manager->get(request);
    auto data = QSharedPointer<QByteArray>::create();
    auto *progress = new QProgressDialog(
        tr("Downloading API specification..."), tr("Cancel"), 0, 0, this);
    progress->setWindowTitle(tr("Import Swagger/OpenAPI"));
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    progress->show();

    connect(progress, &QProgressDialog::canceled, reply, [reply] {
        reply->setProperty("userCanceled", true);
        reply->abort();
    });
    connect(reply, &QNetworkReply::downloadProgress, progress,
            [progress](qint64 received, qint64 total) {
        if (total > 0) {
            progress->setRange(0, 100);
            progress->setValue(static_cast<int>(received * 100 / total));
        }
    });
    connect(reply, &QNetworkReply::readyRead, this, [reply, data] {
        data->append(reply->readAll());
        if (data->size() > MaximumSwaggerDocumentSize) {
            reply->setProperty("documentTooLarge", true);
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, manager, reply, progress, data, url] {
        data->append(reply->readAll());
        const bool canceled = reply->property("userCanceled").toBool();
        const bool tooLarge = reply->property("documentTooLarge").toBool()
            || data->size() > MaximumSwaggerDocumentSize;
        const auto error = reply->error();
        const auto errorText = reply->errorString();
        reply->deleteLater();
        manager->deleteLater();
        progress->close();
        progress->deleteLater();

        if (canceled)
            return;
        if (tooLarge) {
            QMessageBox::critical(
                this, tr("Import failed"),
                tr("The API specification exceeds the 64 MB import limit."));
            return;
        }
        if (error != QNetworkReply::NoError) {
            QMessageBox::critical(this, tr("Import failed"), errorText);
            return;
        }

        auto sourceName = QFileInfo(url.path()).baseName();
        if (sourceName.isEmpty())
            sourceName = url.host();
        auto document = SwaggerImporter::parse(*data, sourceName);
        if (!document.baseUrl.isEmpty()) {
            const QUrl detected(document.baseUrl);
            if (detected.isRelative())
                document.baseUrl = url.resolved(detected).toString();
        }
        importSwaggerDocument(document, true);
    });
}

void MainWindow::importSwaggerDocument(
    const SwaggerDocument &document, bool alwaysPromptForBaseUrl) {
    if (!document.error.isEmpty()) {
        QMessageBox::critical(this, tr("Import failed"), document.error);
        return;
    }

    auto baseUrl = document.baseUrl;
    const QUrl detected(baseUrl);
    const bool usableBaseUrl = detected.isValid() && !detected.host().isEmpty()
        && (detected.scheme() == QStringLiteral("http")
            || detected.scheme() == QStringLiteral("https"));
    if ((alwaysPromptForBaseUrl || !usableBaseUrl)
        && !promptForBaseUrl(baseUrl))
        return;
    while (baseUrl.endsWith(QLatin1Char('/')))
        baseUrl.chop(1);

    const auto result =
        SwaggerImporter::importDocument(document, baseUrl, *m_storage);
    if (!result.error.isEmpty())
        QMessageBox::critical(this, tr("Import failed"), result.error);
    else {
        m_sidebar->refresh();
        statusBar()->showMessage(tr("Imported %1 requests").arg(result.requestCount),
                                 5000);
    }
}

bool MainWindow::promptForBaseUrl(QString &baseUrl) {
    while (true) {
        bool accepted = false;
        const auto value = QInputDialog::getText(
            this, tr("API Base URL"),
            tr("Add or modify the base URL used by imported requests:"),
            QLineEdit::Normal, baseUrl, &accepted).trimmed();
        if (!accepted)
            return false;

        const QUrl url(value);
        if (url.isValid() && !url.host().isEmpty()
            && (url.scheme() == QStringLiteral("http")
                || url.scheme() == QStringLiteral("https"))) {
            baseUrl = value;
            return true;
        }
        QMessageBox::warning(this, tr("Invalid base URL"),
                             tr("Enter a valid HTTP or HTTPS base URL."));
    }
}

void MainWindow::importCollections() {
    const auto path = QFileDialog::getOpenFileName(
        this, tr("Import Collections"), {}, tr("JSON files (*.json)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, tr("Import failed"), file.errorString());
        return;
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray()) {
        QMessageBox::critical(this, tr("Import failed"), error.errorString());
        return;
    }
    const int count = m_storage->importCollections(document.array());
    m_sidebar->refresh();
    statusBar()->showMessage(tr("Imported %1 collection items").arg(count), 5000);
}

void MainWindow::exportCollections() {
    const auto path = QFileDialog::getSaveFileName(
        this, tr("Export Collections"), QStringLiteral("collections.json"),
        tr("JSON files (*.json)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, tr("Export failed"), file.errorString());
        return;
    }
    file.write(QJsonDocument(m_storage->exportCollections())
                   .toJson(QJsonDocument::Indented));
}

void MainWindow::saveResponse() {
    const auto path = QFileDialog::getSaveFileName(
        this, tr("Save Response"), QStringLiteral("response.txt"),
        tr("All files (*)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
        file.write(m_responseViewer->responseData());
}

void MainWindow::exportCurl() {
    const auto request = currentRequest(true);
    QString command = QStringLiteral("curl -X %1").arg(request.method);
    for (const auto &header : request.headers) {
        auto value = header.second;
        value.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
        command += QStringLiteral(" -H '%1: %2'").arg(header.first, value);
    }
    if (!request.body.isEmpty()) {
        auto body = QString::fromUtf8(request.body);
        body.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
        command += QStringLiteral(" --data '%1'").arg(body);
    }
    command += QStringLiteral(" '%1'").arg(request.resolvedUrl());
    QApplication::clipboard()->setText(command);
    statusBar()->showMessage(tr("cURL copied"), 3000);
}

void MainWindow::applyTheme(bool dark) {
    qApp->setStyleSheet(QString::fromUtf8(dark ? m_darkStyle : m_lightStyle));
}

void MainWindow::updateMemoryUsage() {
    qint64 bytes = 0;
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        bytes = static_cast<qint64>(counters.WorkingSetSize);
#elif defined(Q_OS_LINUX)
    QFile statm(QStringLiteral("/proc/self/statm"));
    if (statm.open(QIODevice::ReadOnly)) {
        const auto fields = statm.readAll().split(' ');
        if (fields.size() > 1)
            bytes = fields.at(1).toLongLong() * sysconf(_SC_PAGESIZE);
    }
#endif
    m_memory->setText(tr("Memory: %1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1));
}

void MainWindow::restoreSettings() {
    const auto settings = m_settings->load();
    const bool dark = settings.value(QStringLiteral("theme"))
                          .toString(QStringLiteral("dark")) == QStringLiteral("dark");
    applyTheme(dark);
    if (settings.value(QStringLiteral("window_geometry")).isString())
        restoreGeometry(QByteArray::fromBase64(
            settings.value(QStringLiteral("window_geometry")).toString().toLatin1()));
    m_url->setCurrentText(settings.value(QStringLiteral("last_url")).toString());
    m_timeout->setValue(settings.value(QStringLiteral("timeout_seconds")).toInt(30));
    m_historyLimit = settings.value(QStringLiteral("history_limit")).toInt(50);
    m_historyLimit = qBound(1, m_historyLimit, 50);
    const auto active = settings.value(QStringLiteral("active_environment")).toString();
    const auto index = m_environment->findText(active);
    if (index >= 0)
        m_environment->setCurrentIndex(index);
}

void MainWindow::persistSettings() {
    QJsonObject settings;
    settings.insert(QStringLiteral("theme"),
                    qApp->styleSheet() == QString::fromUtf8(m_darkStyle)
                        ? QStringLiteral("dark") : QStringLiteral("light"));
    settings.insert(QStringLiteral("active_environment"),
                    m_environment->currentText());
    settings.insert(QStringLiteral("window_geometry"),
                    QString::fromLatin1(saveGeometry().toBase64()));
    settings.insert(QStringLiteral("last_url"), m_url->currentText());
    settings.insert(QStringLiteral("timeout_seconds"), m_timeout->value());
    settings.insert(QStringLiteral("history_limit"), m_historyLimit);
    m_settings->save(settings);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    persistSettings();
    QMainWindow::closeEvent(event);
}
