// SPDX-FileCopyrightText: 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick 2.15
import QtQuick.Controls 2.15
import org.deepin.ds 1.0
import org.deepin.ds.dock 1.0
import org.deepin.dtk 1.0 as D

D.SortFilterModel {
    id: model

    property int leftDockOrder: 0
    property int rightDockOrder: 0
    property var acceptItem: null
    property var sortOrderProvider: null
    property var sourceAppletModel: Applet.appletItems

    model: sourceAppletModel

    function scheduleUpdate() {
        updateTimer.restart()
    }

    Timer {
        id: updateTimer
        interval: 0
        repeat: false
        onTriggered: model.update()
    }

    Connections {
        target: model.sourceAppletModel
        ignoreUnknownSignals: true

        function onRowsInserted() {
            model.scheduleUpdate()
        }

        function onRowsRemoved() {
            model.scheduleUpdate()
        }

        function onModelReset() {
            model.scheduleUpdate()
        }

        function onLayoutChanged() {
            model.scheduleUpdate()
        }
    }

    filterAcceptsItem: function(item) {
        if (acceptItem) {
            return acceptItem(item)
        }
        return item.data.dockOrder > leftDockOrder && item.data.dockOrder <= rightDockOrder && (item.data.shouldVisible === undefined || item.data.shouldVisible)
    }
    lessThan: function(leftItem, rightItem) {
        const leftOrder = sortOrderProvider ? sortOrderProvider(leftItem) : parseInt(leftItem.data.dockOrder)
        const rightOrder = sortOrderProvider ? sortOrderProvider(rightItem) : parseInt(rightItem.data.dockOrder)
        if (leftOrder !== rightOrder) {
            return leftOrder < rightOrder
        }

        const leftDockOrder = parseInt(leftItem.data.dockOrder)
        const rightDockOrder = parseInt(rightItem.data.dockOrder)
        if (leftDockOrder !== rightDockOrder) {
            return leftDockOrder < rightDockOrder
        }

        const leftPluginId = leftItem.data && leftItem.data.applet ? leftItem.data.applet.pluginId : ""
        const rightPluginId = rightItem.data && rightItem.data.applet ? rightItem.data.applet.pluginId : ""
        return leftPluginId < rightPluginId
    }
    delegate: Control {
        id: delegateRoot

        property var appletItem: model.data
        property var attachedAppletItem: null
        readonly property string pluginId: appletItem && appletItem.applet ? appletItem.applet.pluginId : ""
        readonly property bool isAiBarApplet: pluginId === "org.deepin.ds.dock.aibar"
        readonly property bool horizontalFashionMode: Panel.rootObject
            ? Panel.viewMode === Dock.FashionMode && !Panel.rootObject.useColumnLayout
            : false
        readonly property bool fixedFashionEntryIcon: horizontalFashionMode && delegateRoot.isFixedFashionEntryIcon(pluginId)
        readonly property bool horizontalAiBarApplet: isAiBarApplet
            && Panel.viewMode === Dock.FashionMode
            && !(Panel.rootObject && Panel.rootObject.useColumnLayout)
        readonly property int hoverInset: 4
        readonly property bool useUnifiedDockHoverBackground: [
            "org.deepin.ds.dock.launcherapplet",
            "org.deepin.ds.dock.aibar",
            "org.deepin.ds.dock.searchitem",
            "org.deepin.ds.dock.multitaskview"
        ].indexOf(pluginId) >= 0
        readonly property real aiBarIconSize: appletItem && appletItem.iconWidth !== undefined ? appletItem.iconWidth : 0
        readonly property real hoverTargetWidth: isAiBarApplet && aiBarIconSize > 0
            ? aiBarIconSize
            : (Panel.rootObject ? Panel.rootObject.dockItemMaxSize * 9 / 14 : 0)
        readonly property real hoverTargetHeight: hoverTargetWidth
        readonly property real unifiedHoverBackgroundWidth: Math.round(hoverTargetWidth + hoverInset * 2)
        readonly property real unifiedHoverBackgroundHeight: Math.round(hoverTargetHeight + hoverInset * 2)
        readonly property real aiBarEdgeMargin: Panel.rootObject
            ? Math.max(0, (Panel.rootObject.dockSize - hoverTargetHeight) / 2)
            : 0
        readonly property real aiBarTrailingInset: Panel.rootObject ? Panel.rootObject.fashionVerticalPadding + 1 : 0
        readonly property real aiBarContentWidth: appletItem ? appletItem.implicitWidth : 0
        readonly property real aiBarLayoutWidth: horizontalAiBarApplet
            ? Math.max(aiBarContentWidth, hoverTargetWidth + aiBarEdgeMargin * 2 - aiBarTrailingInset)
            : aiBarContentWidth
        readonly property int aiBarHorizontalOffset: horizontalAiBarApplet
            ? Math.round(aiBarEdgeMargin)
            : 0
        readonly property real aiBarRightPadding: horizontalAiBarApplet
            ? Math.max(0, aiBarLayoutWidth - aiBarContentWidth)
            : 0
        property real fixedFashionHoverLiftProgress: fixedFashionEntryIcon && delegateHoverHandler.hovered ? 1.0 : 0.0
        readonly property real fixedFashionHoverLiftSize: 4
        readonly property real fixedFashionHoverSizeDelta: fixedFashionHoverLiftSize * fixedFashionHoverLiftProgress
        readonly property real fixedFashionHoverScale: hoverTargetWidth > 0
            ? 1.0 + fixedFashionHoverSizeDelta / hoverTargetWidth
            : 1.0
        readonly property real fixedFashionHoverTranslateY: -fixedFashionHoverSizeDelta / 2
        readonly property bool fixedFashionHoverActive: fixedFashionEntryIcon
            && (delegateHoverHandler.hovered || fixedFashionHoverLiftProgress > 0)
        implicitWidth: appletItem ? aiBarLayoutWidth : 0
        implicitHeight: appletItem ? appletItem.implicitHeight : 0
        rightPadding: aiBarRightPadding

        Binding on width {
            value: Math.max(1, delegateRoot.implicitWidth)
        }

        Binding on height {
            value: Math.max(1, delegateRoot.implicitHeight)
        }

        transform: Translate {
            x: delegateRoot.aiBarHorizontalOffset
        }

        contentItem: Item {
            id: appletHost

            implicitWidth: delegateRoot.appletItem ? delegateRoot.appletItem.implicitWidth : 0
            implicitHeight: delegateRoot.appletItem ? delegateRoot.appletItem.implicitHeight : 0
            scale: delegateRoot.fixedFashionHoverScale
            transformOrigin: Item.Center
            transform: Translate {
                y: delegateRoot.fixedFashionHoverTranslateY
            }
        }
        background: AppletItemBackground {
            x: Math.round(!delegateRoot.isAiBarApplet || (Panel.rootObject && Panel.rootObject.useColumnLayout)
                          ? (delegateRoot.width - width) / 2
                          : delegateRoot.hoverInset * -1)
            y: Math.round(!delegateRoot.isAiBarApplet || !(Panel.rootObject && Panel.rootObject.useColumnLayout)
                          ? (delegateRoot.height - height) / 2
                          : delegateRoot.hoverInset * -1)
            width: delegateRoot.unifiedHoverBackgroundWidth + delegateRoot.fixedFashionHoverSizeDelta
            height: delegateRoot.unifiedHoverBackgroundHeight + delegateRoot.fixedFashionHoverSizeDelta
            radius: height / 5
            transform: Translate {
                y: delegateRoot.fixedFashionHoverTranslateY
            }
            enabled: false
            visible: delegateRoot.useUnifiedDockHoverBackground
            opacity: delegateHoverHandler.hovered ? 1 : 0
            D.ColorSelector.hovered: delegateHoverHandler.hovered

            Behavior on opacity {
                NumberAnimation { duration: 150 }
            }
        }

        HoverHandler {
            id: delegateHoverHandler
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
            enabled: delegateRoot.useUnifiedDockHoverBackground && delegateRoot.visible
        }

        Timer {
            id: syncAppletItemTimer
            interval: 0
            repeat: false
            onTriggered: delegateRoot.syncAppletItem()
        }

        Behavior on fixedFashionHoverLiftProgress {
            NumberAnimation {
                duration: delegateHoverHandler.hovered ? 24 : 96
                alwaysRunToEnd: false
                easing.type: Easing.OutCubic
            }
        }

        function isFixedFashionEntryIcon(pluginId) {
            switch (pluginId) {
            case "org.deepin.ds.dock.launcherapplet":
            case "org.deepin.ds.dock.searchitem":
            case "org.deepin.ds.dock.multitaskview":
                return true
            default:
                return false
            }
        }

        function scheduleSyncAppletItem() {
            if (appletItem) {
                syncAppletItemTimer.restart()
            }
        }

        function syncAppletItem() {
            if (attachedAppletItem && attachedAppletItem !== appletItem && attachedAppletItem.parent === appletHost) {
                attachedAppletItem.parent = null
            }

            if (appletItem) {
                const item = appletItem
                item.parent = appletHost
                item.width = Qt.binding(function() { return Math.max(item.implicitWidth, appletHost.width) })
                item.height = Qt.binding(function() { return Math.max(item.implicitHeight, appletHost.height) })
                item.visible = Qt.binding(function() {
                    return item.shouldVisible === undefined || item.shouldVisible
                })
                if ("dockHoverMagnifyActive" in item) {
                    item.dockHoverMagnifyActive = Qt.binding(function() {
                        return delegateRoot.fixedFashionHoverActive
                    })
                }
                attachedAppletItem = item
            } else {
                attachedAppletItem = null
            }
        }

        onAppletItemChanged: {
            syncAppletItem()
            scheduleSyncAppletItem()
        }
        onVisibleChanged: scheduleSyncAppletItem()
        onWindowChanged: scheduleSyncAppletItem()
        onParentChanged: scheduleSyncAppletItem()

        Component.onCompleted: syncAppletItem()

        Component.onDestruction: {
            if (attachedAppletItem && attachedAppletItem.parent === appletHost) {
                attachedAppletItem.parent = null
            }
            attachedAppletItem = null
        }

        Connections {
            target: Panel

            function onHideStateChanged() {
                if (Panel.hideState !== Dock.Hide) {
                    delegateRoot.scheduleSyncAppletItem()
                }
            }
        }
    }
}
