pragma Singleton

import QtQuick

// 页面级轻提示：页面只发消息，由 Main.qml 统一渲染 Toast。
QtObject {
    signal info(string text)
}
