import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import CouchCoop 1.0
import ComputerManager 1.0
import SdlGamepadKeyNavigation 1.0

Item {
    property var coop
    property var mainWindow
    property bool initialized: false
    property bool launchStarted: false
    property string leftHostUuid: ""
    property string rightHostUuid: ""
    property int leftAppId: -1
    property int rightAppId: -1
    property string leftControllerPath: ""
    property string rightControllerPath: ""

    id: page
    objectName: qsTr("Couch Co-op")
    focus: true
    activeFocusOnTab: true

    function hostAt(uuid) {
        for (var i = 0; i < coop.hosts.length; i++) {
            if (coop.hosts[i].uuid === uuid) {
                return coop.hosts[i]
            }
        }
        return null
    }

    function appsFor(uuid) {
        var host = hostAt(uuid)
        return host && host.apps ? host.apps : []
    }

    function restoreHostSelection(combo, uuid) {
        for (var i = 0; i < coop.hosts.length; i++) {
            if (coop.hosts[i].uuid === uuid) {
                combo.currentIndex = i
                return true
            }
        }
        combo.currentIndex = -1
        return false
    }

    function restoreAppSelection(combo, apps, appId) {
        for (var i = 0; i < apps.length; i++) {
            if (apps[i].id === appId) {
                combo.currentIndex = i
                return true
            }
        }
        combo.currentIndex = apps.length > 0 ? 0 : -1
        return false
    }

    function restoreControllerSelection(combo, path) {
        for (var i = 0; i < coop.controllers.length; i++) {
            if (coop.controllers[i].path === path) {
                combo.currentIndex = i
                return true
            }
        }
        combo.currentIndex = -1
        return false
    }

    function controllerOptions() {
        if (!coop) {
            return []
        }
        var totals = {}
        var occurrences = {}
        var result = []
        for (var i = 0; i < coop.controllers.length; i++) {
            var name = coop.controllers[i].name
            totals[name] = (totals[name] || 0) + 1
        }
        for (var j = 0; j < coop.controllers.length; j++) {
            var controller = coop.controllers[j]
            var controllerName = controller.name
            occurrences[controllerName] = (occurrences[controllerName] || 0) + 1
            var displayName = totals[controllerName] > 1
                    ? qsTr("%1 (%2)").arg(controllerName).arg(occurrences[controllerName])
                    : controllerName
            result.push({"name": controllerName, "path": controller.path, "displayName": displayName})
        }
        return result
    }

    function refreshSelections() {
        if (!selectedHostIsValid(leftHostUuid) && coop.hosts.length > 0) {
            leftHostUuid = coop.hosts[0].uuid
        }
        if (!selectedHostIsValid(rightHostUuid) && coop.hosts.length > 1) {
            rightHostUuid = coop.hosts[0].uuid === leftHostUuid ? coop.hosts[1].uuid : coop.hosts[0].uuid
        }
        restoreHostSelection(leftHostCombo, leftHostUuid)
        restoreHostSelection(rightHostCombo, rightHostUuid)
        restoreAppSelection(leftAppCombo, appsFor(leftHostUuid), leftAppId)
        restoreAppSelection(rightAppCombo, appsFor(rightHostUuid), rightAppId)
    }

    function selectedHostIsValid(uuid) {
        return uuid !== "" && hostAt(uuid) !== null
    }

    function selectionsReady() {
        return selectedHostIsValid(leftHostUuid) &&
               selectedHostIsValid(rightHostUuid) &&
               leftHostUuid !== rightHostUuid &&
               leftAppId >= 0 && rightAppId >= 0 &&
               leftControllerPath !== "" && rightControllerPath !== "" &&
               leftControllerPath !== rightControllerPath
    }

    function startCoop() {
        if (!selectionsReady()) {
            return
        }

        // Controllers can be connected or removed while this page is open.
        // The backend validates these paths again immediately before launch.
        launchStarted = true
        SdlGamepadKeyNavigation.disable()
        if (!coop.start(mainWindow, leftHostUuid, leftAppId, leftControllerPath,
                        rightHostUuid, rightAppId, rightControllerPath)) {
            launchStarted = false
            SdlGamepadKeyNavigation.enable()
        }
    }

    function showFailure(message) {
        launchStarted = false
        SdlGamepadKeyNavigation.enable()
        failureDialog.text = message
        failureDialog.open()
    }

    Component.onCompleted: {
        if (!coop) {
            return
        }
        if (!initialized) {
            coop.initialize(ComputerManager)
            initialized = true
        }
        refreshSelections()
    }

    StackView.onActivated: {
        SdlGamepadKeyNavigation.setUiNavMode(true)
        if (!initialized) {
            coop.initialize(ComputerManager)
            initialized = true
        }
        refreshSelections()
        if (coop.controllers.length > 0 && !leftControllerPath) {
            leftControllerPath = coop.controllers[0].path
        }
        if (coop.controllers.length > 1 && !rightControllerPath) {
            rightControllerPath = coop.controllers[1].path
        }
        restoreControllerSelection(leftControllerCombo, leftControllerPath)
        restoreControllerSelection(rightControllerCombo, rightControllerPath)
        forceActiveFocus(Qt.TabFocus)
    }

    StackView.onDeactivating: {
        if (coop.active) {
            coop.stop()
        } else {
            SdlGamepadKeyNavigation.enable()
        }
        SdlGamepadKeyNavigation.setUiNavMode(false)
    }

    Connections {
        target: coop

        onHostsChanged: {
            refreshSelections()
        }

        onControllersChanged: {
            if (!selectedControllerExists(leftControllerPath)) {
                leftControllerPath = ""
            }
            if (!selectedControllerExists(rightControllerPath)) {
                rightControllerPath = ""
            }
            restoreControllerSelection(leftControllerCombo, leftControllerPath)
            restoreControllerSelection(rightControllerCombo, rightControllerPath)
        }

        onFailed: {
            showFailure(message)
        }

        onActiveChanged: {
            if (!coop.active && launchStarted) {
                launchStarted = false
                SdlGamepadKeyNavigation.setUiNavMode(false)
                SdlGamepadKeyNavigation.enable()
            }
        }
    }

    function selectedControllerExists(path) {
        for (var i = 0; i < coop.controllers.length; i++) {
            if (coop.controllers[i].path === path) {
                return true
            }
        }
        return false
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 18

        Label {
            Layout.fillWidth: true
            text: qsTr("Choose two PCs and two controllers")
            font.pointSize: 24
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }

        Label {
            Layout.fillWidth: true
            visible: coop && coop.hosts.length < 2
            text: qsTr("Two eligible PCs are required. Pair both Sunshine PCs and make sure they are online.")
            font.pointSize: 16
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }

        Label {
            Layout.fillWidth: true
            visible: coop && coop.controllers.length < 2
            text: qsTr("Connect two gamepads to the Steam Deck to assign one to each stream.")
            font.pointSize: 16
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: 2
            columnSpacing: 36
            rowSpacing: 12

            Label { text: qsTr("Player 1"); font.pointSize: 20; font.bold: true }
            Label { text: qsTr("Player 2"); font.pointSize: 20; font.bold: true }

            Label { text: qsTr("PC") }
            Label { text: qsTr("PC") }

            ComboBox {
                id: leftHostCombo
                Layout.fillWidth: true
                model: coop ? coop.hosts : []
                textRole: "name"
                activeFocusOnTab: true
                onCurrentIndexChanged: {
                    if (currentIndex >= 0 && currentIndex < coop.hosts.length) {
                        leftHostUuid = coop.hosts[currentIndex].uuid
                        leftAppId = -1
                        restoreAppSelection(leftAppCombo, appsFor(leftHostUuid), leftAppId)
                    }
                }
            }

            ComboBox {
                id: rightHostCombo
                Layout.fillWidth: true
                model: coop ? coop.hosts : []
                textRole: "name"
                activeFocusOnTab: true
                onCurrentIndexChanged: {
                    if (currentIndex >= 0 && currentIndex < coop.hosts.length) {
                        rightHostUuid = coop.hosts[currentIndex].uuid
                        rightAppId = -1
                        restoreAppSelection(rightAppCombo, appsFor(rightHostUuid), rightAppId)
                    }
                }
            }

            Label { text: qsTr("Game") }
            Label { text: qsTr("Game") }

            ComboBox {
                id: leftAppCombo
                Layout.fillWidth: true
                model: appsFor(leftHostUuid)
                textRole: "name"
                activeFocusOnTab: true
                onCurrentIndexChanged: {
                    var apps = appsFor(leftHostUuid)
                    if (currentIndex >= 0 && currentIndex < apps.length) {
                        leftAppId = apps[currentIndex].id
                    }
                }
            }

            ComboBox {
                id: rightAppCombo
                Layout.fillWidth: true
                model: appsFor(rightHostUuid)
                textRole: "name"
                activeFocusOnTab: true
                onCurrentIndexChanged: {
                    var apps = appsFor(rightHostUuid)
                    if (currentIndex >= 0 && currentIndex < apps.length) {
                        rightAppId = apps[currentIndex].id
                    }
                }
            }

            Label { text: qsTr("Controller") }
            Label { text: qsTr("Controller") }

            ComboBox {
                id: leftControllerCombo
                Layout.fillWidth: true
                model: controllerOptions()
                textRole: "displayName"
                activeFocusOnTab: true
                onCurrentIndexChanged: {
                    if (currentIndex >= 0 && currentIndex < coop.controllers.length) {
                        leftControllerPath = coop.controllers[currentIndex].path
                    }
                }
            }

            ComboBox {
                id: rightControllerCombo
                Layout.fillWidth: true
                model: controllerOptions()
                textRole: "displayName"
                activeFocusOnTab: true
                onCurrentIndexChanged: {
                    if (currentIndex >= 0 && currentIndex < coop.controllers.length) {
                        rightControllerPath = coop.controllers[currentIndex].path
                    }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            visible: leftHostUuid === rightHostUuid && leftHostUuid !== ""
            text: qsTr("Choose two different PCs.")
            color: "#ffb74d"
            horizontalAlignment: Text.AlignHCenter
        }

        Label {
            Layout.fillWidth: true
            visible: leftControllerPath === rightControllerPath && leftControllerPath !== ""
            text: qsTr("Choose two different controllers.")
            color: "#ffb74d"
            horizontalAlignment: Text.AlignHCenter
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 18

            NavigableToolButton {
                id: startButton
                text: qsTr("Start co-op")
                enabled: selectionsReady() && !coop.active
                activeFocusOnTab: true
                onClicked: startCoop()
                Keys.onReturnPressed: clicked()
                Keys.onEnterPressed: clicked()
            }

            NavigableToolButton {
                text: qsTr("Back")
                activeFocusOnTab: true
                onClicked: stackView.pop()
                Keys.onReturnPressed: clicked()
                Keys.onEnterPressed: clicked()
            }
        }
    }

    NavigableMessageDialog {
        id: failureDialog
        standardButtons: Dialog.Ok
    }
}
