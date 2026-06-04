// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick 2.15
import org.deepin.ds 1.0
import org.deepin.dtk 1.0 as D

Item {
    id: root

    property string iconName: ""
    property bool smooth: false
    property bool retainWhileLoading: true
    readonly property real sourceImageDevicePixelRatio: Panel.devicePixelRatio > 0
                                                     ? Panel.devicePixelRatio
                                                     : (Screen.devicePixelRatio > 0 ? Screen.devicePixelRatio : 1.0)
    readonly property bool useSourceImage: iconName.indexOf("data:") === 0
                                           || iconName.indexOf("file:") === 0
                                           || iconName.indexOf("qrc:") === 0
                                           || iconName.indexOf("image:") === 0
                                           || iconName.indexOf("http:") === 0
                                           || iconName.indexOf("https:") === 0
                                           || iconName.indexOf(":/") === 0
                                           || iconName.indexOf("/") === 0

    D.DciIcon {
        anchors.fill: parent
        sourceSize: Qt.size(Math.max(1, Math.round(root.width)),
                            Math.max(1, Math.round(root.height)))
        name: root.useSourceImage ? "" : root.iconName
        smooth: root.smooth
        retainWhileLoading: root.retainWhileLoading
        visible: root.iconName.length > 0 && !root.useSourceImage
    }

    Image {
        anchors.fill: parent
        source: root.useSourceImage ? root.iconName : ""
        sourceSize: Qt.size(Math.max(1, Math.round(width * root.sourceImageDevicePixelRatio)),
                            Math.max(1, Math.round(height * root.sourceImageDevicePixelRatio)))
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        smooth: true
        visible: root.iconName.length > 0 && root.useSourceImage
    }
}
