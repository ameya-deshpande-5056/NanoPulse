#include "mainwindow.h"

#include "collections_sidebar.h"
#include "environment_manager.h"
#include "request_method_colors.h"
#include "request_panel.h"
#include "response_viewer.h"
#include "../import/swagger_importer.h"
#include "../import/postman_importer.h"
#include "../import/postman_exporter.h"
#include "../import/curl_importer.h"
#include "../import/http_file_importer.h"
#include "../import/har_importer.h"
#include "../import/external_collection_importer.h"
#include "../import/bruno_importer.h"
#include "../network/api_client.h"
#include "../storage/settings_manager.h"
#include "../storage/sqlite_manager.h"
#include "../utils/variable_substitution.h"

#include <QApplication>
#include <QBrush>
#include <QCheckBox>
#include <QCloseEvent>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QNetworkCookie>
#include <QProgressDialog>
#include <QPushButton>
#include <QSharedPointer>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
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
    m_requestTabs = new QTabWidget(this);
    m_requestTabs->setTabsClosable(true);
    m_requestTabs->setMovable(true);
    m_requestTabs->setDocumentMode(true);
    m_requestTabs->tabBar()->setElideMode(Qt::ElideNone);
    m_requestTabs->tabBar()->setUsesScrollButtons(true);
    auto *newTab = new QToolButton(m_requestTabs);
    newTab->setText(QStringLiteral("+"));
    newTab->setToolTip(tr("New request tab"));
    m_requestTabs->setCornerWidget(newTab, Qt::TopRightCorner);
    addRequestTab();
    auto *mainSplitter = new QSplitter(Qt::Horizontal, this);
    mainSplitter->addWidget(m_sidebar);
    mainSplitter->addWidget(m_requestTabs);
    mainSplitter->setStretchFactor(1, 1);
    setCentralWidget(mainSplitter);

    auto *fileMenu = menuBar()->addMenu(tr("&File"));
    auto *importAction = fileMenu->addAction(tr("Import Swagger/OpenAPI…"));
    auto *importUrlAction =
        fileMenu->addAction(tr("Import Swagger/OpenAPI from URL..."));
    auto *importCollectionsAction = fileMenu->addAction(tr("Import Collections…"));
    auto *importCurlAction = fileMenu->addAction(tr("Import cURL…"));
    auto *importHttpAction = fileMenu->addAction(tr("Import HTTP Request File…"));
    auto *importBrunoAction = fileMenu->addAction(tr("Import Bruno Collection Directory…"));
    auto *exportCollectionsAction = fileMenu->addAction(tr("Export Collections…"));
    auto *exportPostmanAction = fileMenu->addAction(tr("Export Postman Collection v2.1…"));
    auto *saveAction = fileMenu->addAction(tr("Save Request…"));
    saveAction->setShortcut(QKeySequence::Save);
    auto *curlAction = fileMenu->addAction(tr("Copy as cURL"));
    auto *saveResponseAction = fileMenu->addAction(tr("Save Response…"));
    fileMenu->addSeparator();
    auto *quitAction = fileMenu->addAction(tr("Quit"));
    auto *toolsMenu = menuBar()->addMenu(tr("&Tools"));
    auto *environmentsAction = toolsMenu->addAction(tr("Manage Environments…"));
    auto *cookiesAction = toolsMenu->addAction(tr("Manage Cookies for Current Host…"));
    auto *historyLimitAction = toolsMenu->addAction(tr("History Limit…"));
    auto *themeAction = menuBar()->addAction(tr("Light theme"));
    themeAction->setCheckable(true);
    auto *helpMenu = menuBar()->addMenu(tr("&Help"));
    auto *aboutAction = helpMenu->addAction(tr("About"));

    m_memory = new QLabel(this);
    statusBar()->addPermanentWidget(m_memory);

    connect(newTab, &QToolButton::clicked, this,
            [this] { addRequestTab(); });
    connect(cookiesAction, &QAction::triggered, this, &MainWindow::manageCookies);
    connect(m_requestTabs, &QTabWidget::tabCloseRequested,
            this, &MainWindow::closeRequestTab);
    connect(m_client, &ApiClient::started, this, [this] {
        if (m_activeSend)
            m_activeSend->setEnabled(false);
        if (m_activeCancel)
            m_activeCancel->setEnabled(true);
        if (m_activeResponseViewer)
            m_activeResponseViewer->begin();
    });
    connect(m_client, &ApiClient::chunkReceived, this,
            [this](const QByteArray &chunk) {
                if (m_activeResponseViewer)
                    m_activeResponseViewer->appendChunk(chunk);
            });
    connect(m_client, &ApiClient::completed, this,
            [this](int status, const QList<QPair<QByteArray, QByteArray>> &headers,
                   qint64 elapsed, qint64 bytes, const QString &error) {
        if (m_activeSend)
            m_activeSend->setEnabled(true);
        if (m_activeCancel)
            m_activeCancel->setEnabled(false);
        if (m_activeResponseViewer)
            m_activeResponseViewer->finish(status, headers, elapsed, bytes, error);
        m_activeSend = nullptr;
        m_activeCancel = nullptr;
        m_activeResponseViewer = nullptr;
        m_storage->addHistory(m_lastSent, status, elapsed, m_historyLimit);
        m_sidebar->refreshHistory();
    });
    connect(m_sidebar, &CollectionsSidebar::requestSelected,
            this, &MainWindow::loadRequest);
    connect(m_sidebar, &CollectionsSidebar::historySelected, this,
            [this](const QString &method, const QString &url) {
        if (auto *tab = currentRequestTab()) {
            tab->method->setCurrentText(method);
            tab->url->setCurrentText(url);
        }
    });
    connect(m_sidebar, &CollectionsSidebar::saveCurrentRequested,
            this, &MainWindow::saveRequest);
    connect(importAction, &QAction::triggered, this, &MainWindow::importSwagger);
    connect(importUrlAction, &QAction::triggered,
            this, &MainWindow::importSwaggerFromUrl);
    connect(importCollectionsAction, &QAction::triggered,
            this, &MainWindow::importCollections);
    connect(importCurlAction, &QAction::triggered, this, &MainWindow::importCurl);
    connect(importHttpAction, &QAction::triggered, this, &MainWindow::importHttpFile);
    connect(importBrunoAction, &QAction::triggered,
            this, &MainWindow::importBrunoCollection);
    connect(exportCollectionsAction, &QAction::triggered,
            this, &MainWindow::exportCollections);
    connect(exportPostmanAction, &QAction::triggered,
            this, &MainWindow::exportPostmanCollection);
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
        QMessageBox about(this);
        about.setWindowTitle(tr("About NanoPulse"));
        about.setWindowIcon(QIcon(QStringLiteral(":/app-icon.svg")));
        about.setIconPixmap(QIcon(QStringLiteral(":/app-icon.svg")).pixmap(64, 64));
        about.setText(tr("NanoPulse"));
        about.setInformativeText(
            tr("A lightweight, offline REST client.<br><br>Version %1")
                .arg(QApplication::applicationVersion()));
        about.setStandardButtons(QMessageBox::Ok);
        about.exec();
    });
    auto *sendShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
    connect(sendShortcut, &QShortcut::activated, this, &MainWindow::sendRequest);
    auto *urlShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_L), this);
    connect(urlShortcut, &QShortcut::activated, this, [this] {
        if (auto *tab = currentRequestTab())
            tab->url->lineEdit()->setFocus();
    });

    QFile dark(QStringLiteral(":/style.qss"));
    if (dark.open(QIODevice::ReadOnly))
        m_darkStyle = dark.readAll();
    QFile light(QStringLiteral(":/style-light.qss"));
    if (light.open(QIODevice::ReadOnly))
        m_lightStyle = light.readAll();
    loadEnvironments();
    restoreSettings();
    for (const auto &item : m_storage->history(20))
        if (auto *tab = currentRequestTab(); tab->url->findText(item.url) < 0)
            tab->url->addItem(item.url);
    QTimer::singleShot(0, this, [this] {
        updateMemoryUsage();
        auto *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, &MainWindow::updateMemoryUsage);
        timer->start(3000);
    });
}

