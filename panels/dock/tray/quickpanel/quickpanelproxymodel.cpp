// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "quickpanelproxymodel.h"

#include <QDebug>
#include <DConfig>
#include <DDBusSender>
DCORE_USE_NAMESPACE

namespace dock {
namespace {
enum {
    PluginId = Qt::UserRole + 10,
    PluginDisplayName,
    QuickSurface,
    QuickSurfaceLayoutType,
    QuickSurfaceItemKey,
    TraySurface,
    TraySurfaceItemKey
};
}
QuickPanelProxyModel::QuickPanelProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    updateQuickPlugins();
    sort(0);
}

QString QuickPanelProxyModel::getTitle(const QString &pluginId) const
{
    const auto index = surfaceIndex(pluginId);
    return surfaceDisplayName(index);
}

bool QuickPanelProxyModel::isQuickPanelPopup(const QString &pluginId, const QString &itemKey) const
{
    const auto index = surfaceIndex(pluginId);
    if (!index.isValid())
        return false;

    const auto item = surfaceItemKey(index);
    return item == itemKey;
}

void QuickPanelProxyModel::openSystemSettings()
{
    DDBusSender()
        .service("org.deepin.dde.ControlCenter1")
        .interface("org.deepin.dde.ControlCenter1")
        .path("/org/deepin/dde/ControlCenter1")
        .method(QString("Show"))
        .call();
}

QVariant QuickPanelProxyModel::data(const QModelIndex &index, int role) const
{
    const auto sourceIndex = mapToSource(index);
    switch (role) {
    case PluginId:
        return surfacePluginId(sourceIndex);
    case PluginDisplayName:
        return surfaceDisplayName(sourceIndex);
    case QuickSurfaceItemKey:
        return surfaceItemKey(sourceIndex);
    case QuickSurfaceLayoutType:
        return surfaceType(sourceIndex);
    case QuickSurface:
        return QVariant::fromValue(surfaceObject(sourceIndex));
    case TraySurface: {
        const auto id = surfacePluginId(sourceIndex);
        return QVariant::fromValue(traySurfaceObject(id));
    }
    case TraySurfaceItemKey: {
        const auto id = surfacePluginId(sourceIndex);
        return traySurfaceItemKey(id);
    }
    }
    return {};
}

QHash<int, QByteArray> QuickPanelProxyModel::roleNames() const
{
    const QHash<int, QByteArray> roles {
        {PluginId, "pluginId"}, // plugin's id.
        {PluginDisplayName, "displayName"}, // plugin's displayName.
        {QuickSurface, "surface"}, // quick surface item.
        {QuickSurfaceLayoutType, "surfaceLayoutType"}, // quick surface's layout type. (1, signal), (2, multi), (4, full)
        {QuickSurfaceItemKey, "surfaceItemKey"}, // quick surface's itemKey.
        {TraySurface, "traySurface"},// tray surface item.
        {TraySurfaceItemKey, "traySurfaceItemKey"},// tray surface itemKey.
    };
    return roles;
}

bool QuickPanelProxyModel::lessThan(const QModelIndex &sourceLeft, const QModelIndex &sourceRight) const
{
    auto leftOrder = pluginOrder(sourceLeft);
    auto rightOrder = pluginOrder(sourceRight);

    return leftOrder < rightOrder;
}

bool QuickPanelProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const auto index = this->sourceModel()->index(sourceRow, 0, sourceParent);
    if (!index.isValid())
        return false;
    if (m_quickPlugins.isEmpty())
        return true;
    const auto &id = surfacePluginId(index);
    return m_quickPluginOrder.contains(id);
}

void QuickPanelProxyModel::updateQuickPlugins()
{
    std::unique_ptr<DConfig> dconfig(DConfig::create("org.deepin.dde.shell", "org.deepin.ds.dock.tray"));
    m_quickPlugins = dconfig->value("quickPlugins").toStringList();
    m_quickPluginOrder.clear();
    m_quickPluginOrder.reserve(m_quickPlugins.size());
    for (int i = 0; i < m_quickPlugins.size(); ++i) {
        m_quickPluginOrder.insert(m_quickPlugins.at(i), i);
    }
    qDebug() << "Fetched QuickPanel's plugin by DConfig,"
             << "plugin list size:" << m_quickPlugins.size();
    invalidate();
}

void QuickPanelProxyModel::watchingCountChanged()
{
    if (!m_trayPluginModel) {
        return;
    }

    connect(m_trayPluginModel, &QAbstractItemModel::rowsInserted, this, &QuickPanelProxyModel::updateTrayItemSurface, Qt::UniqueConnection);
    connect(m_trayPluginModel, &QAbstractItemModel::rowsRemoved, this, &QuickPanelProxyModel::updateTrayItemSurface, Qt::UniqueConnection);
    connect(m_trayPluginModel, &QAbstractItemModel::rowsMoved, this, &QuickPanelProxyModel::updateTrayItemSurface, Qt::UniqueConnection);
    connect(m_trayPluginModel, &QAbstractItemModel::modelReset, this, &QuickPanelProxyModel::updateTrayItemSurface, Qt::UniqueConnection);
    connect(m_trayPluginModel, &QAbstractItemModel::dataChanged, this, &QuickPanelProxyModel::updateTrayItemSurface, Qt::UniqueConnection);
}

