// SPDX-FileCopyrightText: 2024 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import Qt5Compat.GraphicalEffects
import org.deepin.ds 1.0
import org.deepin.ds.dock 1.0
import org.deepin.dtk

IconButton {
    id: control
    property bool isActive
    property real radius: 4
    property point lastSpotlightPoint: Qt.point(0, 0)
    property bool autoClosePopup: false
    property bool dockHoverMagnifyActive: false
    property real iconPressBrightness: pressed ? -0.16 : 0.0
    property real iconPressScale: pressed ? 0.9 : 1.0
    readonly property bool drivesDockSpotlight: Window.window === Panel.rootObject

    padding: 4
    topPadding: undefined
    bottomPadding: undefined
    leftPadding: undefined
    rightPadding: undefined

    textColor: DockPalette.iconTextPalette
    display: IconLabel.IconOnly

    icon.width: 16
    icon.height: 16

    function mapSpotlightPoint(localPoint) {
        if (!drivesDockSpotlight) {
            return Qt.point(0, 0)
        }

        const point = localPoint || Qt.point(width / 2, height / 2)
        return mapToItem(null, point.x, point.y)
    }

    function updateSpotlight(localPoint) {
        if (!drivesDockSpotlight) {
            lastSpotlightPoint = Qt.point(0, 0)
            Panel.reportMousePresence(false)
            return
        }

        lastSpotlightPoint = mapSpotlightPoint(localPoint)
        Panel.reportMousePresence(true, lastSpotlightPoint)
    }

    function clearSpotlight() {
        if (!drivesDockSpotlight) {
            Panel.reportMousePresence(false)
            return
        }

        Panel.reportMousePresence(false, lastSpotlightPoint)
    }

    Connections {
        target: control
        enabled: autoClosePopup
        function onClicked() {
            Panel.requestClosePopup()
        }
    }

    background: AppletItemBackground {
        radius: control.radius
        isActive: control.isActive
    }

    Binding {
        target: control.contentItem
        property: "smooth"
        value: control.dockHoverMagnifyActive
        restoreMode: Binding.RestoreNone
    }

    Binding {
        target: control.contentItem
        property: "scale"
        value: control.iconPressScale
        restoreMode: Binding.RestoreNone
    }

    Binding {
        target: control.contentItem
        property: "transformOrigin"
        value: Item.Center
        restoreMode: Binding.RestoreNone
    }

    Binding {
        target: control.contentItem
        property: "layer.enabled"
        value: true
        restoreMode: Binding.RestoreNone
    }

    Binding {
        target: control.contentItem
        property: "layer.effect"
        value: iconPressEffect
        restoreMode: Binding.RestoreNone
    }

    Binding {
        target: control.contentItem ? control.contentItem.layer : null
        property: "smooth"
        value: control.dockHoverMagnifyActive
        restoreMode: Binding.RestoreNone
    }

    BrightnessContrast {
        id: iconPressEffect
        brightness: control.iconPressBrightness
        contrast: 0
    }

    Behavior on iconPressBrightness {
        NumberAnimation {
            duration: 80
            easing.type: Easing.OutQuad
        }
    }

    Behavior on iconPressScale {
        NumberAnimation {
            duration: 64
            alwaysRunToEnd: false
            easing.type: Easing.OutCubic
        }
    }

    HoverHandler {
        id: spotlightHoverHandler
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
        enabled: control.enabled && control.visible && control.hoverEnabled

        onPointChanged: {
            if (hovered) {
                spotlightClearTimer.stop()
                control.updateSpotlight(spotlightHoverHandler.point.position)
            }
        }

        onHoveredChanged: {
            if (hovered) {
                spotlightClearTimer.stop()
                control.updateSpotlight()
                return
            }

            spotlightClearTimer.restart()
        }
    }

    Timer {
        id: spotlightClearTimer
        interval: 70
        repeat: false
        onTriggered: {
            if (!spotlightHoverHandler.hovered) {
                control.clearSpotlight()
            }
        }
    }
}
