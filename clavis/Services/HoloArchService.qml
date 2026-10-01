pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Io
import qs.Common

Singleton {
    id: root

    readonly property string command: Paths.binHome + "/holoarch-desktop"
    property string powerSummary: ""
    property int updateCount: -1
    property string error: ""
    readonly property string updateSummary: error !== "" ? error : updateCount >= 0 ? qsTr("%1 updates").arg(
                                                                                          updateCount) : ""

    function refresh() {
        if (!statusProcess.running)
            statusProcess.running = true;
    }

    function openPowerMenu() {
        Quickshell.execDetached([root.command, "power"]);
    }

    function openMaintenanceMenu() {
        Quickshell.execDetached([root.command, "maintenance"]);
    }

    Component.onCompleted: root.refresh()

    Timer {
        interval: 15000
        repeat: true
        running: WidgetState.quickSettingsOpen
        triggeredOnStart: true
        onTriggered: root.refresh()
    }

    Process {
        id: statusProcess
        command: [root.command, "status"]
        stdout: StdioCollector {
            onStreamFinished: {
                try {
                    const state = JSON.parse(text);
                    if (state.schemaVersion !== 1 || state.ok !== true || typeof state.powerSummary !== "string" ||
                            !Number.isInteger(state.updateCount))
                        throw new Error("Invalid desktop status");
                    root.powerSummary = state.powerSummary;
                    root.updateCount = state.updateCount;
                    root.error = state.updatesError || "";
                } catch (e) {
                    root.powerSummary = "";
                    root.updateCount = -1;
                    root.error = qsTr("Status unavailable");
                }
            }
        }
        onExited: exitCode => {
            if (exitCode !== 0) {
                root.powerSummary = "";
                root.updateCount = -1;
                root.error = qsTr("Status unavailable");
            }
        }
    }
}