MainWindow::~MainWindow() = default;

MainWindow::RequestTab *MainWindow::addRequestTab(const ApiRequest &request) {
    RequestTab tab{};
    tab.page = new QWidget(m_requestTabs);
    tab.method = new QComboBox(tab.page);
    tab.method->addItems({QStringLiteral("GET"), QStringLiteral("POST"),
                          QStringLiteral("PUT"), QStringLiteral("PATCH"),
                          QStringLiteral("DELETE"), QStringLiteral("HEAD"),
                          QStringLiteral("OPTIONS")});
    tab.method->setEditable(true);
    tab.method->lineEdit()->setPlaceholderText(tr("Custom method"));
    RequestMethodColors::installDelegate(tab.method);
    RequestMethodColors::apply(tab.method, true);
    tab.method->setProperty("requestMethod", tab.method->currentText());
    tab.url = new QComboBox(tab.page);
    tab.url->setEditable(true);
    tab.url->setInsertPolicy(QComboBox::InsertAtTop);
    tab.url->setMaxCount(20);
    tab.url->lineEdit()->setPlaceholderText(
        tr("https://api.example.com/resource"));
    tab.environment = new QComboBox(tab.page);
    tab.timeout = new QSpinBox(tab.page);
    tab.timeout->setRange(1, 600);
    tab.timeout->setValue(30);
    tab.timeout->setSuffix(tr(" s"));
    tab.send = new QPushButton(tr("Send"), tab.page);
    tab.cancel = new QPushButton(tr("Cancel"), tab.page);
    tab.cancel->setEnabled(false);
    auto *requestSettings = new QToolButton(tab.page);
    requestSettings->setText(tr("Settings"));
    requestSettings->setToolTip(tr("Request transport settings"));
    tab.requestPanel = new RequestPanel(tab.page);
    tab.responseViewer = new ResponseViewer(tab.page);

    auto *topBar = new QHBoxLayout;
    topBar->addWidget(tab.method);
    topBar->addWidget(tab.url, 1);
    topBar->addWidget(new QLabel(tr("Timeout:"), tab.page));
    topBar->addWidget(tab.timeout);
    topBar->addWidget(tab.environment);
    topBar->addWidget(requestSettings);
    topBar->addWidget(tab.send);
    topBar->addWidget(tab.cancel);
    auto *vertical = new QSplitter(Qt::Vertical, tab.page);
    vertical->addWidget(tab.requestPanel);
    vertical->addWidget(tab.responseViewer);
    vertical->setStretchFactor(0, 1);
    vertical->setStretchFactor(1, 1);
    auto *layout = new QVBoxLayout(tab.page);
    layout->addLayout(topBar);
    layout->addWidget(vertical);

    m_requestTabData.append(tab);
    const int index = m_requestTabs->addTab(tab.page, QString());
    m_requestTabs->setCurrentIndex(index);
    connect(tab.send, &QPushButton::clicked, this, [this, page = tab.page] {
        m_requestTabs->setCurrentWidget(page);
        sendRequest();
    });
    connect(tab.cancel, &QPushButton::clicked, m_client, &ApiClient::cancel);
    connect(requestSettings, &QToolButton::clicked, this,
            [this, page = tab.page] {
                for (auto &candidate : m_requestTabData) {
                    if (candidate.page == page) {
                        configureRequestSettings(&candidate);
                        break;
                    }
                }
            });
    connect(tab.method, &QComboBox::currentTextChanged, this,
            [this, page = tab.page, method = tab.method](const QString &value) {
        method->setProperty("requestMethod", value);
        method->style()->unpolish(method);
        method->style()->polish(method);
        updateRequestTabTitle(page);
    });
    connect(tab.url->lineEdit(), &QLineEdit::textChanged, this,
            [this, page = tab.page] { updateRequestTabTitle(page); });

    const auto method = request.method.isEmpty() ? QStringLiteral("GET") : request.method;
    if (tab.method->findText(method) < 0)
        tab.method->addItem(method);
    tab.method->setCurrentText(method);
    tab.url->setCurrentText(request.url);
    tab.followRedirects = request.followRedirects;
    tab.verifyTls = request.verifyTls;
    tab.proxyUrl = request.proxyUrl;
    tab.clientCertificatePath = request.clientCertificatePath;
    tab.clientPrivateKeyPath = request.clientPrivateKeyPath;
    tab.requestPanel->setRequest(request);
    loadEnvironments();
    updateRequestTabTitle(tab.page);
    return &m_requestTabData.last();
}

