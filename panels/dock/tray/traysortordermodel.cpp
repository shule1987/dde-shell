// SPDX-FileCopyrightText: 2024 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "traysortordermodel.h"
#include "constants.h"
#include "trayitempositionmanager.h"

#include <QDebug>
#include <QDBusMessage>
#include <QDBusConnection>

#include <DConfig>

namespace docktray {

const QString SECTION_TRAY_ACTION = QLatin1String("tray-action");
const QString SECTION_STASHED = QLatin1String("stashed");
const QString SECTION_COLLAPSABLE = QLatin1String("collapsable");
const QString SECTION_FIXED = QLatin1String("fixed");
const QString SECTION_PINNED = QLatin1String("pinned");

TraySortOrderModel::TraySortOrderModel(QObject *parent)
    : QStandardItemModel(parent)
    , m_dconfig(Dtk::Core::DConfig::create("org.deepin.dde.shell", "org.deepin.ds.dock.tray"))
{
    QHash<int, QByteArray> defaultRoleNames = roleNames();
    defaultRoleNames.insert({
        {TraySortOrderModel::SurfaceIdRole, QByteArrayLiteral("surfaceId")},
        {TraySortOrderModel::VisibilityRole, QByteArrayLiteral("visibility")},
        {TraySortOrderModel::DockVisibleRole, QByteArrayLiteral("dockVisible")},
        {TraySortOrderModel::SectionTypeRole, QByteArrayLiteral("sectionType")},
        {TraySortOrderModel::VisualIndexRole, QByteArrayLiteral("visualIndex")},
        {TraySortOrderModel::DelegateTypeRole, QByteArrayLiteral("delegateType")},
        {TraySortOrderModel::ForbiddenSectionsRole, QByteArrayLiteral("forbiddenSections")}
    });
    setItemRoleNames(defaultRoleNames);

    // init sort order data and hidden list data
    loadDataFromDConfig();

    // internal tray actions
    appendRow(createTrayItem("internal/action-stash-placeholder", SECTION_STASHED, "action-stash-placeholder"));
    appendRow(createTrayItem("internal/action-show-stash", SECTION_TRAY_ACTION, "action-show-stash"));
    appendRow(createTrayItem("internal/action-toggle-collapse", SECTION_TRAY_ACTION, "action-toggle-collapse"));
    appendRow(createTrayItem("internal/action-toggle-quick-settings", SECTION_TRAY_ACTION, "action-toggle-quick-settings"));

    connect(m_dconfig.get(), &Dtk::Core::DConfig::valueChanged, this, [this](const QString &key){
        if (key == QLatin1String("hiddenSurfaceIds") || key == QLatin1String("dockHiddenSurfaceIds")) {
            loadDataFromDConfig();
            updateVisualIndexes();
        }
    });

    connect(this, &TraySortOrderModel::availableSurfacesChanged, this, &TraySortOrderModel::onAvailableSurfacesChanged);

    connect(this, &TraySortOrderModel::collapsedChanged, this, [this](){
        qDebug() << "collapsedChanged";
        updateVisualIndexes();
        saveDataToDConfig();
    });
    connect(this, &TraySortOrderModel::actionsAlwaysVisibleChanged, this, [this](){
        qDebug() << "actionsAlwaysVisibleChanged";
        updateVisualIndexes();
    });
    updateVisualIndexes();
}

TraySortOrderModel::~TraySortOrderModel()
{
    // dtor
}

bool TraySortOrderModel::dropToStashTray(const QString &draggedSurfaceId, int dropVisualIndex, bool isBefore)
{
    Q_UNUSED(dropVisualIndex)
    Q_UNUSED(isBefore)
    // Check if the dragged tray surfaceId exists. Reject if not the case
    QList<QStandardItem *> draggedItems = findItems(draggedSurfaceId);
    if (draggedItems.isEmpty()) return false;
    Q_ASSERT(draggedItems.count() == 1);
    QStandardItem * draggedItem = draggedItems[0];
    if (draggedItem->data(ForbiddenSectionsRole).toStringList().contains(SECTION_STASHED)) return false;
    QStringList * sourceSection = getSection(draggedItem->data(SectionTypeRole).toString());

    // Ensure position adjustment will be saved at last
    auto deferSaveSortOrder = qScopeGuard([this](){saveDataToDConfig();});

    // drag inside the stashed section
    if (sourceSection == &m_stashedIds) {
        return false;
    } else {
        sourceSection->removeOne(draggedSurfaceId);
        m_stashedIds.append(draggedSurfaceId);
        updateVisualIndexes();
        return true;
    }
}

// drop existing item to tray, return true if drop attempt is accepted, false if rejected
bool TraySortOrderModel::dropToDockTray(const QString &draggedSurfaceId, int dropVisualIndex, bool isBefore)
{
    // Check if the dragged tray surfaceId exists. Reject if not the case
    QList<QStandardItem *> draggedItems = findItems(draggedSurfaceId);
    if (draggedItems.isEmpty()) return false;
    Q_ASSERT(draggedItems.count() == 1);
    QStringList * sourceSection = getSection(draggedItems[0]->data(SectionTypeRole).toString());
    QStringList forbiddenSections(draggedItems[0]->data(ForbiddenSectionsRole).toStringList());

    // Find the item attempted to drop on
    QStandardItem * dropOnItem = findItemByVisualIndex(dropVisualIndex, DockTraySection);
    if (!dropOnItem) return false;
    QString dropOnSurfaceId(dropOnItem->data(SurfaceIdRole).toString());

    // Ensure position adjustment will be saved at last
    auto deferSaveSortOrder = qScopeGuard([this](){saveDataToDConfig();});

    // If it's hidden, remove it from the hidden list. updateVisualIndexes() will update the
    // item's VisibilityRole property.
    // This is mainly for the drag item from quick settings panel feature.
    auto deferUpdateVisualIndex = qScopeGuard([this](){updateVisualIndexes();});
    if (m_hiddenIdSet.contains(draggedSurfaceId)) {
        m_hiddenIds.removeOne(draggedSurfaceId);
        m_hiddenIdSet.remove(draggedSurfaceId);
        handlePluginVisibleChanged(draggedSurfaceId, true);
    }

    if (dropOnSurfaceId == QLatin1String("internal/action-show-stash")) {
        // show stash action is always the first action, drop before it consider as drop into stashed area
        if (sourceSection != &m_stashedIds) {
            sourceSection->removeOne(draggedSurfaceId);
            m_stashedIds.append(draggedSurfaceId);
            return true;
        } else {
            // already in the stashed tray
            return false;
        }
        if (sourceSection == &m_collapsableIds) {
            // same-section move
            m_collapsableIds.move(m_collapsableIds.indexOf(draggedSurfaceId), 0);
        } else {
            // prepend to collapsable section
            sourceSection->removeOne(draggedSurfaceId);
            m_collapsableIds.prepend(draggedSurfaceId);
        }
        return true;
    }

    if (dropOnSurfaceId == QLatin1String("internal/action-toggle-collapse")) {
        QStringList * targetSection = isBefore ? &m_collapsableIds : &m_pinnedIds;
        if (isBefore) {
            // move to the end of collapsable section
            if (forbiddenSections.contains(SECTION_COLLAPSABLE)) return false;
            if (targetSection == sourceSection) {
                m_collapsableIds.move(m_collapsableIds.indexOf(draggedSurfaceId), m_collapsableIds.count() - 1);
            } else {
                sourceSection->removeOne(draggedSurfaceId);
                m_collapsableIds.append(draggedSurfaceId);
            }
        } else {
            // move to the beginning of pinned section
            if (forbiddenSections.contains(SECTION_PINNED)) return false;
            if (targetSection == sourceSection) {
                m_pinnedIds.move(m_pinnedIds.indexOf(draggedSurfaceId), 0);
            } else {
                sourceSection->removeOne(draggedSurfaceId);
                m_pinnedIds.prepend(draggedSurfaceId);
            }
        }
        return true;
    }

    if (dropOnSurfaceId == QLatin1String("internal/action-toggle-quick-settings")) {
        if (forbiddenSections.contains(SECTION_PINNED)) return false;
        if (sourceSection == &m_pinnedIds) {
            int idx = m_pinnedIds.indexOf(draggedSurfaceId);
            if (idx >= 0) {
                m_pinnedIds.move(idx, m_pinnedIds.count() - 1);
            }
        } else {
            sourceSection->removeOne(draggedSurfaceId);
            m_pinnedIds.append(draggedSurfaceId);
        }
        return true;
    }

    if (dropOnSurfaceId == draggedSurfaceId) {
        // same item, don't need to do anything
        return false;
    }

    QString targetSectionName(dropOnItem->data(SectionTypeRole).toString());
    if (forbiddenSections.contains(targetSectionName)) return false;
    QStringList * targetSection = getSection(targetSectionName);
    if (targetSection == sourceSection) {
        int sourceIndex = targetSection->indexOf(draggedSurfaceId);
        int targetIndex = targetSection->indexOf(dropOnSurfaceId);
        if (isBefore) targetIndex--;
        if (targetIndex < 0) targetIndex = 0;
        if (sourceIndex == targetIndex) {
            // same item (draggedSurfaceId != dropOnSurfaceId caused by isBefore).
            return false;
        }
        targetSection->move(sourceIndex, targetIndex);
    } else {
        int targetIndex = targetSection->indexOf(dropOnSurfaceId);
        if (!isBefore) targetIndex++;
        sourceSection->removeOne(draggedSurfaceId);
        targetSection->insert(targetIndex, draggedSurfaceId);
    }
    return true;
}

void TraySortOrderModel::setSurfaceVisible(const QString &surfaceId, bool visible)
{
    if (visible) {
        if (m_hiddenIdSet.contains(surfaceId)) {
            m_hiddenIds.removeOne(surfaceId);
            m_hiddenIdSet.remove(surfaceId);
        }
    } else {
        if (!m_hiddenIdSet.contains(surfaceId)) {
            m_hiddenIds.append(surfaceId);
            m_hiddenIdSet.insert(surfaceId);
        }
    }
    handlePluginVisibleChanged(surfaceId, visible);
    updateVisualIndexes();
}

bool TraySortOrderModel::isDisplayedSurface(const QString &surfaceId) const
{
    return !m_hiddenIdSet.contains(surfaceId);
}

void TraySortOrderModel::setDockVisible(const QString &surfaceId, bool visible)
{
    if (visible) {
        if (m_dockHiddenIdSet.contains(surfaceId)) {
            m_dockHiddenIds.removeOne(surfaceId);
            m_dockHiddenIdSet.remove(surfaceId);
        }
    } else {
        if (!m_dockHiddenIdSet.contains(surfaceId)) {
            m_dockHiddenIds.append(surfaceId);
            m_dockHiddenIdSet.insert(surfaceId);
        }
    }
    updateVisualIndexes();
    saveDataToDConfig();
}

bool TraySortOrderModel::isDockVisible(const QString &surfaceId) const
{
    return !m_dockHiddenIdSet.contains(surfaceId);
}

QStandardItem *TraySortOrderModel::findItemByVisualIndex(int visualIndex, VisualSections visualSection) const
{
    QStandardItem * result = nullptr;
    const QModelIndexList matched = match(index(0, 0), VisualIndexRole, visualIndex, -1, Qt::MatchExactly);
    for (const QModelIndex & index : matched) {
        QString section(data(index, SectionTypeRole).toString());
        if (visualSection == DockTraySection) {
            if (section == SECTION_STASHED) continue;
            if (m_collapsed && section == SECTION_COLLAPSABLE) continue;
        } else {
            if (section != SECTION_STASHED) continue;
        }
        if (!data(index, VisibilityRole).toBool()) continue;

        result = itemFromIndex(index);
        break;
    }
    return result;
}

QStringList *TraySortOrderModel::getSection(const QString &sectionType)
{
    if (sectionType == SECTION_PINNED) {
        return &m_pinnedIds;
    } else if (sectionType == SECTION_COLLAPSABLE) {
        return &m_collapsableIds;
    } else if (sectionType == SECTION_STASHED) {
        return &m_stashedIds;
    } else if (sectionType == SECTION_FIXED) {
        return &m_fixedIds;
    }

    return nullptr;
}

QString TraySortOrderModel::findSection(const QString &surfaceId, const QString &fallback, const QStringList &forbiddenSections, int pluginFlags)
{
    QStringList * found = nullptr;
    QString result(fallback);

    if (m_pinnedIds.contains(surfaceId)) {
        found = &m_pinnedIds;
        result = SECTION_PINNED;
    } else if (m_collapsableIds.contains(surfaceId)) {
        found = &m_collapsableIds;
        result = SECTION_COLLAPSABLE;
    } else if (m_stashedIds.contains(surfaceId)) {
        found = &m_stashedIds;
        result = SECTION_STASHED;
    } else if (m_fixedIds.contains(surfaceId)) {
        found = &m_fixedIds;
        result = SECTION_FIXED;
    }

    // 设置默认隐藏
    if (!found && // 不在列表中
        !(pluginFlags & Dock::Attribute_ForceDock) && // 非 forceDock
        (pluginFlags & Dock::Attribute_CanSetting) && // 可以在控制中心设置
        result != SECTION_FIXED && // 非固定位置插件（时间）
        !surfaceId.startsWith("internal/") && // 非内置插件
        !surfaceId.startsWith("application-tray::") // 非托盘图标
    ) {
        if (!m_hiddenIdSet.contains(surfaceId)) {
            m_hiddenIds.append(surfaceId);
            m_hiddenIdSet.insert(surfaceId);
            handlePluginVisibleChanged(surfaceId, false);
        }
    }

    if (forbiddenSections.contains(result)) {
        Q_ASSERT(result != fallback);
        if (found) {
            found->removeOne(surfaceId);
            result = fallback;
        }
    }

    return result;
}

void TraySortOrderModel::registerToSection(const QString & surfaceId, const QString & sectionType)
{
    QStringList * section = getSection(sectionType);

    if (section == nullptr) {
        Q_ASSERT(sectionType == SECTION_TRAY_ACTION);
        return;
    }

    if (!section->contains(surfaceId)) {
        // 新项添加到末尾，保持已有项的位置顺序
        // 这样可以确保应用重启后，托盘图标按上次保存的顺序显示
        section->append(surfaceId);
    }
}

QStandardItem *TraySortOrderModel::createTrayItem(const QString &name,
                                                  const QString &sectionType,
                                                  const QString &delegateType,
                                                  const QStringList &forbiddenSections,
                                                  int pluginFlags)
{
    QString actualSectionType = findSection(name, sectionType, forbiddenSections, pluginFlags);
    registerToSection(name, actualSectionType);

    qDebug() << actualSectionType << name << delegateType;

    QStandardItem * item = new QStandardItem(name);
    item->setData(name, TraySortOrderModel::SurfaceIdRole);
    item->setData(true, TraySortOrderModel::VisibilityRole);
    item->setData(true, TraySortOrderModel::DockVisibleRole);
    item->setData(actualSectionType, TraySortOrderModel::SectionTypeRole);
    item->setData(delegateType, TraySortOrderModel::DelegateTypeRole);
    item->setData(forbiddenSections, TraySortOrderModel::ForbiddenSectionsRole);
    item->setData(-1, TraySortOrderModel::VisualIndexRole);
    item->setData(pluginFlags, TraySortOrderModel::PluginFlagsRole);

    return item;
}

void TraySortOrderModel::updateVisualIndexes()
{
    m_isUpdating = true;
    emit isUpdatingChanged(true);

    auto &positionManager = TrayItemPositionManager::instance();
    positionManager.beginLayoutSync();
    // Reset visual-index mapping before rebuilding it so reserved staged-drop slots
    // fall back to the default item size instead of keeping the previous occupant's size.
    positionManager.clearRegisteredSizes();

    QHash<QString, QStandardItem *> itemsBySurfaceId;
    itemsBySurfaceId.reserve(rowCount());
    for (int i = 0; i < rowCount(); ++i) {
        auto *trayItem = item(i);
        if (!trayItem) {
            continue;
        }
        itemsBySurfaceId.insert(trayItem->data(TraySortOrderModel::SurfaceIdRole).toString(), trayItem);
    }

    const auto itemBySurfaceId = [&itemsBySurfaceId](const QString &surfaceId) {
        return itemsBySurfaceId.value(surfaceId, nullptr);
    };

    const auto assignDockVisualIndex = [&positionManager](QStandardItem *trayItem, int visualIndex) {
        if (!trayItem || visualIndex < 0) {
            return;
        }

        trayItem->setData(visualIndex, TraySortOrderModel::VisualIndexRole);
        positionManager.registerVisualItem(trayItem->data(TraySortOrderModel::SurfaceIdRole).toString(), visualIndex);
    };

    const auto surfaceAvailable = [this](const QString &surfaceId) {
        return surfaceId.startsWith(QLatin1String("internal/")) || m_availableSurfaceIdSet.contains(surfaceId);
    };
    
    for (int i = 0; i < rowCount(); i++) {
        item(i)->setData(-1, TraySortOrderModel::VisualIndexRole);
    }

    // stashed action
    // "internal/action-stash-placeholder"
    QStandardItem * stashPlaceholder = itemBySurfaceId(QStringLiteral("internal/action-stash-placeholder"));
    Q_ASSERT(stashPlaceholder);

    // the visual index of stashed items are also for their sort order, but the index
    // number is independently from these non-stashed items.
    int stashedVisualIndex = 0;
    bool showStashActionVisible = m_actionsAlwaysVisible;
    for (const QString & id : std::as_const(m_stashedIds)) {
        QStandardItem *trayItem = itemBySurfaceId(id);
        if (!trayItem) continue;
        if (trayItem->data(TraySortOrderModel::VisualIndexRole).toInt() != -1) continue;
        if (stashPlaceholder == trayItem) continue;
        // forcedock and can not setting plugin need always set to visible
        auto pluginFlags = trayItem->data(TraySortOrderModel::PluginFlagsRole).toInt();
        bool itemVisible = (pluginFlags & Dock::Attribute_ForceDock) || !(pluginFlags & Dock::Attribute_CanSetting) || !m_hiddenIdSet.contains(id);
        bool dockVisible = !m_dockHiddenIdSet.contains(id) && surfaceAvailable(id);
        trayItem->setData(SECTION_STASHED, TraySortOrderModel::SectionTypeRole);
        trayItem->setData(itemVisible, TraySortOrderModel::VisibilityRole);
        trayItem->setData(dockVisible, TraySortOrderModel::DockVisibleRole);
        if (itemVisible && dockVisible) {
            showStashActionVisible = true;
            trayItem->setData(stashedVisualIndex, TraySortOrderModel::VisualIndexRole);
            stashedVisualIndex++;
        }
    }

    stashPlaceholder->setData(stashedVisualIndex == 0 && showStashActionVisible, TraySortOrderModel::VisibilityRole);

    int currentVisualIndex = 0;
    // "internal/action-show-stash"
    QStandardItem *showStashAction = itemBySurfaceId(QStringLiteral("internal/action-show-stash"));
    Q_ASSERT(showStashAction);
    showStashAction->setData(showStashActionVisible, TraySortOrderModel::VisibilityRole);
    if (showStashActionVisible) {
        assignDockVisualIndex(showStashAction, currentVisualIndex);
        currentVisualIndex++;
    }

    // collapsable
    bool toogleCollapseActionVisible = m_actionsAlwaysVisible;
    for (const QString & id : std::as_const(m_collapsableIds)) {
        QStandardItem *trayItem = itemBySurfaceId(id);
        if (!trayItem) continue;
        if (trayItem->data(TraySortOrderModel::VisualIndexRole).toInt() != -1) continue;
        auto pluginFlags = trayItem->data(TraySortOrderModel::PluginFlagsRole).toInt();
        bool itemVisible = (pluginFlags & Dock::Attribute_ForceDock) || !(pluginFlags & Dock::Attribute_CanSetting) || !m_hiddenIdSet.contains(id);
        bool dockVisible = !m_dockHiddenIdSet.contains(id) && surfaceAvailable(id);
        trayItem->setData(SECTION_COLLAPSABLE, TraySortOrderModel::SectionTypeRole);
        trayItem->setData(itemVisible, TraySortOrderModel::VisibilityRole);
        trayItem->setData(dockVisible, TraySortOrderModel::DockVisibleRole);
        if (itemVisible && dockVisible) {
            toogleCollapseActionVisible = true;
            if (!m_collapsed) {
                reserveStagedDropSpace(currentVisualIndex);
                assignDockVisualIndex(trayItem, currentVisualIndex);
                currentVisualIndex++;
            } else {
                // When collapsed, collapsable items should be hidden (visualIndex = -1)
                trayItem->setData(-1, TraySortOrderModel::VisualIndexRole);
            }
        }
    }

    // "internal/action-toggle-collapse"
    QStandardItem *toggleCollapseAction = itemBySurfaceId(QStringLiteral("internal/action-toggle-collapse"));
    Q_ASSERT(toggleCollapseAction);
    toggleCollapseAction->setData(toogleCollapseActionVisible, TraySortOrderModel::VisibilityRole);
    if (toogleCollapseActionVisible) {
        reserveStagedDropSpace(currentVisualIndex);
        assignDockVisualIndex(toggleCollapseAction, currentVisualIndex);
        currentVisualIndex++;
    }

    // pinned
    for (const QString & id : std::as_const(m_pinnedIds)) {
        QStandardItem *trayItem = itemBySurfaceId(id);
        if (!trayItem) continue;
        if (trayItem->data(TraySortOrderModel::VisualIndexRole).toInt() != -1) continue;
        auto flags = trayItem->data(TraySortOrderModel::PluginFlagsRole).toInt();
        bool itemVisible = (flags & Dock::Attribute_ForceDock) || !(flags & Dock::Attribute_CanSetting) || !m_hiddenIdSet.contains(id);
        bool dockVisible = !m_dockHiddenIdSet.contains(id) && surfaceAvailable(id);
        trayItem->setData(SECTION_PINNED, TraySortOrderModel::SectionTypeRole);
        trayItem->setData(itemVisible, TraySortOrderModel::VisibilityRole);
        trayItem->setData(dockVisible, TraySortOrderModel::DockVisibleRole);
        if (itemVisible && dockVisible) {
            reserveStagedDropSpace(currentVisualIndex);
            assignDockVisualIndex(trayItem, currentVisualIndex);
            currentVisualIndex++;
        }
    }

    // "internal/action-toggle-quick-settings"
    QStandardItem *toggleQuickSettingsAction = itemBySurfaceId(QStringLiteral("internal/action-toggle-quick-settings"));
    Q_ASSERT(toggleQuickSettingsAction);
    toggleQuickSettingsAction->setData(SECTION_FIXED, TraySortOrderModel::SectionTypeRole);
    reserveStagedDropSpace(currentVisualIndex);
    assignDockVisualIndex(toggleQuickSettingsAction, currentVisualIndex);
    currentVisualIndex++;

    // fixed (not actually 'fixed' since it's just a section next to pinned)
    // By design, fixed items can be rearranged within the fixed section, but cannot
    // move to other sections. We archive that by setting the 'forbiddenSections' property
    // to the items in fixed sections.
    for (const QString & id : std::as_const(m_fixedIds)) {
        QStandardItem *trayItem = itemBySurfaceId(id);
        if (!trayItem) continue;
        if (trayItem->data(TraySortOrderModel::VisualIndexRole).toInt() != -1) continue;
        auto flags = trayItem->data(TraySortOrderModel::PluginFlagsRole).toInt();
        bool itemVisible = (flags & Dock::Attribute_ForceDock) || !(flags & Dock::Attribute_CanSetting) || !m_hiddenIdSet.contains(id);
        bool dockVisible = !m_dockHiddenIdSet.contains(id) && surfaceAvailable(id);
        trayItem->setData(SECTION_FIXED, TraySortOrderModel::SectionTypeRole);
        trayItem->setData(itemVisible, TraySortOrderModel::VisibilityRole);
        trayItem->setData(dockVisible, TraySortOrderModel::DockVisibleRole);
        if (itemVisible && dockVisible) {
            assignDockVisualIndex(trayItem, currentVisualIndex);
            currentVisualIndex++;
        }
    }

    // update visible item count property
    setProperty("visualItemCount", currentVisualIndex);
    positionManager.endLayoutSync();
    
    m_isUpdating = false;
    emit isUpdatingChanged(false);

    qDebug() << "update" << m_visualItemCount << currentVisualIndex;
}

QString TraySortOrderModel::registerSurfaceId(const QVariantMap & surfaceData)
{
    QString surfaceId(surfaceData.value("surfaceId").toString());
    QString delegateType(surfaceData.value("delegateType", "legacy-tray-plugin").toString());
    QString preferredSection(surfaceData.value("sectionType", "collapsable").toString());
    QStringList forbiddenSections(surfaceData.value("forbiddenSections").toStringList());
    int pluginFlas(surfaceData.value("pluginFlags").toInt());

    QList<QStandardItem *> results = findItems(surfaceId);
    if (!results.isEmpty()) {
        QStandardItem * result = results[0];
        // check if the item is currently in a forbidden zone
        QString currentSection(result->data(SectionTypeRole).toString());
        if (forbiddenSections.contains(currentSection)) {
            result->setData(findSection(surfaceId, preferredSection, forbiddenSections, pluginFlas), SectionTypeRole);
        }
        return surfaceId;
    }

    appendRow(createTrayItem(surfaceId, preferredSection, delegateType, forbiddenSections, pluginFlas));
    return surfaceId;
}

void TraySortOrderModel::loadDataFromDConfig()
{
    m_stashedIds = m_dconfig->value("stashedSurfaceIds").toStringList();
    m_collapsableIds = m_dconfig->value("collapsableSurfaceIds").toStringList();
    m_pinnedIds = m_dconfig->value("pinnedSurfaceIds").toStringList();
    m_hiddenIds = m_dconfig->value("hiddenSurfaceIds").toStringList();
    m_dockHiddenIds = m_dconfig->value("dockHiddenSurfaceIds").toStringList();
    m_hiddenIdSet = QSet<QString>(m_hiddenIds.cbegin(), m_hiddenIds.cend());
    m_dockHiddenIdSet = QSet<QString>(m_dockHiddenIds.cbegin(), m_dockHiddenIds.cend());
    m_collapsed = m_dconfig->value("isCollapsed").toBool();
}

void TraySortOrderModel::saveDataToDConfig()
{
    m_dconfig->setValue("stashedSurfaceIds", m_stashedIds);
    m_dconfig->setValue("collapsableSurfaceIds", m_collapsableIds);
    m_dconfig->setValue("pinnedSurfaceIds", m_pinnedIds);
    m_dconfig->setValue("hiddenSurfaceIds", m_hiddenIds);
    m_dconfig->setValue("dockHiddenSurfaceIds", m_dockHiddenIds);
    m_dconfig->setValue("isCollapsed", m_collapsed);
}

void TraySortOrderModel::onAvailableSurfacesChanged()
{
    m_availableSurfaceIdSet.clear();
    for (const QVariantMap &surface : std::as_const(m_availableSurfaces)) {
        const QString surfaceId = surface.value(QStringLiteral("surfaceId")).toString();
        if (!surfaceId.isEmpty()) {
            m_availableSurfaceIdSet.insert(surfaceId);
        }
    }

    if (m_availableSurfaces.isEmpty()) {
        updateVisualIndexes();
        return;
    }

    // Register newly available tray items incrementally. The compositor can emit
    // empty or partial surface lists while tray plugins are restarting, so this
    // model must not treat one update as a complete snapshot and remove rows.
    for (const QVariantMap & surface : m_availableSurfaces) {
        registerSurfaceId(surface);
    }

    // finally, update visual index
    updateVisualIndexes();
    // and also save the current sort order
    saveDataToDConfig();
}

void TraySortOrderModel::handlePluginVisibleChanged(const QString &surfaceId, bool visible)
{
    QStringList parts = surfaceId.split("::");
    if (parts.size() != 2 || parts.at(1).isEmpty()) {
        qWarning() << "Invalid surfaceId format:" << surfaceId;
        return;
    }

    QDBusMessage msg = QDBusMessage::createMethodCall(
        "org.deepin.dde.Dock1",
        "/org/deepin/dde/Dock1",
        "org.deepin.dde.Dock1",
        "setItemOnDock"
    );
    
    const QString DockQuickPlugins = "Dock_Quick_Plugins";
    msg << DockQuickPlugins
        << parts.last()
        << visible;
    
    QDBusMessage reply = QDBusConnection::sessionBus().call(msg);
    
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << "DBus call failed:" << reply.errorMessage();
    } else {
        qDebug() << "setItemOnDock call success";
    }
}

