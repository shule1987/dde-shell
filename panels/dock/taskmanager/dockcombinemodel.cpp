// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dockcombinemodel.h"
#include "abstracttaskmanagerinterface.h"
#include "globals.h"
#include "rolecombinemodel.h"
#include "taskmanager.h"

namespace dock
{
DockCombineModel::DockCombineModel(QAbstractItemModel *major, QAbstractItemModel *minor, int majorRoles, CombineFunc func, QObject *parent)
    : RoleCombineModel(major, minor, majorRoles, func, parent)
    , AbstractTaskManagerInterface(this)
{
    // due to role has changed by RoleGroupModel, so we redirect role to TaskManager::Roles.
    const auto combinedRoleNames = RoleCombineModel::roleNames();
    m_roleMaps = {{TaskManager::ActiveRole, combinedRoleNames.key(MODEL_ACTIVE)},
                  {TaskManager::AttentionRole, combinedRoleNames.key(MODEL_ATTENTION)},
                  {TaskManager::DesktopIdRole, combinedRoleNames.key(MODEL_DESKTOPID)},
                  {TaskManager::IconNameRole, combinedRoleNames.key(MODEL_ICONNAME)},
                  {TaskManager::IdentityRole, combinedRoleNames.key(MODEL_IDENTIFY)},
                  {TaskManager::ActionsRole, combinedRoleNames.key(MODEL_ACTIONS)},
                  {TaskManager::NameRole, combinedRoleNames.key(MODEL_NAME)},
                  {TaskManager::WinIdRole, combinedRoleNames.key(MODEL_WINID)},
                  {TaskManager::WinIconRole, combinedRoleNames.key(MODEL_WINICON)},
                  {TaskManager::WinTitleRole, combinedRoleNames.key(MODEL_TITLE)}};
    
    connect(sourceModel(), &QAbstractItemModel::dataChanged, this, &DockCombineModel::onSourceDataChanged);
}

QHash<int, QByteArray> DockCombineModel::roleNames() const
{
    return {{TaskManager::ActiveRole, MODEL_ACTIVE},
            {TaskManager::AttentionRole, MODEL_ATTENTION},
            {TaskManager::DesktopIdRole, MODEL_DESKTOPID},
            {TaskManager::IconNameRole, MODEL_ICONNAME},
            {TaskManager::IdentityRole, MODEL_IDENTIFY},
            {TaskManager::ActionsRole, MODEL_ACTIONS},
            {TaskManager::NameRole, MODEL_NAME},
            {TaskManager::WinIdRole, MODEL_WINID},
            {TaskManager::WinIconRole, MODEL_WINICON},
            {TaskManager::WinTitleRole, MODEL_TITLE}};
}

QVariant DockCombineModel::data(const QModelIndex &index, int role) const
{
    switch (role) {
    case TaskManager::DesktopIdRole: {
        auto res = RoleCombineModel::data(index, m_roleMaps.value(TaskManager::DesktopIdRole)).toString();
        if (res.isEmpty()) {
            auto data = RoleCombineModel::data(index, m_roleMaps.value(TaskManager::IdentityRole)).toStringList();
            res = data.join("-");
        }
        return res;
    }
    case TaskManager::IconNameRole: {
        const QString windowIcon = RoleCombineModel::data(index, m_roleMaps.value(TaskManager::WinIconRole)).toString();
        auto icon = RoleCombineModel::data(index, m_roleMaps.value(TaskManager::IconNameRole)).toString();
        if ((icon.isEmpty() || icon == QLatin1String(DEFAULT_APP_ICONNAME))
            && !windowIcon.isEmpty()) {
            icon = windowIcon;
        }
        return icon;
    }
    default: {
        auto newRole = m_roleMaps.value(role, -1);
        if (newRole == -1) {
            return QVariant();
        }

        return RoleCombineModel::data(index, newRole);
    }
    }

    return QVariant();
}

void DockCombineModel::onSourceDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QList<int> &roles)
{
    // 部分窗口没有desktop, 应用图标使用的是窗口图标，当窗口图标变化的时候也需要发送应用图标变化的通知，以便前端更新
    if ((roles.isEmpty() || roles.contains(TaskManager::WinIconRole)) &&
        !roles.contains(TaskManager::IconNameRole)) {
        Q_EMIT dataChanged(topLeft, bottomRight, { TaskManager::IconNameRole });
    }
}
}