MainWindow::RequestTab *MainWindow::currentRequestTab() {
    const int index = m_requestTabs->currentIndex();
    return index >= 0 && index < m_requestTabData.size()
        ? &m_requestTabData[index] : nullptr;
}

const MainWindow::RequestTab *MainWindow::currentRequestTab() const {
    const int index = m_requestTabs->currentIndex();
    return index >= 0 && index < m_requestTabData.size()
        ? &m_requestTabData.at(index) : nullptr;
}

void MainWindow::closeRequestTab(int index) {
    if (index < 0 || index >= m_requestTabData.size())
        return;
    const auto &tab = m_requestTabData.at(index);
    if (tab.responseViewer == m_activeResponseViewer) {
        statusBar()->showMessage(tr("Wait for the request to finish before closing its tab."),
                                 3000);
        return;
    }
    if (m_requestTabData.size() == 1) {
        tab.method->setCurrentText(QStringLiteral("GET"));
        tab.url->clear();
        tab.requestPanel->setRequest({});
        tab.responseViewer->begin();
        updateRequestTabTitle(tab.page);
        return;
    }
    auto *page = tab.page;
    m_requestTabs->removeTab(index);
    m_requestTabData.removeAt(index);
    page->deleteLater();
}

void MainWindow::updateRequestTabTitle(QWidget *page) {
    const int index = m_requestTabs->indexOf(page);
    if (index < 0)
        return;
    const auto &tab = m_requestTabData.at(index);
    const auto url = tab.url->currentText().trimmed();
    const auto text = url.isEmpty()
        ? tr("New Request")
        : QStringLiteral("%1 %2").arg(tab.method->currentText(), url);
    const auto methodIndex = tab.method->currentIndex();
    const auto color = tab.method->itemData(methodIndex, Qt::ForegroundRole)
                           .value<QBrush>().color();
    m_requestTabs->setTabText(index, text);
    m_requestTabs->tabBar()->setTabTextColor(index, color);
    m_requestTabs->setTabToolTip(index, text);
}

