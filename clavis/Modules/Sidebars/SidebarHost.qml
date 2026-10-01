pragma ComponentBehavior: Bound

import QtQuick
import Quickshell
import qs.Common
import qs.Services

Scope {
    id: root

    property var activeScreen: null
    property var pendingScreen: null
    property string activeScreenName: ""
    readonly property SidebarHostWindow hostWindow: hostLoader.item as SidebarHostWindow

    function requestScreen(name) {
        const nextScreen = Brightness.getScreenByName(name) || Brightness.activeScreen;
        if (!nextScreen || nextScreen === activeScreen)
            return;
        pendingScreen = nextScreen;
        replaceWindow.restart();
    }

    function toggleSidebar(target) {
        return root.hostWindow ? root.hostWindow.toggleSidebar(target) : "UNAVAILABLE";
    }

    function setSidebarOpen(target, open) {
        return root.hostWindow ? root.hostWindow.setSidebarOpen(target, open) : "UNAVAILABLE";
    }

    function openOnScreen(role, view, screenName) {
        return root.hostWindow ? root.hostWindow.openOnScreen(role, view, screenName) : "UNAVAILABLE";
    }

    Component.onCompleted: root.requestScreen(WidgetState.sidebarScreenName)

    Connections {
        target: Quickshell

        function onScreensChanged() {
            root.requestScreen(root.activeScreenName);
        }
    }

    Timer {
        id: replaceWindow

        interval: 0
        onTriggered: {
            // Quickshell 0.3.1 loses BackgroundEffect after a PanelWindow moves
            // between outputs. Recreate only this host on the requested screen;
            // WidgetState retains the open panels and their selected views.
            hostLoader.active = false;
            root.activeScreen = root.pendingScreen;
            root.activeScreenName = root.activeScreen.name;
            hostLoader.active = true;
        }
    }

    Loader {
        id: hostLoader

        active: false
        sourceComponent: SidebarHostWindow {
            initialScreen: root.activeScreen
            screen: root.activeScreen
            onRetainedScreenNameChanged: root.requestScreen(retainedScreenName)
        }
    }
}
