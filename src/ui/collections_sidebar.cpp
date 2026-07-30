#include "collections_sidebar.h"

#include "../storage/sqlite_manager.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QEvent>
#include <QHelpEvent>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QToolTip>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
constexpr int IdRole = Qt::UserRole;
constexpr int FolderRole = Qt::UserRole + 1;
constexpr int LoadedRole = Qt::UserRole + 2;
}

CollectionsSidebar::CollectionsSidebar(SqliteManager *storage, QWidget *parent)
    : QWidget(parent), m_storage(storage) {
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Filter collections"));
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->viewport()->installEventFilter(this);
    m_history = new QComboBox(this);
    m_history->setPlaceholderText(tr("Recent requests"));
    m_history->installEventFilter(this);
    m_history->view()->viewport()->installEventFilter(this);
    auto *clearHistory = new QPushButton(tr("Clear history"), this);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("Collections"), this));
    layout->addWidget(m_search);
    layout->addWidget(m_tree, 1);
    layout->addWidget(m_history);
    layout->addWidget(clearHistory);

    connect(m_tree, &QTreeWidget::itemExpanded, this,
            [this](QTreeWidgetItem *item) {
        if (item->data(0, FolderRole).toBool()
            && !item->data(0, LoadedRole).toBool())
            loadChildren(item);
    });
    connect(m_tree, &QTreeWidget::itemActivated, this,
            [this](QTreeWidgetItem *item) {
        if (!item->data(0, FolderRole).toBool())
            emit requestSelected(item->data(0, IdRole).toString());
    });
    connect(m_tree, &QTreeWidget::customContextMenuRequested,
            this, &CollectionsSidebar::showContextMenu);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &text) {
        for (int index = 0; index < m_tree->topLevelItemCount(); ++index) {
            auto *item = m_tree->topLevelItem(index);
            item->setHidden(!text.isEmpty()
                            && !item->text(0).contains(text, Qt::CaseInsensitive));
        }
    });
    connect(m_history, &QComboBox::activated, this, [this](int index) {
        if (index >= 0) {
            m_history->setProperty(
                "requestMethod", m_history->itemData(index, Qt::UserRole));
            m_history->style()->unpolish(m_history);
            m_history->style()->polish(m_history);
            emit historySelected(m_history->itemData(index, Qt::UserRole).toString(),
                                 m_history->itemData(index, Qt::UserRole + 1).toString());
        }
    });
    connect(clearHistory, &QPushButton::clicked, this, [this] {
        m_storage->clearHistory();
        refreshHistory();
    });
    refresh();
}

void CollectionsSidebar::refresh() {
    m_tree->clear();
    for (const auto &item : m_storage->collectionChildren({}))
        addItem(nullptr, item.id, item.name, item.folder);
    refreshHistory();
}

void CollectionsSidebar::refreshHistory() {
    m_history->clear();
    const auto items = m_storage->history(5);
    for (const auto &item : items) {
        m_history->addItem(item.method + QStringLiteral("  ") + item.url);
        const auto index = m_history->count() - 1;
        m_history->setItemData(index, item.method, Qt::UserRole);
        m_history->setItemData(index, item.url, Qt::UserRole + 1);
    }
    if (m_history->count() > 0) {
        m_history->setProperty(
            "requestMethod", m_history->itemData(0, Qt::UserRole));
        m_history->style()->unpolish(m_history);
        m_history->style()->polish(m_history);
    }
}