void MainWindow::sendRequest() {
    if (m_client->busy())
        return;
    auto *tab = currentRequestTab();
    if (!tab)
        return;
    m_lastSent = currentRequest(true);
    if (!QUrl(m_lastSent.resolvedUrl()).isValid()
        || m_lastSent.url.trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("Invalid URL"), tr("Enter a valid URL."));
        return;
    }
    if (tab->url->findText(tab->url->currentText()) < 0)
        tab->url->insertItem(0, tab->url->currentText());
    m_activeResponseViewer = tab->responseViewer;
    m_activeSend = tab->send;
    m_activeCancel = tab->cancel;
    m_client->send(m_lastSent);
}

void MainWindow::configureRequestSettings(RequestTab *tab) {
    if (!tab)
        return;
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Request settings"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    auto *redirects = new QCheckBox(tr("Follow redirects"), &dialog);
    redirects->setChecked(tab->followRedirects);
    auto *verifyTls = new QCheckBox(tr("Verify TLS certificates"), &dialog);
    verifyTls->setChecked(tab->verifyTls);
    auto *proxy = new QLineEdit(tab->proxyUrl, &dialog);
    proxy->setPlaceholderText(tr("System proxy (or http://user:password@host:port)"));
    auto *certificate = new QLineEdit(tab->clientCertificatePath, &dialog);
    auto *privateKey = new QLineEdit(tab->clientPrivateKeyPath, &dialog);
    form->addRow(redirects);
    form->addRow(verifyTls);
    form->addRow(tr("Proxy"), proxy);
    form->addRow(tr("Client certificate (PEM)"), certificate);
    form->addRow(tr("Private key (RSA PEM)"), privateKey);
    layout->addLayout(form);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                          &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    tab->followRedirects = redirects->isChecked();
    tab->verifyTls = verifyTls->isChecked();
    tab->proxyUrl = proxy->text().trimmed();
    tab->clientCertificatePath = certificate->text().trimmed();
    tab->clientPrivateKeyPath = privateKey->text().trimmed();
}

