// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.deepin.ds.dock.tray 1.0 as DDT

Control {
    id: root
    property bool itemVisible: true
    visible: itemVisible || opacity > 0.01

    spacing: 0
    padding: 0
    x: (index % columnCount) * (16 + itemPadding * 2 + itemSpacing)
    y: Math.floor(index / columnCount) * (16 + itemPadding * 2 + itemSpacing)
    Behavior on x {
        NumberAnimation { duration: 200; easing.type: Easing.OutQuad }
    }
    Behavior on y {
        NumberAnimation { duration: 200; easing.type: Easing.OutQuad }
    }
    states: [
        State {
            when: root.itemVisible
            PropertyChanges { target: root; opacity: 1.0 }
        },
        State {
            name: "item-invisible"
            when: !root.itemVisible
            PropertyChanges { target: root; opacity: 0.0 }
        }
    ]
    transitions: [
        Transition {
            to: "item-invisible"
            SequentialAnimation {
                NumberAnimation { property: "opacity"; easing.type: Easing.InQuad; duration: 200 }
            }
        },
        Transition {
            NumberAnimation { property: "opacity"; easing.type: Easing.OutQuad; duration: 200 }
        }
    ]
}