bool CollectionsSidebar::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() != QEvent::ToolTip)
        return QWidget::eventFilter(watched, event);

    auto *helpEvent = static_cast<QHelpEvent *>(event);
    QString text;
    int availableWidth = 0;
    if (watched == m_tree->viewport()) {
        const auto *item = m_tree->itemAt(helpEvent->pos());
        if (item) {
            const auto rect = m_tree->visualItemRect(item);
            text = item->text(0);
            availableWidth = m_tree->viewport()->width() - rect.x() - 4;
        }
    } else if (watched == m_history) {
        QStyleOptionComboBox option;
        option.initFrom(m_history);
        option.currentText = m_history->currentText();
        text = m_history->currentText();
        availableWidth = m_history->style()
                             ->subControlRect(QStyle::CC_ComboBox, &option,
                                              QStyle::SC_ComboBoxEditField, m_history)
                             .width();
    } else if (watched == m_history->view()->viewport()) {
        const auto index = m_history->view()->indexAt(helpEvent->pos());
        if (index.isValid()) {
            const auto rect = m_history->view()->visualRect(index);
            text = index.data().toString();
            availableWidth = m_history->view()->viewport()->width() - rect.x() - 4;
        }
    }

    if (!text.isEmpty()
        && fontMetrics().horizontalAdvance(text) > availableWidth) {
        QToolTip::showText(helpEvent->globalPos(), text, this);
        return true;
    }
    QToolTip::hideText();
    return false;
}

void CollectionsSidebar::loadChildren(QTreeWidgetItem *parent) {
    parent->takeChildren();
    const auto id = parent->data(0, IdRole).toString();
    for (const auto &item : m_storage->collectionChildren(id))
        addItem(parent, item.id, item.name, item.folder);
    parent->setData(0, LoadedRole, true);
}

void CollectionsSidebar::addItem(QTreeWidgetItem *parent, const QString &id,
                                 const QString &name, bool folder) {
    auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
    item->setText(0, name);
    item->setData(0, IdRole, id);
    item->setData(0, FolderRole, folder);
    item->setData(0, LoadedRole, false);
    if (folder)
        new QTreeWidgetItem(item, {tr("Loading…")});
}

QString CollectionsSidebar::selectedFolderId() const {
    const auto *item = m_tree->currentItem();
    if (!item)
        return {};
    return item->data(0, FolderRole).toBool()
        ? item->data(0, IdRole).toString()
        : item->parent() ? item->parent()->data(0, IdRole).toString() : QString();
}

void CollectionsSidebar::showContextMenu(const QPoint &position) {
    auto *item = m_tree->itemAt(position);
    QMenu menu(this);
    auto *newRequest = menu.addAction(tr("New Request"));
    auto *newFolder = menu.addAction(tr("New Folder"));
    QAction *duplicate = nullptr;
    QAction *rename = nullptr;
    QAction *remove = nullptr;
    if (item) {
        if (!item->data(0, FolderRole).toBool())
            duplicate = menu.addAction(tr("Duplicate"));
        rename = menu.addAction(tr("Rename"));
        remove = menu.addAction(tr("Delete"));
    }
    auto *choice = menu.exec(m_tree->viewport()->mapToGlobal(position));
    if (!choice)
        return;
    if (choice == newRequest) {
        emit saveCurrentRequested(selectedFolderId());
    } else if (choice == newFolder) {
        bool accepted = false;
        const auto name = QInputDialog::getText(
            this, tr("New Folder"), tr("Name:"), QLineEdit::Normal, {}, &accepted);
        if (accepted && !name.trimmed().isEmpty()) {
            m_storage->createFolder(name.trimmed(), selectedFolderId());
            refresh();
        }
    } else if (choice == duplicate) {
        const auto request = m_storage->request(item->data(0, IdRole).toString());
        m_storage->saveRequest(item->text(0) + tr(" Copy"), request,
                               selectedFolderId());
        refresh();
    } else if (choice == rename) {
        bool accepted = false;
        const auto name = QInputDialog::getText(
            this, tr("Rename"), tr("Name:"), QLineEdit::Normal,
            item->text(0), &accepted);
        if (accepted && !name.trimmed().isEmpty()) {
            m_storage->renameItem(item->data(0, IdRole).toString(), name.trimmed());
            refresh();
        }
    } else if (choice == remove
               && QMessageBox::question(this, tr("Delete"),
                                        tr("Delete the selected item?"))
                      == QMessageBox::Yes) {
        m_storage->deleteItem(item->data(0, IdRole).toString());
        refresh();
    }
}