void MainWindow::manageCookies() {
    const auto *tab = currentRequestTab();
    if (!tab)
        return;
    const QUrl url(tab->url->currentText());
    if (!url.isValid() || url.host().isEmpty()) {
        QMessageBox::warning(this, tr("Cookies"), tr("Enter a valid request URL first."));
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Cookies for %1").arg(url.host()));
    auto *layout = new QVBoxLayout(&dialog);
    auto *table = new QTableWidget(0, 2, &dialog);
    table->setHorizontalHeaderLabels({tr("Name"), tr("Value")});
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    const auto cookies = m_client->cookiesForUrl(url);
    table->setRowCount(cookies.size() + 1);
    for (int row = 0; row < cookies.size(); ++row) {
        table->setItem(row, 0, new QTableWidgetItem(QString::fromUtf8(cookies.at(row).name())));
        table->setItem(row, 1, new QTableWidgetItem(QString::fromUtf8(cookies.at(row).value())));
    }
    layout->addWidget(table);
    auto *note = new QLabel(tr("Changes are kept locally in this session and sent only to this host."),
                             &dialog);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                          &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    QList<QNetworkCookie> changed;
    for (int row = 0; row < table->rowCount(); ++row) {
        const auto *name = table->item(row, 0);
        const auto *value = table->item(row, 1);
        if (name && !name->text().trimmed().isEmpty())
            changed.append(QNetworkCookie(name->text().trimmed().toUtf8(),
                                          value ? value->text().toUtf8() : QByteArray()));
    }
    m_client->setCookiesForUrl(url, changed);
}

ApiRequest MainWindow::currentRequest(bool substitute) const {
    const auto *tab = currentRequestTab();
    if (!tab)
        return {};
    auto request = tab->requestPanel->request();
    request.method = tab->method->currentText();
    request.url = tab->url->currentText();
    request.timeoutMs = tab->timeout->value() * 1000;
    request.followRedirects = tab->followRedirects;
    request.verifyTls = tab->verifyTls;
    request.proxyUrl = tab->proxyUrl;
    request.clientCertificatePath = tab->clientCertificatePath;
    request.clientPrivateKeyPath = tab->clientPrivateKeyPath;
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
    request.binaryFilePath = VariableSubstitution::apply(request.binaryFilePath, variables);
    for (auto &entry : request.bodyEntries) {
        entry.key = VariableSubstitution::apply(entry.key, variables);
        entry.value = VariableSubstitution::apply(entry.value, variables);
        entry.filePath = VariableSubstitution::apply(entry.filePath, variables);
    }
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
    addRequestTab(request);
}

void MainWindow::loadEnvironments() {
    const auto environments = m_storage->environments();
    for (auto &tab : m_requestTabData) {
        const auto selected = tab.environment->currentText();
        tab.environment->clear();
        tab.environment->addItem(tr("No environment"));
        for (const auto &environment : environments)
            tab.environment->addItem(environment.first);
        const auto index = tab.environment->findText(selected);
        if (index >= 0)
            tab.environment->setCurrentIndex(index);
    }
}

QMap<QString, QString> MainWindow::activeVariables() const {
    const auto *tab = currentRequestTab();
    if (!tab)
        return {};
    for (const auto &environment : m_storage->environments())
        if (environment.first == tab->environment->currentText())
            return environment.second;
    return {};
}

void MainWindow::importSwagger() {
    const auto path = QFileDialog::getOpenFileName(
        this, tr("Import Swagger/OpenAPI"), {},
        tr("OpenAPI and Swagger (*.json *.yaml *.yml);;All files (*)"));
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
        this, tr("Import Collections"), {},
        tr("REST collections (*.json *.har *.hoppscotch *.insomnia);;All files (*)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, tr("Import failed"), file.errorString());
        return;
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || (!document.isArray() && !document.isObject())) {
        QMessageBox::critical(this, tr("Import failed"), error.errorString());
        return;
    }
    int count = 0;
    if (document.isArray()) {
        count = m_storage->importCollections(document.array());
    } else if (PostmanImporter::isCollection(document.object())) {
        const auto result = PostmanImporter::importCollection(document.object(), *m_storage);
        if (!result.error.isEmpty()) {
            QMessageBox::critical(this, tr("Import failed"), result.error);
            return;
        }
        count = result.requestCount;
    } else if (HarImporter::isHar(document.object())) {
        const auto result = HarImporter::importArchive(
            document.object(), QFileInfo(path).completeBaseName(), *m_storage);
        if (!result.error.isEmpty()) {
            QMessageBox::critical(this, tr("Import failed"), result.error);
            return;
        }
        count = result.requestCount;
    } else if (ExternalCollectionImporter::recognizes(document.object())) {
        const auto result = ExternalCollectionImporter::importCollection(
            document.object(), QFileInfo(path).completeBaseName(), *m_storage);
        if (!result.error.isEmpty()) {
            QMessageBox::critical(this, tr("Import failed"), result.error);
            return;
        }
        count = result.requestCount;
    } else {
        QMessageBox::critical(this, tr("Import failed"),
                              tr("Choose a NanoPulse, Postman, Insomnia, Hoppscotch, Thunder Client, or HAR collection."));
        return;
    }
    m_sidebar->refresh();
    statusBar()->showMessage(tr("Imported %1 collection items").arg(count), 5000);
}

void MainWindow::importCurl() {
    bool accepted = false;
    const auto command = QInputDialog::getMultiLineText(this, tr("Import cURL"),
                                                          tr("Paste cURL command:"), {}, &accepted);
    if (!accepted || command.trimmed().isEmpty())
        return;
    QString error;
    const auto request = CurlImporter::parse(command, &error);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, tr("Import cURL"), error);
        return;
    }
    addRequestTab(request);
}

