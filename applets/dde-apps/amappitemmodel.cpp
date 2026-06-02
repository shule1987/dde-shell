// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "amappitemmodel.h"
#include "amappitem.h"
#include "appgroupmanager.h"
#include "appitemmodel.h"
#include "objectmanager1interface.h"

#include <DUtil>
#include <QFutureWatcher>
#include <QtConcurrent>

Q_LOGGING_CATEGORY(appsLog, "org.deepin.dde.shell.dde-apps.amappitemmodel")

namespace apps
{
AMAppItemModel::AMAppItemModel(QObject *parent)
    : AppItemModel(parent)
    , m_manager(new ObjectManager("org.desktopspec.ApplicationManager1", "/org/desktopspec/ApplicationManager1", QDBusConnection::sessionBus()))
    , m_ready(false)
{
    qRegisterMetaType<ObjectInterfaceMap>();
    qDBusRegisterMetaType<ObjectInterfaceMap>();
    qRegisterMetaType<ObjectMap>();
    qDBusRegisterMetaType<ObjectMap>();
    qDBusRegisterMetaType<QStringMap>();
    qRegisterMetaType<QStringMap>();
    qRegisterMetaType<PropMap>();
    qDBusRegisterMetaType<PropMap>();
    qDBusRegisterMetaType<QDBusObjectPath>();

    connect(m_manager, &ObjectManager::InterfacesAdded, this, [this](const QDBusObjectPath &objPath, ObjectInterfaceMap interfacesAndProperties) {
        auto desktopId = DUtil::unescapeFromObjectPath(objPath.path().split('/').last());
        if (m_appItemsByDesktopId.contains(desktopId)) {
            qCWarning(appsLog()) << "desktopId: " << desktopId << " already contains";
            return;
        }
        auto appItem = new AMAppItem(objPath, interfacesAndProperties);
        m_appItemsByDesktopId.insert(desktopId, appItem);
        appendRow(appItem);
    });

    connect(m_manager, &ObjectManager::InterfacesRemoved, this, [this](const QDBusObjectPath &objPath, const QStringList &interfaces) {
        Q_UNUSED(interfaces)
        auto desktopId = DUtil::unescapeFromObjectPath(objPath.path().split('/').last());
        auto appItem = m_appItemsByDesktopId.take(desktopId);
        if (!appItem) {
            qCWarning(appsLog()) << "failed find desktopId: " << desktopId;
            return;
        }
        removeRow(appItem->row());
    });

    // Load static desktop info off the UI thread, then mutate the model on this thread.
    auto watcher = new QFutureWatcher<ObjectMap>(this);
    connect(watcher, &QFutureWatcher<ObjectMap>::finished, this, [this, watcher]() {
        const auto apps = watcher->result();

        QList<QStandardItem *> pendingItems;
        for (auto app = apps.cbegin(); app != apps.cend(); app++) {
            auto path = app.key();
            if (!path.path().isEmpty()) {
                const auto desktopId = DUtil::unescapeFromObjectPath(path.path().split('/').last());
                if (!m_appItemsByDesktopId.contains(desktopId)) {
                    auto appItem = new AMAppItem(path, app.value());
                    m_appItemsByDesktopId.insert(desktopId, appItem);
                    pendingItems << appItem;
                }
            }
        }
        if (!pendingItems.isEmpty()) {
            invisibleRootItem()->appendRows(pendingItems);
        }

        m_ready = true;
        Q_EMIT readyChanged(m_ready);
        qCDebug(appsLog) << "AMAppItemModel is now ready with apps counts:" << rowCount();
        watcher->deleteLater();
    });

    watcher->setFuture(QtConcurrent::run([]() {
        ObjectManager manager("org.desktopspec.ApplicationManager1", "/org/desktopspec/ApplicationManager1", QDBusConnection::sessionBus());
        return manager.GetManagedObjects().value();
    }));
}

bool AMAppItemModel::ready() const
{
    return m_ready;
}

AMAppItem * AMAppItemModel::appItem(const QString &id)
{
    return m_appItemsByDesktopId.value(id, nullptr);
}

}