int QuickPanelProxyModel::pluginOrder(const QModelIndex &index) const
{
    const auto id = surfacePluginId(index);
    auto ret = m_quickPluginOrder.value(id, -1);
    auto order = surfaceOrder(index);
    if (order > 0) {
        ret = order;
    }
    auto type = surfaceType(index);
    switch (type) {
    case 2:
        ret += 1000;
        break;
    case 4:
        ret += 4000;
        break;
    default:
        ret += 2000;
        break;
    }

    return ret;
}

int QuickPanelProxyModel::surfaceType(const QModelIndex &index) const
{
    // Quick_Panel_Single = 0x40, Quick_Panel_Multi = 0x80, Quick_Panel_Full = 0x100
    auto flags = surfaceValue(index, "pluginFlags").toInt();
    if (flags & 0x100)
        return 4;
    if (flags & 0x80)
        return 2;
    return 1;
}

int QuickPanelProxyModel::surfaceOrder(const QModelIndex &index) const
{
    return surfaceValue(index, "order").toInt();
}

QString QuickPanelProxyModel::surfacePluginId(const QModelIndex &index) const
{
    return surfaceValue(index, "pluginId").toString();
}

QString QuickPanelProxyModel::surfaceItemKey(const QModelIndex &index) const
{
    return surfaceValue(index, "itemKey").toString();
}

QString QuickPanelProxyModel::surfaceDisplayName(const QModelIndex &index) const
{
    return surfaceValue(index, "displayName").toString();
}

QVariant QuickPanelProxyModel::surfaceValue(const QModelIndex &index, const QByteArray &roleName) const
{
    if (auto modelData = surfaceObject(index))
        return modelData->property(roleName);

    return {};
}

QModelIndex QuickPanelProxyModel::surfaceIndex(const QString &pluginId) const
{
    const auto targetModel = surfaceModel();
    if (!targetModel)
        return {};
    for (int i = 0; i < targetModel->rowCount(); i++) {
        const auto index = targetModel->index(i, 0);
        const auto id = surfacePluginId(index);
        if (id == pluginId)
            return index;
    }
    return {};
}

QObject *QuickPanelProxyModel::surfaceObject(const QModelIndex &index) const
{
    const auto modelDataRole = roleByName("shellSurface");
    if (modelDataRole >= 0)
        return surfaceModel()->data(index, modelDataRole).value<QObject *>();

    return nullptr;
}

QObject *QuickPanelProxyModel::traySurfaceObject(const QString &pluginId) const
{
    return m_traySurfaceByPluginId.value(pluginId, nullptr);
}

QString QuickPanelProxyModel::traySurfaceItemKey(const QString &pluginId) const
{
    return m_traySurfaceItemKeyByPluginId.value(pluginId);
}

int QuickPanelProxyModel::roleByName(const QByteArray &roleName) const
{
    if (!surfaceModel())
        return -1;
    const auto roleNames = surfaceModel()->roleNames();
    return roleNames.key(roleName, -1);
}

QAbstractListModel *QuickPanelProxyModel::surfaceModel() const
{
    return qobject_cast<QAbstractListModel *>(sourceModel());
}

void QuickPanelProxyModel::updateTrayItemSurface()
{
    rebuildTraySurfaceCache();
    emit trayItemSurfaceChanged();
    if (rowCount() > 0)
        emit dataChanged(index(0, 0), index(rowCount() - 1, 0), {TraySurface, TraySurfaceItemKey});
}

void QuickPanelProxyModel::rebuildTraySurfaceCache()
{
    m_traySurfaceByPluginId.clear();
    m_traySurfaceItemKeyByPluginId.clear();

    if (!m_trayPluginModel) {
        return;
    }

    const auto modelDataRole = m_trayPluginModel->roleNames().key("shellSurface", -1);
    if (modelDataRole < 0) {
        return;
    }

    const int count = m_trayPluginModel->rowCount();
    for (int i = 0; i < count; ++i) {
        const auto index = m_trayPluginModel->index(i, 0);
        const auto item = index.data(modelDataRole).value<QObject *>();
        if (!item) {
            continue;
        }

        const auto id = item->property("pluginId").toString();
        if (id.isEmpty()) {
            continue;
        }

        m_traySurfaceByPluginId.insert(id, item);
        m_traySurfaceItemKeyByPluginId.insert(id, item->property("itemKey").toString());
    }
}

void QuickPanelProxyModel::classBegin()
{
}

void QuickPanelProxyModel::componentComplete()
{
    updateTrayItemSurface();
}

QObject *QuickPanelProxyModel::trayItemSurface() const
{
    if (m_trayItemPluginId.isEmpty())
        return nullptr;

    return traySurfaceObject(m_trayItemPluginId);
}

QString QuickPanelProxyModel::trayItemPluginId() const
{
    return m_trayItemPluginId;
}

void QuickPanelProxyModel::setTrayItemPluginId(const QString &newTrayItemPluginId)
{
    if (m_trayItemPluginId == newTrayItemPluginId)
        return;
    m_trayItemPluginId = newTrayItemPluginId;
    emit trayItemPluginIdChanged();
    updateTrayItemSurface();
}

QAbstractItemModel *QuickPanelProxyModel::trayPluginModel() const
{
    return m_trayPluginModel;
}

void QuickPanelProxyModel::setTrayPluginModel(QAbstractItemModel *newTrayPluginModel)
{
    if (m_trayPluginModel == newTrayPluginModel)
        return;
    if (m_trayPluginModel) {
        disconnect(m_trayPluginModel, nullptr, this, nullptr);
    }
    m_trayPluginModel = newTrayPluginModel;
    rebuildTraySurfaceCache();
    watchingCountChanged();
    emit trayPluginModelChanged();
    updateTrayItemSurface();
}

}
