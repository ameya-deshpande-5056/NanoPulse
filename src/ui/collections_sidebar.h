#pragma once

#include <QWidget>

class QComboBox;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;
class SqliteManager;

class CollectionsSidebar : public QWidget {
    Q_OBJECT
public:
    explicit CollectionsSidebar(SqliteManager *storage, QWidget *parent = nullptr);
    void refresh();
    void refreshHistory();
    void applyTheme(bool dark);

signals:
    void requestSelected(const QString &id);
    void historySelected(const QString &method, const QString &url);
    void saveCurrentRequested(const QString &folderId);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void loadChildren(QTreeWidgetItem *parent);
    void addItem(QTreeWidgetItem *parent, const QString &id, const QString &name,
                 bool folder);
    void showContextMenu(const QPoint &position);
    QString selectedFolderId() const;

    SqliteManager *m_storage;
    QLineEdit *m_search;
    QTreeWidget *m_tree;
    QComboBox *m_history;
    bool m_darkTheme = true;
};
