// SPDX-FileCopyrightText: 2024 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtWayland.Compositor
import org.deepin.ds.dock 1.0
import org.deepin.ds 1.0

Item {
    id: root
    property var shellSurface: null
    signal surfaceDestroyed()
    property bool autoClose: false
    property bool inputEventsEnabled: true
    property bool hovered: hoverHandler.hovered
    property bool pressed: tapHandler.pressed
    property int cursorShape: Qt.ArrowCursor
    property Item shellSurfaceItem: surfaceLoader.item ? surfaceLoader.item : fallbackSurfaceItem
    property bool surfaceItemEnabled: true
    
    implicitWidth: shellSurface ? shellSurface.width : 10
    implicitHeight: shellSurface ? shellSurface.height : 10

    function takeFocus() {
        if (surfaceLoader.item) {
            surfaceLoader.item.takeFocus()
        }
    }

    function fixPosition() {
        fixPositionTimer.start()
    }

    Item {
        id: fallbackSurfaceItem
        anchors.fill: parent
        visible: false
    }

    Loader {
        id: surfaceLoader
        anchors.fill: parent
        active: root.surfaceItemEnabled && root.shellSurface
        sourceComponent: shellSurfaceComponent

        onLoaded: {
            if (!item) {
                return
            }

            item.surfaceDestroyed.connect(root.surfaceDestroyed)
            item.fixPosition()
        }
    }

    Component {
        id: shellSurfaceComponent

        ShellSurfaceItem {
            width: root.width
            height: root.height
            shellSurface: root.shellSurface
            inputEventsEnabled: root.inputEventsEnabled
            // we need to set smooth to false, otherwise the image
            // will be blurred if the scale is 1.25.
            // If the surface width is 150, the buffer width will be 150 * 1.25 = 188
            // But the ShellSurfaceItem pixel width on screen is 150 * 1.25 = 187.5
            // So Qt will use the 188 to scale to 187.5, which will be blurred.
            // But if we set smooth to false, the Qt doesn't linear interpolation.
            // TODO: If the buffer size greater than the ShellSurfaceItem pixel
            // size, we also need enable smooth.
            smooth: false

            onVisibleChanged: function () {
                if (visible) {
                    fixPositionTimer.start()
                }

                if (root.autoClose && !visible) {
                    // surface is valid but client's shellSurface maybe invalid.
                    Qt.callLater(closeShellSurface)
                }
            }
            function closeShellSurface()
            {
                if (surface && shellSurface) {
                    DockCompositor.closeShellSurface(shellSurface)
                }
            }

            function mapToScene(x, y) {
                if (!parent || !Window.window || !Window.window.contentItem) {
                    return Qt.point(x, y)
                }

                const point = Qt.point(x, y)
                // Must use parent.mapFoo, because the item's position is relative to the parent Item
                const mappedPoint = parent.mapToItem(Window.window.contentItem, point)
                return mappedPoint
            }

            function mapFromScene(x, y) {
                if (!parent || !Window.window || !Window.window.contentItem) {
                    return Qt.point(x, y)
                }

                const point = Qt.point(x, y)
                // Must use parent.mapFoo, because the item's position is relative to the parent Item
                const mappedPoint = parent.mapFromItem(Window.window.contentItem, point)
                return mappedPoint
            }

            function fixPosition() {
                if (!parent || !Window.window || !Window.window.contentItem) {
                    return
                }

                // See QTBUG: https://bugreports.qt.io/browse/QTBUG-135833
                // Snap to the nearest physical pixel to avoid one-pixel seams between
                // QML-painted items and shell-surface items in the dock.
                const scenePoint = mapToScene(0, 0)
                const snappedX = Math.round(scenePoint.x * Panel.devicePixelRatio) / Panel.devicePixelRatio
                const snappedY = Math.round(scenePoint.y * Panel.devicePixelRatio) / Panel.devicePixelRatio
                x = mapFromScene(snappedX, scenePoint.y).x
                y = mapFromScene(scenePoint.x, snappedY).y
            }
        }
    }

    HoverHandler {
        id: hoverHandler
        parent: root.shellSurfaceItem
        cursorShape: root.cursorShape
    }
    TapHandler {
        id: tapHandler
        parent: root.shellSurfaceItem
    }

    Timer {
        id: fixPositionTimer
        interval: 100
        repeat: false
        running: false
        onTriggered: {
            if (surfaceLoader.item) {
                surfaceLoader.item.fixPosition()
            }
        }
    }

    Connections {
        target: shellSurface
        ignoreUnknownSignals: true
        // TODO it's maybe a bug for qt, we force shellSurface's value to update
        function onAboutToDestroy()
        {
            root.surfaceItemEnabled = false
            Qt.callLater(function() {
                root.surfaceItemEnabled = true
            })
        }

        function onCursorShapeRequested(cursorShape)
        {
            console.log("onCursorShapeRequested:", cursorShape)
            // Qt::CursorShape range is 0-21, plus 24 (BitmapCursor) and 25 (CustomCursor).
            // We set a default if the value is out of logical bounds.
            if (cursorShape < 0 || cursorShape > 25) {
                root.cursorShape = Qt.ArrowCursor
            } else {
                root.cursorShape = cursorShape
            }
        }
    }
}
