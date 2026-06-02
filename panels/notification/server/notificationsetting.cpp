// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "notificationsetting.h"

#include <QVariant>
#include <QAbstractListModel>
#include <QLoggingCategory>
#include <QSet>

#include <DConfig>

namespace notification {
Q_DECLARE_LOGGING_CATEGORY(notifyLog)
}
namespace notification {

static const QString InvalidApp {"DS-Invalid-Apps"};
namespace {
enum Roles {
    DesktopIdRole = 0x1000,
    NameRole,
    IconNameRole,
    StartUpWMClassRole,
    NoDisplayRole,
    ActionsRole,
    DDECategoryRole,
    InstalledTimeRole,
    LastLaunchedTimeRole,
    LaunchedTimesRole,
    DockedRole,
    OnDesktopRole,
    AutoStartRole,
    AppTypeRole,
};
}

NotificationSetting::NotificationSetting(QObject *parent)
    : QObject(parent)
    , m_impl(Dtk::Core::DConfig::create("org.deepin.dde.shell", "org.deepin.dde.shell.notification", QString(), this))
{
    invalidAppItemCached();
    connect(m_impl, &Dtk::Core::DConfig::valueChanged, this, [this] (const QString &key) {
        if (key == "appsInfo") {
            invalidAppItemCached();
        } else {
            static const QStringList
                keys{"dndMode", "openByTimeInterval", "lockScreenOpenDndMode", "startTime", "endTime", "notificationClosed", "maxCount", "bubbleCount"};
            if (keys.contains(key)) {
                m_systemInfo = {};
            }
        }
    });
}

void NotificationSetting::setAppAccessor(QAbstractItemModel *model)
{
    if (m_appAccessor == model)
        return;

    if (m_appAccessor) {
        disconnect(m_appAccessor, nullptr, this, nullptr);
    }

    m_appAccessor = model;
    if (!m_appAccessor) {
        QMutexLocker locker(&m_appItemsMutex);
        m_appItems.clear();
        m_appItemById.clear();
        return;
    }

    QObject::connect(m_appAccessor, &QAbstractItemModel::rowsInserted, this, &NotificationSetting::onAppsChanged);
    QObject::connect(m_appAccessor, &QAbstractItemModel::rowsRemoved, this, &NotificationSetting::onAppsChanged);
}

QAbstractItemModel *NotificationSetting::appAccessor() const
{
    return m_appAccessor;
}

void NotificationSetting::setAppValue(const QString &id, AppConfigItem item, const QVariant &value)
{
    auto info = appInfo(id);
    switch (item) {
    case EnableNotification: {
        info["enabled"] = value.toBool();
        break;
    }
    case EnablePreview: {
        info["enablePreview"] = value.toBool();
        break;
    }
    case EnableSound: {
        info["enableSound"] = value.toBool();
        break;
    }
    case ShowInCenter: {
        info["showInCenter"] = value.toBool();
        break;
    }
    case ShowOnLockScreen: {
        info["showOnLockScreen"] = value.toBool();
        break;
    }
    case ShowOnDesktop: {
        info["showOnDesktop"] = value.toBool();
        break;
    }
    default:
        break;
    }

    {
        QMutexLocker locker(&m_appsInfoMutex);
        m_appsInfo[id] = info;
        m_impl->setValue("appsInfo", m_appsInfo);
    }

    Q_EMIT appValueChanged(id, item, value);
}

QVariant NotificationSetting::appValue(const QString &id, AppConfigItem item)
{
    const auto app = appItem(id);
    switch (item) {
    case AppName: {
        return app.appName;
    }
    case AppIcon: {
        return app.appIcon;
    default:
        break;
    }
    }

    const auto info = appInfo(id);
    switch (item) {
    case EnableNotification: {
        return info.value("enabled", true);
    }
    case EnablePreview: {
        return info.value("enablePreview", true);
    }
    case EnableSound: {
        return info.value("enableSound", true);
    }
    case ShowInCenter: {
        return info.value("showInCenter", true);
    }
    case ShowOnLockScreen: {
        return info.value("showOnLockScreen", true);
    }
    case ShowOnDesktop: {
        return info.value("showOnDesktop", true);
    }
    default:
        break;
    }

    return QVariant();
}

void NotificationSetting::setSystemValue(NotificationSetting::SystemConfigItem item, const QVariant &value)
{
    switch (item) {
    case DNDMode:
        m_impl->setValue("dndMode", value);
        break;
    case OpenByTimeInterval:
        m_impl->setValue("openByTimeInterval", value);
        break;
    case LockScreenOpenDNDMode:
        m_impl->setValue("lockScreenOpenDndMode", value);
        break;
    case StartTime:
        m_impl->setValue("startTime", value);
        break;
    case EndTime:
        m_impl->setValue("endTime", value);
        break;
    case CloseNotification:
        m_impl->setValue("notificationClosed", value);
        break;
    case MaxCount:
        m_impl->setValue("maxCount", value);
        break;
    case BubbleCount:
        m_impl->setValue("bubbleCount", value);
        break;
    default:
        return;
    }
    m_systemInfo = {};
    Q_EMIT systemValueChanged(item, value);
}

QVariant NotificationSetting::systemValue(NotificationSetting::SystemConfigItem item)
{
    switch (item) {
    case DNDMode:
        return systemValue("dndMode", true);
    case LockScreenOpenDNDMode:
        return systemValue("lockScreenOpenDndMode", false);
    case OpenByTimeInterval:
        return systemValue("openByTimeInterval", true);
    case StartTime:
        return systemValue("startTime", "07:00");
    case EndTime:
        return systemValue("endTime", "22:00");
    case CloseNotification:
        return systemValue("notificationClosed", false);
    case MaxCount:
        return systemValue("maxCount", 2000);
    case BubbleCount:
        return systemValue("bubbleCount", 3);
    }

    return {};
}

QStringList NotificationSetting::apps() const
{
    QStringList ret;
    const auto items = appItems();
    ret.reserve(items.size());
    for (const auto &item : items) {
        ret << item.id;
    }
    return ret;
}

NotificationSetting::AppItem NotificationSetting::appItem(const QString &id) const
{
    QMutexLocker locker(&(const_cast<NotificationSetting *>(this)->m_appItemsMutex));
    if (m_appItems.isEmpty()) {
        QList<NotificationSetting::AppItem> apps = appItemsImpl();
        auto that = const_cast<NotificationSetting *>(this);
        that->m_appItems = apps;
        that->m_appItemById.clear();
        that->m_appItemById.reserve(apps.size());
        for (const auto &item : std::as_const(apps)) {
            that->m_appItemById.insert(item.id, item);
        }
    }

    return m_appItemById.value(id);
}

QList<NotificationSetting::AppItem> NotificationSetting::appItems() const
{
    QMutexLocker locker(&(const_cast<NotificationSetting *>(this)->m_appItemsMutex));
    if (!m_appItems.isEmpty())
        return m_appItems;

    QList<NotificationSetting::AppItem> apps = appItemsImpl();
    auto that = const_cast<NotificationSetting *>(this);
    that->m_appItems = apps;
    that->m_appItemById.clear();
    that->m_appItemById.reserve(apps.size());
    for (const auto &item : std::as_const(apps)) {
        that->m_appItemById.insert(item.id, item);
    }
    return m_appItems;
}

QList<NotificationSetting::AppItem> NotificationSetting::appItemsImpl() const
{
    if (!m_appAccessor)
        return {};

    QList<NotificationSetting::AppItem> apps;
    apps.reserve(m_appAccessor->rowCount());
    for (int i = 0; i < m_appAccessor->rowCount(); i++) {
        const auto index = m_appAccessor->index(i, 0);

        const auto desktopId = m_appAccessor->data(index, DesktopIdRole).toString();
        const auto icon = m_appAccessor->data(index, IconNameRole).toString();
        const auto name = m_appAccessor->data(index, NameRole).toString();

        NotificationSetting::AppItem item;
        item.id = desktopId;
        item.appIcon = icon;
        item.appName = name;
        apps << item;
    }
    return apps;
}

QVariantMap NotificationSetting::appInfo(const QString &id) const
{
    QMutexLocker locker(&(const_cast<NotificationSetting *>(this)->m_appsInfoMutex));
    if (m_appsInfo.contains(InvalidApp)) {
        const_cast<NotificationSetting *>(this)->m_appsInfo = m_impl->value("appsInfo").toMap();
    }
    if (auto iter = m_appsInfo.find(id); iter != m_appsInfo.end()) {
        return iter.value().toMap();
    }
    return {};
}

void NotificationSetting::onAppsChanged()
{
    const auto old = appItems();
    const auto current = appItemsImpl();

    QSet<QString> oldIds;
    oldIds.reserve(old.size());
    for (const auto &item : old) {
        oldIds.insert(item.id);
    }

    QSet<QString> currentIds;
    currentIds.reserve(current.size());
    for (const auto &item : current) {
        currentIds.insert(item.id);
    }

    QList<NotificationSetting::AppItem> added;
    for (auto item : current) {
        if (!oldIds.contains(item.id)) {
            added << item;
        }
    }
    for (auto item : added) {
        qDebug(notifyLog) << "Application added" << item.id;
        Q_EMIT appAdded(item.id);
    }

    QList<NotificationSetting::AppItem> removed;
    for (auto item : old) {
        if (!currentIds.contains(item.id)) {
            removed << item;
        }
    }
    for (auto item : removed) {
        qDebug(notifyLog) << "Application removed" << item.id;
        Q_EMIT appRemoved(item.id);
    }

    {
        QMutexLocker locker(&m_appItemsMutex);
        m_appItems = current;
        m_appItemById.clear();
        m_appItemById.reserve(current.size());
        for (const auto &item : current) {
            m_appItemById.insert(item.id, item);
        }
    }
}

void NotificationSetting::invalidAppItemCached()
{
    QMutexLocker locker(&m_appsInfoMutex);
    m_appsInfo.clear();
    m_appsInfo[InvalidApp] = QVariant();
}

QVariant NotificationSetting::systemValue(const QString &key, const QVariant &fallback)
{
    if (!m_systemInfo.contains(key))
        m_systemInfo[key] = m_impl->value(key, fallback);
    return m_systemInfo[key];
}

} // notification
