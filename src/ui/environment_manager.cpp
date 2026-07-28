#include "environment_manager.h"

#include "../storage/sqlite_manager.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QHeaderView>
#include <QVBoxLayout>

EnvironmentManager::EnvironmentManager(SqliteManager *storage, QWidget *parent)
    : QDialog(parent), m_storage(storage) {
    setWindowTitle(tr("Environments"));
    resize(700, 440);
    m_names = new QListWidget(this);
    m_variables = new QTableWidget(1, 2, this);
    m_variables->setHorizontalHeaderLabels({tr("Key"), tr("Value")});
    m_variables->horizontalHeader()->setStretchLastSection(true);
    auto *newButton = new QPushButton(tr("New"), this);
    auto *deleteButton = new QPushButton(tr("Delete"), this);
    auto *saveButton = new QPushButton(tr("Save"), this);
    auto *importButton = new QPushButton(tr("Import"), this);
    auto *exportButton = new QPushButton(tr("Export"), this);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);

    auto *left = new QVBoxLayout;
    left->addWidget(m_names);
    left->addWidget(newButton);
    left->addWidget(deleteButton);
    auto *rightButtons = new QHBoxLayout;
    rightButtons->addWidget(saveButton);
    rightButtons->addWidget(importButton);
    rightButtons->addWidget(exportButton);
    rightButtons->addStretch();
    auto *right = new QVBoxLayout;
    right->addWidget(m_variables);
    right->addLayout(rightButtons);
    auto *content = new QHBoxLayout;
    content->addLayout(left, 1);
    content->addLayout(right, 3);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(content);
    layout->addWidget(buttons);

    connect(m_names, &QListWidget::currentRowChanged,
            this, &EnvironmentManager::loadSelected);
    connect(m_variables, &QTableWidget::cellChanged, this,
            [this](int row, int) {
        if (row == m_variables->rowCount() - 1) {
            const auto *key = m_variables->item(row, 0);
            const auto *value = m_variables->item(row, 1);
            if ((key && !key->text().isEmpty())
                || (value && !value->text().isEmpty()))
                m_variables->insertRow(m_variables->rowCount());
        }
    });
    connect(newButton, &QPushButton::clicked, this, [this] {
        bool accepted = false;
        const auto name = QInputDialog::getText(
            this, tr("New Environment"), tr("Name:"), QLineEdit::Normal, {},
            &accepted);
        if (accepted && !name.trimmed().isEmpty()) {
            m_storage->saveEnvironment(name.trimmed(), {});
            reload();
            const auto matches = m_names->findItems(name.trimmed(), Qt::MatchExactly);
            if (!matches.isEmpty())
                m_names->setCurrentItem(matches.first());
            emit environmentsChanged();
        }
    });
    connect(deleteButton, &QPushButton::clicked, this, [this] {
        if (m_names->currentItem()) {
            m_storage->deleteEnvironment(m_names->currentItem()->text());
            reload();
            emit environmentsChanged();
        }
    });
    connect(saveButton, &QPushButton::clicked,
            this, &EnvironmentManager::saveSelected);
    connect(importButton, &QPushButton::clicked,
            this, &EnvironmentManager::importJson);
    connect(exportButton, &QPushButton::clicked,
            this, &EnvironmentManager::exportJson);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    reload();
}

void EnvironmentManager::reload() {
    const auto current = m_names->currentItem()
        ? m_names->currentItem()->text() : QString();
    m_names->clear();
    for (const auto &environment : m_storage->environments())
        m_names->addItem(environment.first);
    const auto matches = m_names->findItems(current, Qt::MatchExactly);
    if (!matches.isEmpty())
        m_names->setCurrentItem(matches.first());
    else if (m_names->count())
        m_names->setCurrentRow(0);
    else
        m_variables->setRowCount(1);
}

void EnvironmentManager::loadSelected() {
    if (!m_names->currentItem())
        return;
    for (const auto &environment : m_storage->environments()) {
        if (environment.first != m_names->currentItem()->text())
            continue;
        m_variables->blockSignals(true);
        m_variables->setRowCount(environment.second.size() + 1);
        int row = 0;
        for (auto it = environment.second.begin(); it != environment.second.end();
             ++it, ++row) {
            m_variables->setItem(row, 0, new QTableWidgetItem(it.key()));
            m_variables->setItem(row, 1, new QTableWidgetItem(it.value()));
        }
        m_variables->blockSignals(false);
        break;
    }
}

void EnvironmentManager::saveSelected() {
    if (!m_names->currentItem())
        return;
    QMap<QString, QString> variables;
    for (int row = 0; row < m_variables->rowCount(); ++row) {
        const auto *key = m_variables->item(row, 0);
        const auto *value = m_variables->item(row, 1);
        if (key && !key->text().trimmed().isEmpty())
            variables.insert(key->text().trimmed(), value ? value->text() : QString());
    }
    m_storage->saveEnvironment(m_names->currentItem()->text(), variables);
    emit environmentsChanged();
}

void EnvironmentManager::importJson() {
    const auto path = QFileDialog::getOpenFileName(
        this, tr("Import Environments"), {}, tr("JSON files (*.json)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const auto object = QJsonDocument::fromJson(file.readAll()).object();
    for (auto environment = object.begin(); environment != object.end();
         ++environment) {
        QMap<QString, QString> variables;
        const auto values = environment.value().toObject();
        for (auto value = values.begin(); value != values.end(); ++value)
            variables.insert(value.key(), value.value().toString());
        m_storage->saveEnvironment(environment.key(), variables);
    }
    reload();
    emit environmentsChanged();
}

void EnvironmentManager::exportJson() {
    const auto path = QFileDialog::getSaveFileName(
        this, tr("Export Environments"), QStringLiteral("environments.json"),
        tr("JSON files (*.json)"));
    if (path.isEmpty())
        return;
    QJsonObject root;
    for (const auto &environment : m_storage->environments()) {
        QJsonObject values;
        for (auto it = environment.second.begin(); it != environment.second.end(); ++it)
            values.insert(it.key(), it.value());
        root.insert(environment.first, values);
    }
    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}