QModelIndex TraySortOrderModel::getModelIndexByVisualIndex(int visualIndex) const
{
    for (int i = 0; i < rowCount(); i++) {
        QModelIndex index = this->index(i, 0);
        int itemVisualIndex = data(index, VisualIndexRole).toInt();
        bool visibility = data(index, VisibilityRole).toBool();
        
        if (visibility && itemVisualIndex == visualIndex) {
            return index;
        }
    }
    return QModelIndex();
}

void TraySortOrderModel::reserveStagedDropSpace(int &currentVisualIndex)
{
    if (!m_stagedSurfaceId.isEmpty() && currentVisualIndex == m_stagedVisualIndex) {
        currentVisualIndex++;
    }
}

void TraySortOrderModel::stageDropPosition(const QString &surfaceId, int visualIndex)
{
    if (m_stagedSurfaceId == surfaceId && m_stagedVisualIndex == visualIndex) {
        return;
    }
    
    m_stagedSurfaceId = surfaceId;
    m_stagedVisualIndex = visualIndex;
    emit stagedDropChanged();
    
    // Update visual indexes to show preview position
    updateVisualIndexes();
}

void TraySortOrderModel::commitStagedDrop()
{
    if (m_stagedSurfaceId.isEmpty() || m_stagedVisualIndex < 0) {
        return;
    }
    
    // Save staged values before clearing
    QString surfaceId = m_stagedSurfaceId;
    int visualIndex = m_stagedVisualIndex;
    
    m_stagedSurfaceId.clear();
    m_stagedVisualIndex = -1;
    emit stagedDropChanged();
    
    updateVisualIndexes();
    dropToDockTray(surfaceId, visualIndex, true);
}

void TraySortOrderModel::clearStagedDrop()
{
    // Avoid redundant work if there is no staged drop
    if (m_stagedSurfaceId.isEmpty() && m_stagedVisualIndex < 0) {
        return;
    }
    
    m_stagedSurfaceId.clear();
    m_stagedVisualIndex = -1;
    emit stagedDropChanged();
    
    // Update visual indexes to remove preview
    updateVisualIndexes();
}

}