void MainWindow::importHttpFile() {
    const auto path = QFileDialog::getOpenFileName(
        this, tr("Import HTTP Request File"), {},
        tr("HTTP request files (*.http *.rest);;All files (*)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, tr("Import failed"), file.errorString());
        return;
    }
    QString error;
    const auto requests = HttpFileImporter::parse(QString::fromUtf8(file.readAll()), &error);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, tr("Import failed"), error);
        return;
    }
    for (const auto &request : requests)
        addRequestTab(request);
    statusBar()->showMessage(tr("Imported %1 requests").arg(requests.size()), 5000);
}

void MainWindow::importBrunoCollection() {
    const auto path = QFileDialog::getExistingDirectory(
        this, tr("Import Bruno Collection Directory"));
    if (path.isEmpty())
        return;
    const auto result = BrunoImporter::importDirectory(path, *m_storage);
    if (!result.error.isEmpty()) {
        QMessageBox::critical(this, tr("Import failed"), result.error);
        return;
    }
    m_sidebar->refresh();
    statusBar()->showMessage(tr("Imported %1 Bruno requests").arg(result.requestCount), 5000);
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

void MainWindow::exportPostmanCollection() {
    bool accepted = false;
    const auto name = QInputDialog::getText(this, tr("Export Postman Collection"),
                                            tr("Collection name:"), QLineEdit::Normal,
                                            tr("NanoPulse Collection"), &accepted);
    if (!accepted || name.trimmed().isEmpty())
        return;
    const auto path = QFileDialog::getSaveFileName(
        this, tr("Export Postman Collection"), QStringLiteral("nanopulse.postman_collection.json"),
        tr("Postman Collection (*.postman_collection.json);;JSON files (*.json)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, tr("Export failed"), file.errorString());
        return;
    }
    file.write(QJsonDocument(PostmanExporter::collection(m_storage->exportCollections(),
                                                           name.trimmed()))
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
        if (const auto *tab = currentRequestTab())
            file.write(tab->responseViewer->responseData());
}

void MainWindow::exportCurl() {
    const auto request = currentRequest(true);
    const auto shellQuote = [](QString value) {
        value.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
        return QStringLiteral("'%1'").arg(value);
    };
    QString command = QStringLiteral("curl -X %1").arg(request.method);
    for (const auto &header : request.headers) {
        command += QStringLiteral(" -H %1")
                       .arg(shellQuote(header.first + QStringLiteral(": ") + header.second));
    }
    if (request.bodyMode == RequestBodyMode::UrlEncoded) {
        for (const auto &entry : request.bodyEntries)
            if (entry.enabled && !entry.key.isEmpty())
                command += QStringLiteral(" --data-urlencode %1")
                               .arg(shellQuote(entry.key + QLatin1Char('=') + entry.value));
    } else if (request.bodyMode == RequestBodyMode::Multipart) {
        for (const auto &entry : request.bodyEntries) {
            if (!entry.enabled || entry.key.isEmpty())
                continue;
            command += QStringLiteral(" -F %1").arg(shellQuote(
                entry.key + QLatin1Char('=') + (entry.isFile
                    ? QStringLiteral("@") + entry.filePath : entry.value)));
        }
    } else if (request.bodyMode == RequestBodyMode::Binary
               && !request.binaryFilePath.isEmpty()) {
        command += QStringLiteral(" --data-binary @%1").arg(shellQuote(request.binaryFilePath));
    } else if (!request.body.isEmpty()) {
        command += QStringLiteral(" --data-raw %1")
                       .arg(shellQuote(QString::fromUtf8(request.body)));
    }
    command += QStringLiteral(" %1").arg(shellQuote(request.resolvedUrl()));
    QApplication::clipboard()->setText(command);
    statusBar()->showMessage(tr("cURL copied"), 3000);
}

void MainWindow::applyTheme(bool dark) {
    qApp->setStyleSheet(QString::fromUtf8(dark ? m_darkStyle : m_lightStyle));
    m_sidebar->applyTheme(dark);
    for (const auto &tab : m_requestTabData) {
        RequestMethodColors::apply(tab.method, dark);
        updateRequestTabTitle(tab.page);
    }
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
    if (auto *tab = currentRequestTab()) {
        tab->url->setCurrentText(settings.value(QStringLiteral("last_url")).toString());
        tab->timeout->setValue(
            settings.value(QStringLiteral("timeout_seconds")).toInt(30));
    }
    m_historyLimit = settings.value(QStringLiteral("history_limit")).toInt(50);
    m_historyLimit = qBound(1, m_historyLimit, 50);
    const auto active = settings.value(QStringLiteral("active_environment")).toString();
    if (auto *tab = currentRequestTab()) {
        const auto index = tab->environment->findText(active);
        if (index >= 0)
            tab->environment->setCurrentIndex(index);
    }
}

void MainWindow::persistSettings() {
    QJsonObject settings;
    settings.insert(QStringLiteral("theme"),
                    qApp->styleSheet() == QString::fromUtf8(m_darkStyle)
                        ? QStringLiteral("dark") : QStringLiteral("light"));
    if (const auto *tab = currentRequestTab()) {
        settings.insert(QStringLiteral("active_environment"),
                        tab->environment->currentText());
        settings.insert(QStringLiteral("last_url"), tab->url->currentText());
        settings.insert(QStringLiteral("timeout_seconds"), tab->timeout->value());
    }
    settings.insert(QStringLiteral("window_geometry"),
                    QString::fromLatin1(saveGeometry().toBase64()));
    settings.insert(QStringLiteral("history_limit"), m_historyLimit);
    m_settings->save(settings);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    persistSettings();
    QMainWindow::closeEvent(event);
}
