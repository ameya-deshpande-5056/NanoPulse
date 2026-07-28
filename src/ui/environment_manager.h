#pragma once

#include <QDialog>

class QListWidget;
class QTableWidget;
class SqliteManager;

class EnvironmentManager : public QDialog {
    Q_OBJECT
public:
    explicit EnvironmentManager(SqliteManager *storage, QWidget *parent = nullptr);

signals:
    void environmentsChanged();

private:
    void reload();
    void loadSelected();
    void saveSelected();
    void importJson();
    void exportJson();

    SqliteManager *m_storage;
    QListWidget *m_names;
    QTableWidget *m_variables;
};
