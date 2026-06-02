// SPDX-FileCopyrightText: 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "itemmodel.h"
#include "abstractitem.h"
#include "appitem.h"
#include "globals.h"
#include "taskmanager.h"
#include "taskmanagersettings.h"

#include <algorithm>

#include <QVariant>
#include <QPointer>
#include <QAbstractListModel>
#include <QJsonArray>
#include <QJsonObject>

namespace dock {
ItemModel* ItemModel::instance()
{
    static ItemModel* appItemModel = nullptr;
    if (appItemModel == nullptr) {
        appItemModel = new ItemModel();
    }
    return appItemModel;
}

ItemModel::ItemModel(QObject* parent)
    : QAbstractListModel(parent)
    , m_recentSize(3)
{
}

QHash<int, QByteArray> ItemModel::roleNames() const
{
    // clang-format off
    return {{ItemModel::ItemIdRole, MODEL_ITEMID},
        {TaskManager::NameRole, MODEL_NAME},
        {TaskManager::IconNameRole, MODEL_ICONNAME},
        {TaskManager::ActiveRole, MODEL_ACTIVE},
        {TaskManager::AttentionRole, MODEL_ATTENTION},
        {TaskManager::MenusRole, MODEL_MENUS},
        {TaskManager::DockedRole, MODEL_DOCKED},
        {TaskManager::WindowsRole, MODEL_WINDOWS},
        {TaskManager::WinIconRole, MODEL_WINICON},
        {ItemModel::DockedDirRole, "dockedDir"}
    };
    // clang-format on
}

int ItemModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant ItemModel::data(const QModelIndex &index, int role) const
{
    if (!hasIndex(index.row(), index.column(), index.parent())) {
        return QVariant();
    }

    auto item = m_items[index.row()];
    // clang-format off
    switch (role) {
        case ItemModel::ItemIdRole: return item->id();
        case TaskManager::NameRole: return item->name();
        case TaskManager::IconNameRole: return item->icon();
        case TaskManager::ActiveRole: return item->isActive();
        case TaskManager::AttentionRole: return item->isAttention();
        case TaskManager::MenusRole: return item->menus();
        case TaskManager::DockedRole: return item->isDocked();
        case TaskManager::WindowsRole: return item->data().toStringList();
        case TaskManager::WinIconRole: return item->data().toStringList();
        case ItemModel::DockedDirRole: return item->data().toString();
    }
    // clang-format on
    return QVariant();
}

QJsonArray ItemModel::dumpDockedItems() const
{
    QJsonArray result;

    foreach(auto item, m_items) {
        if (!item->isDocked())
            continue;

        QJsonObject tmp;
        tmp["id"] = item->id();
        tmp["type"] = item->type();
        result.append(tmp);
    }

    return result;
}

void ItemModel::requestActivate(const QModelIndex &index) const
{
    QString itemId = data(index).toString();

    auto item = ItemModel::instance()->getItemById(itemId);
    if (!item) {
        return;
    }

    item->handleClick(QString());
}

void ItemModel::requestNewInstance(const QModelIndex &index, const QString &action) const
{
    QString itemId = data(index).toString();

    auto item = ItemModel::instance()->getItemById(itemId);
    if (!item) {
        return;
    }

    item->handleClick(DOCK_ACTIN_LAUNCH);
}

void ItemModel::requestClose(const QModelIndex &index, bool force) const
{
    QString itemId = data(index).toString();

    auto item = ItemModel::instance()->getItemById(itemId);
    if (!item) {
        return;
    }

    item->handleClick(force ? DOCK_ACTION_FORCEQUIT : DOCK_ACTION_CLOSEALL);
}

void ItemModel::requestOpenUrls(const QModelIndex &index, const QList<QUrl> &urls) const
{
    QString itemId = data(index).toString();

    auto item = ItemModel::instance()->getItemById(itemId);
    if (!item) {
        return;
    }

    // convert urls to string list
    QStringList urlsStr;
    for (auto url : std::as_const(urls)) {
        urlsStr.append(url.toString());
    }

    item->handleFileDrop(urlsStr);
}

void ItemModel::requestWindowsView(const QModelIndexList &indexes) const
{
    // nothing here, dummy entry.
}

void ItemModel::requestUpdateWindowIconGeometry(const QModelIndex &index, const QRect &geometry, QObject *delegate) const
{
    QString itemId = data(index).toString();

    QPointer<AppItem> item = static_cast<AppItem *>(ItemModel::instance()->getItemById(itemId).get());
    if (item.isNull())
        return;

    for (auto window : item->getAppendWindows()) {
        window->setWindowIconGeometry(qobject_cast<QWindow *>(delegate), geometry);
    }
}

QPointer<AbstractItem> ItemModel::getItemById(const QString& id) const
{
    return m_itemById.value(id, nullptr);
}

void ItemModel::addItem(QPointer<AbstractItem> item)
{
    if (m_items.contains(item)) return;

    connect(item.get(), &AbstractItem::destroyed, this, &ItemModel::onItemDestroyed, Qt::UniqueConnection);
    connect(item.get(), &AbstractItem::nameChanged, this, &ItemModel::onItemChanged, Qt::UniqueConnection);
    connect(item.get(), &AbstractItem::iconChanged, this, &ItemModel::onItemChanged, Qt::UniqueConnection);
    connect(item.get(), &AbstractItem::activeChanged, this, &ItemModel::onItemChanged, Qt::UniqueConnection);
    connect(item.get(), &AbstractItem::attentionChanged, this, &ItemModel::onItemChanged, Qt::UniqueConnection);
    connect(item.get(), &AbstractItem::menusChanged, this, &ItemModel::onItemChanged, Qt::UniqueConnection);
    connect(item.get(), &AbstractItem::dockedChanged, this, &ItemModel::onItemChanged, Qt::UniqueConnection);
    connect(item.get(), &AbstractItem::dataChanged, this, &ItemModel::onItemChanged, Qt::UniqueConnection);

    const int row = rowCount();
    beginInsertRows(QModelIndex(), row, row);
    m_items.append(item);
    m_itemById.insert(item->id(), item);
    m_itemRows.insert(item.get(), row);
    endInsertRows();
}

void ItemModel::onItemDestroyed()
{
    auto item = qobject_cast<AbstractItem*>(sender());
    auto beginIndex = m_itemRows.value(item, -1);
    auto lastIndex = beginIndex;

    if (beginIndex == -1 || lastIndex == -1) return;

    beginRemoveRows(QModelIndex(), beginIndex, lastIndex);
    const auto itemId = item->id();
    m_items.removeAt(beginIndex);
    m_itemById.remove(itemId);
    m_itemRows.remove(item);
    for (int i = beginIndex; i < m_items.size(); ++i) {
        if (m_items.at(i)) {
            m_itemRows.insert(m_items.at(i).get(), i);
        }
    }
    endRemoveRows();
}

void ItemModel::onItemChanged()
{
    auto item = qobject_cast<AbstractItem*>(sender());
    if (!item) return;
    const int row = m_itemRows.value(item, -1);
    if (row < 0) return;

    const auto modelIndex = index(row, 0);
    Q_EMIT dataChanged(modelIndex, modelIndex);
}
}
