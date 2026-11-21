import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    width: 1200
    height: 790
    minimumWidth: 900
    minimumHeight: 630
    visible: true
    title: "Ember · Local Intelligence"
    color: "#000000"
    font.family: "Segoe UI"
    property int page: 0
    property color mint: "#77C4FF"
    property color silver: "#B2C2D1"
    property color borderColor: "#1A2D45"
    property color cardColor: "#0A101C"
    property string historyQuery: ""
    Timer {
        id: historySearchDebounce
        interval: 180
        repeat: false
        onTriggered: window.historyQuery = historySearch.text.trim()
    }
    Dialog {
        id: deleteChatDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 430
        modal: true
        title: "Delete local chat?"
        standardButtons: Dialog.Yes | Dialog.Cancel
        onAccepted: ember.deleteCurrentSession()
        background: Rectangle {
            color: "#0A1626"
            radius: 16
            border.color: "#225688"
            border.width: 1
        }
        contentItem: Item {
            implicitWidth: 365
            implicitHeight: 83
            Text {
                anchors.fill: parent
                text: "Only this conversation will be deleted from the local SQLite archive. The other chats will remain available."
                wrapMode: Text.WordWrap
                font.pixelSize: 13
                color: "#D8EBFB"
            }
        }
    }
    Dialog {
        id: continuityDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(window.width - 90, 590)
        height: Math.min(window.height - 90, 490)
        modal: true
        title: "Conversation memory"
        standardButtons: Dialog.NoButton
        onOpened: continuityEditor.text = ember.continuityNotes
        background: Rectangle {
            color: "#091522"
            radius: 17
            border.color: "#3676A8"
            border.width: 1
        }
        contentItem: ColumnLayout {
            spacing: 12
            Text {
                Layout.fillWidth: true
                text: "Keep essential characters, relationships, world rules or project facts across sessions. This text stays on your PC and belongs only to this chat."
                color: "#BACFE4"
                wrapMode: Text.WordWrap
                font.pixelSize: 13
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                background: Rectangle {
                    color: "#040B14"
                    radius: 11
                    border.color: "#1B3D5C"
                }
                TextArea {
                    id: continuityEditor
                    wrapMode: TextEdit.Wrap
                    color: "#E3F3FF"
                    font.pixelSize: 13
                    padding: 14
                    placeholderText: "Characters, locations, promises, unresolved questions, story timeline..."
                    placeholderTextColor: "#7796AF"
                    selectByMouse: true
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: continuityEditor.text.length + " / 1,800 characters"
                    color: continuityEditor.text.length > 1800 ? "#FF9F99" : "#8CA9C2"
                    font.pixelSize: 11
                }
                Item { Layout.fillWidth: true }
                PillButton {
                    text: "Cancel"
                    implicitHeight: 36
                    implicitWidth: 95
                    onClicked: continuityDialog.close()
                }
                PillButton {
                    text: "Save memory"
                    implicitHeight: 36
                    implicitWidth: 128
                    enabled: continuityEditor.text.length <= 1800
                    onClicked: {
                        ember.setContinuityNotes(continuityEditor.text)
                        continuityDialog.close()
                    }
                }
            }
        }
    }
    Connections {
        target: ember
        function onDictationReady(text) {
            if (!text || text.length === 0) return
            window.page = 1
            const needsSpace = composer.text.length > 0 &&
                               !/\s$/.test(composer.text)
            composer.insert(composer.cursorPosition, (needsSpace ? " " : "") + text)
            composer.forceActiveFocus()
        }
    }

    function submitComposer() {
        const value = composer.text.trim()
        if (value.length === 0 || !ember.brainReady || ember.busy) return
        ember.submitText(value)
        composer.text = ""
    }

    component NavButton: Rectangle {
        required property string label
        required property string symbol
        required property int index
        width: parent.width
        height: 53
        radius: 15
        color: window.page === index ? "#112940" : (hover.hovered ? "#0D1B30" : "transparent")
        border.width: window.page === index ? 1 : 0
        border.color: "#2C6997"
        Behavior on color { ColorAnimation { duration: 180 } }
        HoverHandler { id: hover }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: window.page = index
        }
        Row {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 17
            spacing: 16
            Text { text: symbol; color: window.page === index ? window.mint : window.silver; font.pixelSize: 20 }
            Text { text: label; color: window.page === index ? "#E6F4FF" : "#B9C8D9"; font.pixelSize: 15; font.weight: window.page === index ? Font.DemiBold : Font.Normal }
        }
    }
    component PillButton: Button {
        id: pill
        property color fill: "#0F2B4B"
        property color ink: "#EAF5FF"
        font.pixelSize: 14
        font.weight: Font.DemiBold
        implicitHeight: 44
        implicitWidth: 138
        background: Rectangle {
            radius: 13
            color: !pill.enabled ? "#202B3A" : (pill.down ? "#245780" : (pill.hovered ? "#19496D" : pill.fill))
            border.color: !pill.enabled ? "#36404B" : "#3476A7"
            border.width: 1
            Behavior on color { ColorAnimation { duration: 140 } }
        }
        contentItem: Text {
            text: pill.text
            color: pill.enabled ? pill.ink : "#8698B5"
            font: pill.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            Layout.preferredWidth: 225
            Layout.fillHeight: true
            color: "#05080E"
            border.color: "#122035"
            border.width: 1
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 0
                RowLayout {
                    Layout.topMargin: 12
                    Layout.bottomMargin: 9
                    spacing: 13
                    Rectangle {
                        width: 42; height: 42; radius: 15
                        gradient: Gradient {
                            GradientStop { position: 0; color: "#134C81" }
                            GradientStop { position: 1; color: "#08152A" }
                        }
                        border.color: "#378CC8"
                        Image {
                            anchors.fill: parent
                            anchors.margins: 3
                            source: "qrc:/qt/qml/Ember/assets/ember-emblem.svg"
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                            mipmap: true
                        }
                    }
                    Column {
                        spacing: 2
                        Text { text: "EMBER"; color: "#F1F9FF"; font.family: "Bahnschrift"; font.pixelSize: 21; font.bold: true; font.letterSpacing: 2.5 }
                        Text { text: "LOCAL INTELLIGENCE"; color: "#789FC0"; font.pixelSize: 9; font.letterSpacing: 1.2 }
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#1A2940"; Layout.topMargin: 21; Layout.bottomMargin: 21 }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    NavButton { label: "Voice"; symbol: "◉"; index: 0 }
                    NavButton { label: "Chat"; symbol: "▤"; index: 1 }
                    NavButton { label: "Settings"; symbol: "⚙"; index: 2 }
                }
                ColumnLayout {
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    Layout.topMargin: 25
                    spacing: 10
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: "LOCAL HISTORY"
                            color: "#8099AC"
                            font.pixelSize: 10
                            font.letterSpacing: 1.4
                            font.weight: Font.DemiBold
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: ember.localHistoryReady ? "SQLite" : "Memory"
                            font.pixelSize: 10
                            color: ember.localHistoryReady ? window.mint : "#CF9B88"
                        }
                    }
                    TextField {
                        id: historySearch
                        Layout.fillWidth: true
                        Layout.preferredHeight: 37
                        placeholderText: "Search saved chats"
                        font.pixelSize: 12
                        color: "#D6EBFB"
                        placeholderTextColor: "#7892AD"
                        leftPadding: 12
                        rightPadding: 12
                        selectByMouse: true
                        enabled: ember.localHistoryReady
                        onTextChanged: historySearchDebounce.restart()
                        Keys.onEscapePressed: text = ""
                        background: Rectangle {
                            radius: 10
                            color: "#071422"
                            border.width: 1
                            border.color: historySearch.activeFocus ? "#4B9BD0" : "#203A54"
                            Behavior on border.color { ColorAnimation { duration: 140 } }
                        }
                    }
                    ListView {
                        id: historyList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 5
                        model: {
                            const updated = ember.sessions
                            return window.historyQuery.length > 0
                                ? ember.searchHistory(window.historyQuery) : updated
                        }
                        boundsBehavior: Flickable.StopAtBounds
                        delegate: Rectangle {
                            required property var modelData
                            width: historyList.width
                            height: window.historyQuery.length > 0 ? 67 : 49
                            radius: 10
                            color: modelData.id === ember.currentSessionId
                                   ? "#133352" : (historyHover.hovered ? "#0C1D31" : "transparent")
                            border.color: modelData.id === ember.currentSessionId
                                          ? "#2B6EA6" : "transparent"
                            border.width: 1
                            Behavior on color { ColorAnimation { duration: 110 } }
                            Text {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.leftMargin: 12
                                anchors.rightMargin: 9
                                anchors.topMargin: window.historyQuery.length > 0 ? 9 : 0
                                height: window.historyQuery.length > 0 ? 24 : parent.height
                                verticalAlignment: Text.AlignVCenter
                                text: modelData.title
                                elide: Text.ElideRight
                                color: modelData.id === ember.currentSessionId
                                       ? "#EAF7FF" : "#A6BED2"
                                font.pixelSize: 12
                                maximumLineCount: 1
                            }
                            Text {
                                visible: window.historyQuery.length > 0
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.leftMargin: 12
                                anchors.rightMargin: 9
                                anchors.topMargin: 36
                                text: modelData.snippet || "Title match"
                                elide: Text.ElideRight
                                color: "#7F9FBA"
                                font.pixelSize: 10
                                maximumLineCount: 1
                            }
                            HoverHandler { id: historyHover }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    ember.openSession(modelData.id)
                                    window.page = 1
                                }
                            }
                        }
                    }
                    PillButton {
                        Layout.fillWidth: true
                        text: "Export selected chat"
                        implicitHeight: 37
                        enabled: ember.localHistoryReady && ember.currentSessionId.length > 0 && !ember.busy
                        onClicked: ember.exportCurrentSession()
                    }
                    Text {
                        text: "Only text is saved. No audio recordings."
                        color: "#718CA5"
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 9
                    Text { text: "ON THIS MACHINE"; color: "#7194B4"; font.pixelSize: 10; font.letterSpacing: 1.3 }
                    RowLayout {
                        spacing: 9
                        Rectangle {
                            width: 8; height: 8; radius: 4
                            color: ember.brainReady ? window.mint : "#FFB97D"
                        }
                        Text {
                            text: ember.brainReady ? "Brain online" : "Brain offline"
                            color: window.silver
                            font.pixelSize: 13
                        }
                    }
                    Text {
                        text: "Private by design. No hosted AI endpoints."
                        color: "#7D95AF"
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#000000"
            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 36
                anchors.rightMargin: 36
                anchors.topMargin: 27
                anchors.bottomMargin: 25
                spacing: 19
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Column {
                        spacing: 5
                        Text {
                            text: window.page === 0 ? "The sound of intelligence" :
                                  window.page === 1 ? "A conversation with Ember" : "Make Ember yours"
                            font.pixelSize: 27
                            font.family: "Bahnschrift"
                            font.weight: Font.DemiBold
                            color: "#F3F7FE"
                        }
                        Text {
                            text: window.page === 0 ? "English voice · modular local engines · no browser wrapper" :
                                  window.page === 1 ? "Persistent on-device history · RU/EN dictation · no cloud sync" :
                                  "Modular engines, voices and personality"
                            font.pixelSize: 12
                            color: "#92A5B8"
                        }
                    }
                    Item { Layout.fillWidth: true }
                    PillButton {
                        text: ember.brainReady ? "Stop brain" : "Start brain"
                        onClicked: ember.brainReady ? ember.stopBrain() : ember.startBrain()
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 43
                    radius: 12
                    color: "#0B1A2B"
                    border.color: "#254263"
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        spacing: 12
                        Rectangle {
                            width: 8; height: 8; radius: 4
                            color: ember.busy ? "#FFC889" : (ember.brainReady ? window.mint : "#AF8492")
                        }
                        Text {
                            Layout.fillWidth: true
                            text: ember.status
                            color: "#CADFF2"
                            elide: Text.ElideRight
                            font.pixelSize: 13
                        }
                        BusyIndicator { running: ember.busy; visible: running; implicitWidth: 26; implicitHeight: 26 }
                    }
                }

                StackLayout {
                    id: views
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: window.page

                    Item {
                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 14
                            Item { Layout.fillHeight: true }
                            Item {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.preferredWidth: 345
                                Layout.preferredHeight: 295
                                Rectangle {
                                    id: orbit
                                    anchors.centerIn: parent
                                    width: 282
                                    height: 282
                                    radius: 141
                                    color: "transparent"
                                    border.color: ember.listening ? "#6DBEFF" : "#204B76"
                                    border.width: ember.listening ? 2 : 1
                                    Behavior on border.color { ColorAnimation { duration: 250 } }
                                    Rectangle {
                                        x: 16; y: 32; width: 10; height: 10; radius: 5
                                        color: "#71C7FF"
                                        opacity: ember.busy || ember.listening ? 0.9 : 0.4
                                    }
                                    NumberAnimation on rotation {
                                        from: 0; to: 360; duration: 14500
                                        loops: Animation.Infinite
                                        running: ember.busy || ember.listening
                                    }
                                }
                                Rectangle {
                                    anchors.centerIn: parent
                                    width: 228
                                    height: 228
                                    radius: 114
                                    color: "#112745"
                                    border.color: "#2C5F93"
                                    border.width: 1
                                    gradient: Gradient {
                                        GradientStop { position: 0; color: "#1D598C" }
                                        GradientStop { position: 0.48; color: "#142C4C" }
                                        GradientStop { position: 1; color: "#09162B" }
                                    }
                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: 138; height: 138; radius: 69
                                        color: "#0A203F"
                                        border.color: "#53A7E6"
                                        border.width: 1
                                        Image {
                                            anchors.fill: parent
                                            anchors.margins: 12
                                            source: "qrc:/qt/qml/Ember/assets/ember-emblem.svg"
                                            fillMode: Image.PreserveAspectFit
                                            smooth: true
                                            mipmap: true
                                            opacity: ember.listening ? 0.62 : 1
                                        }
                                        Rectangle {
                                            anchors.centerIn: parent
                                            visible: ember.listening
                                            width: 23; height: 23; radius: 12
                                            color: window.mint
                                        }
                                    }
                                }
                            }
                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                text: ember.listening ? "I'm listening" :
                                      ember.speaking ? "Ember is speaking" :
                                      ember.busy ? "Putting thoughts into words" : "Speak when you're ready"
                                color: "#F2F9FF"
                                font.pixelSize: 23
                                font.weight: Font.DemiBold
                            }
                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.maximumWidth: 540
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                                text: "Push to talk. Your microphone is idle until you start, and recording stops after 30 seconds."
                                color: "#94AFC8"
                                font.pixelSize: 13
                            }
                            RowLayout {
                                Layout.alignment: Qt.AlignHCenter
                                spacing: 13
                                PillButton {
                                    text: ember.listening ? "Stop recording" : "Record voice"
                                    implicitWidth: 169
                                    enabled: ember.listening || (ember.brainReady && !ember.busy)
                                    onClicked: ember.listening ? ember.stopRecording() : ember.startRecording()
                                }
                                PillButton {
                                    text: "Test voice"
                                    implicitWidth: 120
                                    enabled: !ember.busy && !ember.listening
                                    onClicked: ember.testVoice()
                                }
                                PillButton {
                                    text: "Cancel"
                                    implicitWidth: 100
                                    enabled: ember.busy || ember.listening || ember.speaking
                                    onClicked: ember.cancelWork()
                                }
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.maximumWidth: 670
                                Layout.alignment: Qt.AlignHCenter
                                Layout.preferredHeight: 100
                                radius: 17
                                color: window.cardColor
                                border.color: window.borderColor
                                Column {
                                    anchors.fill: parent
                                    anchors.margins: 18
                                    spacing: 10
                                    Text { text: "LAST HEARD"; color: "#7EB5E2"; font.pixelSize: 10; font.letterSpacing: 1.2 }
                                    Text {
                                        width: parent.width
                                        text: ember.transcript.length ? ember.transcript : "Your words will appear here once transcribed."
                                        color: ember.transcript.length ? "#E1F2FF" : "#879CAB"
                                        font.pixelSize: 13
                                        wrapMode: Text.WordWrap
                                        maximumLineCount: 3
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.maximumWidth: 670
                                Layout.fillWidth: true
                                text: ember.voiceTimings
                                color: "#849FAF"
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                                font.pixelSize: 11
                            }
                            Item { Layout.fillHeight: true }
                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                text: ember.speechServerReady && ember.voiceEngine === 1 ?
                                      "Ember can start English speech after its first LLM sentence when Early Speech is enabled. Native PCM output remains push-to-talk." :
                                      "Conversation remains turn-based. Start native speech.cpp in Settings for incremental PCM playback."
                                color: "#78909E"
                                font.pixelSize: 11
                            }
                        }
                    }
                    Item {
                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 15
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: "LOCAL CONVERSATION"; visible: window.width >= 1040; color: "#9CB8D4"; font.pixelSize: 11; font.letterSpacing: 1.2 }
                                Item { Layout.fillWidth: true }
                                ComboBox {
                                    id: conversationMode
                                    model: ["Conversation", "Story studio", "Troubleshooting", "Development"]
                                    currentIndex: ember.chatMode
                                    onActivated: ember.chatMode = currentIndex
                                    Layout.preferredWidth: window.width < 1100 ? 145 : 174
                                    implicitHeight: 36
                                    font.pixelSize: 12
                                    contentItem: Text {
                                        leftPadding: 11
                                        text: conversationMode.displayText
                                        color: "#D8EFFF"
                                        verticalAlignment: Text.AlignVCenter
                                        elide: Text.ElideRight
                                        font.pixelSize: 12
                                    }
                                    background: Rectangle {
                                        color: "#0B223A"
                                        radius: 10
                                        border.color: "#275D8E"
                                    }
                                    delegate: ItemDelegate {
                                        required property int index
                                        required property string modelData
                                        width: conversationMode.width
                                        height: 38
                                        text: modelData
                                        contentItem: Text {
                                            text: parent.text
                                            color: "#DFEEFF"
                                            font.pixelSize: 12
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                        background: Rectangle {
                                            color: parent.highlighted ? "#184B79" : "#0C2237"
                                        }
                                    }
                                    popup: Popup {
                                        y: conversationMode.height + 4
                                        width: conversationMode.width
                                        padding: 3
                                        implicitHeight: contentItem.implicitHeight + 6
                                        background: Rectangle {
                                            color: "#0B1D31"
                                            radius: 10
                                            border.color: "#2D69A0"
                                        }
                                        contentItem: ListView {
                                            model: conversationMode.popup.visible ? conversationMode.delegateModel : null
                                            currentIndex: conversationMode.highlightedIndex
                                            implicitHeight: contentHeight
                                            clip: true
                                        }
                                    }
                                }
                                PillButton {
                                    text: window.width < 1100 ? "Stop" : "Stop reply"
                                    implicitWidth: window.width < 1100 ? 74 : 119
                                    implicitHeight: 36
                                    enabled: ember.busy
                                    visible: ember.busy
                                    onClicked: ember.cancelWork()
                                }
                                PillButton {
                                    text: "Memory"
                                    implicitWidth: 94
                                    implicitHeight: 36
                                    enabled: ember.localHistoryReady
                                    onClicked: continuityDialog.open()
                                }
                                PillButton {
                                    text: "Delete"
                                    implicitWidth: 84
                                    implicitHeight: 36
                                    enabled: ember.localHistoryReady
                                    onClicked: deleteChatDialog.open()
                                }
                                PillButton {
                                    text: window.width < 1100 ? "New chat" : "New conversation"
                                    implicitWidth: window.width < 1100 ? 105 : 166
                                    implicitHeight: 36
                                    onClicked: ember.clearConversation()
                                }
                            }
                            ListView {
                                id: timeline
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                spacing: 15
                                model: ember.messages
                                boundsBehavior: Flickable.StopAtBounds
                                delegate: Item {
                                    required property var modelData
                                    width: timeline.width
                                    height: messageSurface.implicitHeight + 8
                                    Rectangle {
                                        id: messageSurface
                                        anchors.right: modelData.role === "you" ? parent.right : undefined
                                        anchors.left: modelData.role === "you" ? undefined : parent.left
                                        width: Math.min(parent.width * 0.82, 700)
                                        implicitHeight: bubbleBody.implicitHeight + 39
                                        radius: 16
                                        color: modelData.role === "you" ? "#123E68" : "#172434"
                                        border.color: modelData.role === "you" ? "#3576AF" : "#2F4056"
                                        Column {
                                            id: bubbleBody
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.top: parent.top
                                            anchors.leftMargin: 20
                                            anchors.rightMargin: 20
                                            anchors.topMargin: 16
                                            spacing: 8
                                            Text {
                                                text: modelData.role === "you" ? "YOU" : "EMBER"
                                                color: modelData.role === "you" ? window.mint : "#B5CCEA"
                                                font.pixelSize: 10
                                                font.bold: true
                                                font.letterSpacing: 1.1
                                            }
                                            Text {
                                                width: parent.width
                                                text: modelData.body
                                                textFormat: Text.PlainText
                                                wrapMode: Text.Wrap
                                                color: "#EEF3F9"
                                                font.pixelSize: 14
                                                lineHeight: 1.3
                                            }
                                        }
                                    }
                                }
                                Text {
                                    anchors.centerIn: parent
                                    visible: timeline.count === 0
                                    text: "A private conversation begins here.\nNo messages have been saved or sent to the cloud."
                                    color: "#879DAC"
                                    horizontalAlignment: Text.AlignHCenter
                                    font.pixelSize: 15
                                    lineHeight: 1.5
                                }
                                Connections {
                                    target: ember
                                    function onMessagesChanged() { timeline.positionViewAtEnd() }
                                }
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 142
                                color: window.cardColor
                                radius: 17
                                border.color: window.borderColor
                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    spacing: 10
                                    TextArea {
                                        id: composer
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        enabled: !ember.busy
                                        color: "#ECF4F8"
                                        font.pixelSize: 14
                                        wrapMode: TextEdit.Wrap
                                        placeholderText: ember.brainReady ?
                                                         (ember.chatMode === 1 ? "Continue your story, build characters, or write a scene…" :
                                                          ember.chatMode === 2 ? "Describe the problem, symptoms, and observations…" :
                                                          ember.chatMode === 3 ? "Describe the code, repository, or feature…" :
                                                          "Ask Ember anything…") :
                                                          "Type or dictate locally · start the brain to send"
                                        placeholderTextColor: "#748C9C"
                                        background: Rectangle { color: "transparent" }
                                        Keys.onPressed: function(event) {
                                            if ((event.modifiers & Qt.ControlModifier) &&
                                                (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) {
                                                window.submitComposer()
                                                event.accepted = true
                                            }
                                        }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Text { text: "Ctrl + Enter to send"; color: "#8097A7"; font.pixelSize: 11 }
                                        Item { Layout.fillWidth: true }
                                        PillButton {
                                            text: ember.dictationActive ? "● Dictating…" : "Hold to dictate"
                                            implicitWidth: 145
                                            implicitHeight: 38
                                            enabled: !ember.busy && (!ember.listening || ember.dictationActive)
                                            onPressed: ember.startDictation()
                                            onReleased: ember.stopDictation()
                                            onCanceled: ember.stopDictation()
                                            ToolTip.visible: hovered && !ember.dictationActive
                                            ToolTip.delay: 800
                                            ToolTip.text: "Hold, speak English or Russian, then release. The transcript appears in your composer."
                                        }
                                        PillButton {
                                            text: ember.busy ? "Working…" : "Send message"
                                            implicitWidth: 146
                                            implicitHeight: 38
                                            enabled: ember.brainReady && !ember.busy && composer.text.trim().length > 0
                                            onClicked: window.submitComposer()
                                        }
                                    }
                                }
                            }
                        }
                    }
                    Item {
                        Flickable {
                            anchors.fill: parent
                            clip: true
                            contentWidth: width
                            contentHeight: settingsContent.implicitHeight
                            ScrollBar.vertical: ScrollBar {}
                            ColumnLayout {
                                id: settingsContent
                                width: parent.width
                                spacing: 16
                                Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: 85
                                    radius: 16
                                    color: "#0B203B"
                                    border.color: "#24578E"
                                    Column {
                                        anchors.fill: parent
                                        anchors.margins: 17
                                        spacing: 6
                                        Text { text: "PORTABLE LOCAL SETUP"; color: window.mint; font.pixelSize: 12; font.bold: true; font.letterSpacing: 1 }
                                        Text {
                                            width: parent.width
                                            text: "Default installation: D:\\Games\\Ember. Choose trusted local engines and models; optional model downloads begin only when you explicitly start a service."
                                            color: "#CFDFF1"
                                            wrapMode: Text.WordWrap
                                            font.pixelSize: 13
                                        }
                                    }
                                }
                                Text { text: "MODEL ROUTING"; color: "#D9ECFF"; font.pixelSize: 16; font.weight: Font.DemiBold }
                                Repeater {
                                    model: [
                                        { key: "brainExe", label: "llama-server executable" },
                                        { key: "brainModel", label: "Language model · GGUF" },
                                        { key: "asrExe", label: "whisper-cli executable" },
                                        { key: "asrModel", label: "Speech recognition weights" },
                                        { key: "asrVadModel", label: "Silero speech detection · optional" },
                                        { key: "ttsExe", label: "sherpa-onnx TTS executable" },
                                        { key: "speechExe", label: "speech.cpp native TTS executable · optional" },
                                        { key: "ttsDir", label: "Kokoro model folder" }
                                    ]
                                    delegate: Rectangle {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        implicitHeight: 74
                                        radius: 13
                                        color: window.cardColor
                                        border.color: "#263849"
                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: 16
                                            anchors.rightMargin: 13
                                            spacing: 12
                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 4
                                                Text { text: modelData.label; color: "#E5EFF6"; font.pixelSize: 13; font.weight: Font.DemiBold }
                                                Text {
                                                    Layout.fillWidth: true
                                                    text: { let version = ember.configurationRevision; return ember.pathFor(modelData.key) }
                                                    color: "#8599B0"
                                                    font.pixelSize: 11
                                                    elide: Text.ElideMiddle
                                                    maximumLineCount: 1
                                                }
                                            }
                                            PillButton {
                                                text: "Browse"
                                                implicitWidth: 93
                                                implicitHeight: 36
                                                onClicked: ember.choosePath(modelData.key)
                                            }
                                        }
                                    }
                                }
                                Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: 100
                                    radius: 13
                                    color: window.cardColor
                                    border.color: window.borderColor
                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 14
                                        spacing: 9
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Text {
                                                text: "Dictation recognition language"
                                                color: "#D2E6F8"
                                                font.pixelSize: 13
                                                Layout.fillWidth: true
                                            }
                                            ComboBox {
                                                id: languagePicker
                                                Layout.preferredWidth: 212
                                                implicitHeight: 38
                                                model: ["Auto-detect · RU / EN", "English", "Russian"]
                                                currentIndex: ember.recognitionLanguage
                                                onActivated: ember.recognitionLanguage = currentIndex
                                                font.pixelSize: 12
                                                contentItem: Text {
                                                    text: languagePicker.displayText
                                                    leftPadding: 11
                                                    color: "#E6F4FF"
                                                    font.pixelSize: 12
                                                    verticalAlignment: Text.AlignVCenter
                                                }
                                                background: Rectangle {
                                                    radius: 10
                                                    color: "#0A1B31"
                                                    border.color: "#2B648D"
                                                }
                                                delegate: ItemDelegate {
                                                    required property int index
                                                    required property string modelData
                                                    width: languagePicker.width
                                                    height: 38
                                                    text: modelData
                                                    contentItem: Text {
                                                        text: parent.text
                                                        color: "#E6F4FF"
                                                        font.pixelSize: 12
                                                        verticalAlignment: Text.AlignVCenter
                                                    }
                                                    background: Rectangle {
                                                        color: parent.highlighted ? "#15446A" : "#0C1C31"
                                                    }
                                                }
                                            }
                                        }
                                        Text {
                                            text: "Auto-detect works with multilingual Whisper weights. Voice replies stay English."
                                            color: "#869CB6"
                                            font.pixelSize: 11
                                            wrapMode: Text.WordWrap
                                            Layout.fillWidth: true
                                        }
                                    }
                                }
                                Text {
                                    text: "AUDIO ROUTING"
                                    color: "#D9ECFF"
                                    font.pixelSize: 16
                                    font.weight: Font.DemiBold
                                    Layout.topMargin: 8
                                }
                                Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: 168
                                    radius: 14
                                    color: window.cardColor
                                    border.color: window.borderColor
                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 15
                                        spacing: 12
                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 16
                                            Text {
                                                text: "Microphone"
                                                color: "#C3D5DF"
                                                font.pixelSize: 13
                                                Layout.preferredWidth: 125
                                            }
                                            ComboBox {
                                                id: microphonePicker
                                                Layout.fillWidth: true
                                                model: ember.audioInputs
                                                textRole: "name"
                                                currentIndex: ember.selectedInputIndex
                                                enabled: !ember.listening
                                                onActivated: ember.setInputDeviceIndex(currentIndex)
                                                implicitHeight: 41
                                                font.pixelSize: 13
                                                background: Rectangle {
                                                    radius: 11
                                                    color: "#0B223A"
                                                    border.color: "#316EA3"
                                                }
                                                contentItem: Text {
                                                    leftPadding: 14
                                                    verticalAlignment: Text.AlignVCenter
                                                    color: "#E7F4FF"
                                                    elide: Text.ElideRight
                                                    font.pixelSize: 13
                                                    text: microphonePicker.displayText
                                                }
                                                popup: Popup {
                                                    y: microphonePicker.height + 4
                                                    width: microphonePicker.width
                                                    implicitHeight: Math.min(contentItem.implicitHeight + 6, 280)
                                                    padding: 3
                                                    background: Rectangle { color: "#102137"; radius: 10; border.color: "#2E6FA7" }
                                                    contentItem: ListView {
                                                        clip: true
                                                        implicitHeight: contentHeight
                                                        model: microphonePicker.popup.visible ? microphonePicker.delegateModel : null
                                                        currentIndex: microphonePicker.highlightedIndex
                                                        ScrollIndicator.vertical: ScrollIndicator {}
                                                    }
                                                }
                                                delegate: ItemDelegate {
                                                    required property var modelData
                                                    width: microphonePicker.width
                                                    height: 38
                                                    text: modelData.name
                                                    font.pixelSize: 12
                                                    contentItem: Text {
                                                        text: parent.text
                                                        color: "#EAF4FF"
                                                        font: parent.font
                                                        verticalAlignment: Text.AlignVCenter
                                                        elide: Text.ElideRight
                                                    }
                                                    background: Rectangle { color: parent.highlighted ? "#1D4672" : "#102137" }
                                                }
                                            }
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 16
                                            Text {
                                                text: "Speakers"
                                                color: "#C3D5DF"
                                                font.pixelSize: 13
                                                Layout.preferredWidth: 125
                                            }
                                            ComboBox {
                                                id: speakerPicker
                                                Layout.fillWidth: true
                                                model: ember.audioOutputs
                                                textRole: "name"
                                                currentIndex: ember.selectedOutputIndex
                                                implicitHeight: 41
                                                onActivated: ember.setOutputDeviceIndex(currentIndex)
                                                font.pixelSize: 13
                                                background: Rectangle {
                                                    radius: 11
                                                    color: "#0B223A"
                                                    border.color: "#316EA3"
                                                }
                                                contentItem: Text {
                                                    leftPadding: 14
                                                    verticalAlignment: Text.AlignVCenter
                                                    color: "#E7F4FF"
                                                    elide: Text.ElideRight
                                                    font.pixelSize: 13
                                                    text: speakerPicker.displayText
                                                }
                                                popup: Popup {
                                                    y: speakerPicker.height + 4
                                                    width: speakerPicker.width
                                                    implicitHeight: Math.min(contentItem.implicitHeight + 6, 280)
                                                    padding: 3
                                                    background: Rectangle { color: "#102137"; radius: 10; border.color: "#2E6FA7" }
                                                    contentItem: ListView {
                                                        clip: true
                                                        implicitHeight: contentHeight
                                                        model: speakerPicker.popup.visible ? speakerPicker.delegateModel : null
                                                        currentIndex: speakerPicker.highlightedIndex
                                                        ScrollIndicator.vertical: ScrollIndicator {}
                                                    }
                                                }
                                                delegate: ItemDelegate {
                                                    required property var modelData
                                                    width: speakerPicker.width
                                                    height: 38
                                                    text: modelData.name
                                                    font.pixelSize: 12
                                                    contentItem: Text {
                                                        text: parent.text
                                                        color: "#EAF4FF"
                                                        font: parent.font
                                                        verticalAlignment: Text.AlignVCenter
                                                        elide: Text.ElideRight
                                                    }
                                                    background: Rectangle { color: parent.highlighted ? "#1D4672" : "#102137" }
                                                }
                                            }
                                        }
                                        Text {
                                            text: "Device choices are stored locally; disconnected devices fall back to Windows default."
                                            color: "#889FAD"
                                            font.pixelSize: 11
                                            Layout.fillWidth: true
                                            wrapMode: Text.WordWrap
                                        }
                                    }
                                }
                                Text { text: "PERSONALITY & VOICE"; color: "#D9ECFF"; font.pixelSize: 16; font.weight: Font.DemiBold; Layout.topMargin: 8 }
                                Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: 178
                                    radius: 13
                                    color: window.cardColor
                                    border.color: window.borderColor
                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 14
                                        spacing: 8
                                        Text { text: "Editable system style"; color: "#CDDCE6"; font.pixelSize: 13 }
                                        TextArea {
                                            id: personaEditor
                                            Layout.fillWidth: true
                                            Layout.fillHeight: true
                                            text: ember.persona
                                            color: "#E3EDF4"
                                            font.pixelSize: 12
                                            wrapMode: TextEdit.Wrap
                                            background: Rectangle { radius: 8; color: "#0A1524" }
                                        }
                                        RowLayout {
                                            Item { Layout.fillWidth: true }
                                            PillButton {
                                                text: "Save persona"
                                                implicitWidth: 132
                                                implicitHeight: 32
                                                onClicked: ember.setPersona(personaEditor.text)
                                            }
                                        }
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 16
                                    Text {
                                        Layout.fillWidth: true
                                        text: "Synthesis engine"
                                        color: "#B1C7DE"
                                        font.pixelSize: 13
                                    }
                                    ComboBox {
                                        id: enginePicker
                                        Layout.preferredWidth: 268
                                        Layout.preferredHeight: 43
                                        model: [
                                            "Kokoro · local CLI",
                                            "Qwen3 / Chatterbox · local API"
                                        ]
                                        currentIndex: ember.voiceEngine
                                        enabled: !ember.busy && !ember.listening && !ember.speaking
                                        onActivated: ember.voiceEngine = currentIndex
                                        contentItem: Text {
                                            leftPadding: 14
                                            text: enginePicker.displayText
                                            color: "#EAF4FF"
                                            font.pixelSize: 13
                                            verticalAlignment: Text.AlignVCenter
                                            elide: Text.ElideRight
                                        }
                                        background: Rectangle {
                                            color: "#0D233A"
                                            radius: 12
                                            border.color: "#316EA3"
                                        }
                                        delegate: ItemDelegate {
                                            required property int index
                                            required property string modelData
                                            width: enginePicker.width
                                            height: 40
                                            text: modelData
                                            highlighted: enginePicker.highlightedIndex === index
                                            contentItem: Text {
                                                text: parent.text
                                                color: "#EAF4FF"
                                                font.pixelSize: 13
                                                verticalAlignment: Text.AlignVCenter
                                            }
                                            background: Rectangle {
                                                color: parent.highlighted ? "#1D4672" : "#102137"
                                            }
                                        }
                                    }
                                }
                                RowLayout {
                                    visible: ember.voiceEngine === 0
                                    Layout.fillWidth: true
                                    spacing: 16
                                    Text {
                                        Layout.fillWidth: true
                                        text: "Voice · Kokoro English v0.19"
                                        color: "#B1C7DE"
                                        font.pixelSize: 13
                                        wrapMode: Text.WordWrap
                                    }
                                    ComboBox {
                                        id: voiceSelector
                                        Layout.preferredWidth: 268
                                        Layout.preferredHeight: 43
                                        model: [
                                            "af · Female · US",
                                            "Bella · Female · US",
                                            "Nicole · Female · US",
                                            "Sarah · Female · US",
                                            "Sky · Female · US",
                                            "Adam · Male · US",
                                            "Michael · Male · US",
                                            "Emma · Female · UK",
                                            "Isabella · Female · UK",
                                            "George · Male · UK",
                                            "Lewis · Male · UK"
                                        ]
                                        currentIndex: ember.voiceId
                                        enabled: !ember.busy && !ember.listening && !ember.speaking
                                        onActivated: ember.voiceId = currentIndex
                                        contentItem: Text {
                                            text: voiceSelector.displayText
                                            color: "#EAF4FF"
                                            font.pixelSize: 13
                                            leftPadding: 14
                                            verticalAlignment: Text.AlignVCenter
                                            elide: Text.ElideRight
                                        }
                                        background: Rectangle {
                                            color: "#0D233A"
                                            radius: 12
                                            border.width: 1
                                            border.color: "#316EA3"
                                        }
                                        delegate: ItemDelegate {
                                            required property int index
                                            required property string modelData
                                            width: voiceSelector.width
                                            height: 40
                                            text: modelData
                                            font.pixelSize: 13
                                            highlighted: voiceSelector.highlightedIndex === index
                                            contentItem: Text {
                                                text: parent.text
                                                font: parent.font
                                                verticalAlignment: Text.AlignVCenter
                                                color: "#EAF4FF"
                                            }
                                            background: Rectangle {
                                                color: parent.highlighted ? "#1D4672" : "#102137"
                                            }
                                        }
                                        popup: Popup {
                                            y: voiceSelector.height + 4
                                            width: voiceSelector.width
                                            implicitHeight: Math.min(contentItem.implicitHeight + 6, 330)
                                            padding: 3
                                            background: Rectangle {
                                                color: "#102137"
                                                radius: 10
                                                border.color: "#2E6FA7"
                                            }
                                            contentItem: ListView {
                                                clip: true
                                                implicitHeight: contentHeight
                                                model: voiceSelector.popup.visible ? voiceSelector.delegateModel : null
                                                currentIndex: voiceSelector.highlightedIndex
                                                ScrollIndicator.vertical: ScrollIndicator {}
                                            }
                                        }
                                    }
                                }
                                Rectangle {
                                    visible: ember.voiceEngine === 1
                                    Layout.fillWidth: true
                                    implicitHeight: 243
                                    color: window.cardColor
                                    radius: 13
                                    border.color: window.borderColor
                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 16
                                        spacing: 9
                                        Text {
                                            text: "LOCAL SPEECH API · 127.0.0.1:43822"
                                            color: window.mint
                                            font.pixelSize: 11
                                            font.letterSpacing: 1
                                        }
                                        RowLayout {
                                            spacing: 14
                                            Layout.fillWidth: true
                                            Text {
                                                text: "Model alias"
                                                Layout.preferredWidth: 94
                                                color: window.silver
                                                font.pixelSize: 13
                                            }
                                            TextField {
                                                id: externalModel
                                                Layout.fillWidth: true
                                                enabled: !ember.busy && !ember.speaking && !ember.speechServerRunning
                                                text: ember.localModel
                                                maximumLength: 80
                                                color: "#EAF5FF"
                                                placeholderText: "qwen3-tts-0.6b"
                                                font.pixelSize: 13
                                                onEditingFinished: ember.localModel = text
                                                background: Rectangle {
                                                    color: "#0A192D"
                                                    radius: 9
                                                    border.color: "#254A73"
                                                }
                                            }
                                        }
                                        RowLayout {
                                            spacing: 14
                                            Layout.fillWidth: true
                                            Text {
                                                text: "Voice ID"
                                                Layout.preferredWidth: 94
                                                color: window.silver
                                                font.pixelSize: 13
                                            }
                                            TextField {
                                                id: externalVoice
                                                Layout.fillWidth: true
                                                enabled: !ember.busy && !ember.speaking
                                                text: ember.localVoice
                                                maximumLength: 80
                                                color: "#EAF5FF"
                                                placeholderText: "vivian"
                                                font.pixelSize: 13
                                                onEditingFinished: ember.localVoice = text
                                                background: Rectangle {
                                                    color: "#0A192D"
                                                    radius: 9
                                                    border.color: "#254A73"
                                                }
                                            }
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 9
                                            Text {
                                                text: "Female presets"
                                                color: "#9AB8CF"
                                                font.pixelSize: 11
                                                Layout.fillWidth: true
                                            }
                                            PillButton {
                                                text: "Vivian · bright"
                                                implicitWidth: 146
                                                implicitHeight: 32
                                                enabled: !ember.busy && !ember.speaking
                                                onClicked: {
                                                    ember.localVoice = "vivian"
                                                    externalVoice.text = "vivian"
                                                }
                                            }
                                            PillButton {
                                                text: "Serena · warm"
                                                implicitWidth: 144
                                                implicitHeight: 32
                                                enabled: !ember.busy && !ember.speaking
                                                onClicked: {
                                                    ember.localVoice = "serena"
                                                    externalVoice.text = "serena"
                                                }
                                            }
                                        }
                                        Text {
                                            text: "Use any compatible local TTS server on this port, or start native speech.cpp below. PCM16 WAV only; no redirects or remote hosts."
                                            color: "#86A4BD"
                                            font.pixelSize: 11
                                            Layout.fillWidth: true
                                            wrapMode: Text.WordWrap
                                        }
                                    }
                                }
                                Rectangle {
                                    visible: ember.voiceEngine === 1
                                    Layout.fillWidth: true
                                    implicitHeight: 227
                                    radius: 14
                                    color: window.cardColor
                                    border.color: window.borderColor
                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 15
                                        spacing: 11
                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 8
                                            Text {
                                                text: "NATIVE speech.cpp · VULKAN"
                                                color: window.mint
                                                font.pixelSize: 11
                                                font.letterSpacing: 0.9
                                                Layout.fillWidth: true
                                            }
                                            Rectangle {
                                                width: 7
                                                height: 7
                                                radius: 4
                                                color: ember.speechServerReady ? "#7DCAFE" : "#4A6077"
                                            }
                                            Text {
                                                text: ember.speechServerReady ? "Ready" :
                                                      ember.speechServerRunning ? "Loading" : "Stopped"
                                                color: "#A9C9E4"
                                                font.pixelSize: 12
                                            }
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            text: "Optional native Qwen3 speech service. After Ready, sound streams into the selected Windows device without a full WAV download. First model start may download licensed weights; inference stays on loopback."
                                            wrapMode: Text.WordWrap
                                            color: "#8EABC5"
                                            font.pixelSize: 12
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 10
                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 3
                                                Text {
                                                    text: "EARLY SENTENCE SPEECH"
                                                    color: "#C9E3F5"
                                                    font.pixelSize: 11
                                                    font.letterSpacing: 0.8
                                                }
                                                Text {
                                                    text: "Faster first phrase, but LLM/TTS share GPU headroom."
                                                    color: "#86A5BE"
                                                    font.pixelSize: 11
                                                }
                                            }
                                            PillButton {
                                                text: ember.earlySpeechEnabled ? "Enabled" : "Disabled"
                                                implicitWidth: 112
                                                implicitHeight: 34
                                                enabled: !ember.busy && !ember.listening && !ember.speaking
                                                onClicked: ember.earlySpeechEnabled = !ember.earlySpeechEnabled
                                            }
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Item { Layout.fillWidth: true }
                                            PillButton {
                                                text: ember.speechServerRunning ? "Stop native TTS" : "Start native TTS"
                                                implicitWidth: 170
                                                implicitHeight: 38
                                                enabled: ember.speechServerRunning || !ember.busy
                                                onClicked: {
                                                    if (ember.speechServerRunning) {
                                                        ember.stopSpeechServer()
                                                    } else {
                                                        ember.localModel = externalModel.text.trim()
                                                        ember.localVoice = externalVoice.text.trim()
                                                        ember.startSpeechServer()
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                                Text {
                                    visible: ember.voiceEngine === 0
                                    Layout.fillWidth: true
                                    text: "All 11 speaker names and IDs come from the official Kokoro English v0.19 pack. Compare female voices with Test voice. No weights are bundled."
                                    wrapMode: Text.WordWrap
                                    color: "#8A9FAD"
                                    font.pixelSize: 12
                                }
                                Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: 210
                                    radius: 14
                                    color: window.cardColor
                                    border.color: window.borderColor
                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 16
                                        spacing: 11
                                        Text {
                                            text: "VOICE COMPARISON SCRIPT"
                                            font.pixelSize: 11
                                            font.letterSpacing: 1
                                            color: window.mint
                                        }
                                        TextArea {
                                            id: voiceSampleEditor
                                            Layout.fillWidth: true
                                            Layout.fillHeight: true
                                            text: ember.voiceSample
                                            color: "#F0F8F6"
                                            font.pixelSize: 13
                                            wrapMode: TextEdit.Wrap
                                            background: Rectangle { radius: 9; color: "#090F1D" }
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Text {
                                                text: "Compare female voices with the same English script. Sound quality requires real listening tests."
                                                color: "#819CAA"
                                                font.pixelSize: 11
                                                Layout.fillWidth: true
                                                wrapMode: Text.WordWrap
                                            }
                                            PillButton {
                                                text: "Save sample"
                                                implicitWidth: 132
                                                implicitHeight: 34
                                                enabled: voiceSampleEditor.text.trim().length > 0 &&
                                                         voiceSampleEditor.text.length <= 1200
                                                onClicked: ember.voiceSample = voiceSampleEditor.text
                                            }
                                        }
                                    }
                                }
                                Item { Layout.preferredHeight: 22 }
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: "EMBER • NATIVE C++ / QT QUICK"
                        color: "#758B9A"
                        font.pixelSize: 10
                        font.letterSpacing: 1.2
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: "LOCAL FIRST · MODULAR BY DESIGN"
                        color: "#758B9A"
                        font.pixelSize: 10
                        font.letterSpacing: 1.2
                    }
                }
            }
        }
    }
}
