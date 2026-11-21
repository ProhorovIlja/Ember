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
    color: "#0B1019"
    font.family: "Segoe UI"
    property int page: 0
    property color mint: "#80F1C3"
    property color silver: "#B2C2D1"
    property color borderColor: "#293749"
    property color cardColor: "#151E2B"
    Dialog {
        id: deleteChatDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
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
        contentItem: Text {
            width: 340
            text: "Only this conversation will be deleted from the local SQLite archive. The other chats will remain available."
            wrapMode: Text.WordWrap
            font.pixelSize: 13
            color: "#D8EBFB"
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
        color: window.page === index ? "#234138" : (hover.hovered ? "#182633" : "transparent")
        border.width: window.page === index ? 1 : 0
        border.color: "#416C62"
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
            Text { text: label; color: window.page === index ? "#E5FFF3" : "#B9C8D9"; font.pixelSize: 15; font.weight: window.page === index ? Font.DemiBold : Font.Normal }
        }
    }
    component PillButton: Button {
        id: pill
        property color fill: "#263E39"
        property color ink: "#DAFFF0"
        font.pixelSize: 14
        font.weight: Font.DemiBold
        implicitHeight: 44
        implicitWidth: 138
        background: Rectangle {
            radius: 13
            color: !pill.enabled ? "#29343D" : (pill.down ? "#38655A" : (pill.hovered ? "#315A50" : pill.fill))
            border.color: !pill.enabled ? "#36404B" : "#497669"
            border.width: 1
            Behavior on color { ColorAnimation { duration: 140 } }
        }
        contentItem: Text {
            text: pill.text
            color: pill.enabled ? pill.ink : "#78908F"
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
            color: "#101823"
            border.color: "#202C3A"
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
                            GradientStop { position: 0; color: "#224C43" }
                            GradientStop { position: 1; color: "#122830" }
                        }
                        border.color: "#4D9B84"
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
                        Text { text: "EMBER"; color: "#ECFFF7"; font.pixelSize: 19; font.bold: true; font.letterSpacing: 2 }
                        Text { text: "LOCAL INTELLIGENCE"; color: "#809C9D"; font.pixelSize: 9; font.letterSpacing: 1.2 }
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#253341"; Layout.topMargin: 21; Layout.bottomMargin: 21 }
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
                    ListView {
                        id: historyList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 5
                        model: ember.sessions
                        boundsBehavior: Flickable.StopAtBounds
                        delegate: Rectangle {
                            required property var modelData
                            width: historyList.width
                            height: 49
                            radius: 10
                            color: modelData.id === ember.currentSessionId
                                   ? "#133352" : (historyHover.hovered ? "#0C1D31" : "transparent")
                            border.color: modelData.id === ember.currentSessionId
                                          ? "#2B6EA6" : "transparent"
                            border.width: 1
                            Behavior on color { ColorAnimation { duration: 110 } }
                            Text {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 9
                                verticalAlignment: Text.AlignVCenter
                                text: modelData.title
                                elide: Text.ElideRight
                                color: modelData.id === ember.currentSessionId
                                       ? "#EAF7FF" : "#A6BED2"
                                font.pixelSize: 12
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
                    Text { text: "ON THIS MACHINE"; color: "#7892A4"; font.pixelSize: 10; font.letterSpacing: 1.3 }
                    RowLayout {
                        spacing: 9
                        Rectangle {
                            width: 8; height: 8; radius: 4
                            color: ember.brainReady ? window.mint : "#CA9477"
                        }
                        Text {
                            text: ember.brainReady ? "Brain online" : "Brain offline"
                            color: window.silver
                            font.pixelSize: 13
                        }
                    }
                    Text {
                        text: "Private by design. No hosted AI endpoints."
                        color: "#8298AA"
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
            color: "#0C121D"
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
                            font.pixelSize: 26
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
                    color: "#192632"
                    border.color: "#314957"
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        spacing: 12
                        Rectangle {
                            width: 8; height: 8; radius: 4
                            color: ember.busy ? "#F2C38A" : (ember.brainReady ? window.mint : "#B78377")
                        }
                        Text {
                            Layout.fillWidth: true
                            text: ember.status
                            color: "#BBD1D9"
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
                                    border.color: ember.listening ? "#69F3BA" : "#315A56"
                                    border.width: ember.listening ? 2 : 1
                                    Behavior on border.color { ColorAnimation { duration: 250 } }
                                    Rectangle {
                                        x: 16; y: 32; width: 10; height: 10; radius: 5
                                        color: "#83F3BE"
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
                                    color: "#182F36"
                                    border.color: "#326D62"
                                    border.width: 1
                                    gradient: Gradient {
                                        GradientStop { position: 0; color: "#315D54" }
                                        GradientStop { position: 0.48; color: "#1A383D" }
                                        GradientStop { position: 1; color: "#101E30" }
                                    }
                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: 138; height: 138; radius: 69
                                        color: "#10252B"
                                        border.color: "#4FAF91"
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
                                color: "#ECFFF6"
                                font.pixelSize: 23
                                font.weight: Font.DemiBold
                            }
                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.maximumWidth: 540
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                                text: "Push to talk. Your microphone is idle until you start, and recording stops after 30 seconds."
                                color: "#93A7B8"
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
                                    Text { text: "LAST HEARD"; color: "#85AFA6"; font.pixelSize: 10; font.letterSpacing: 1.2 }
                                    Text {
                                        width: parent.width
                                        text: ember.transcript.length ? ember.transcript : "Your words will appear here once transcribed."
                                        color: ember.transcript.length ? "#DDEDE8" : "#879CAB"
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
                                text: "The first voice pipeline is turn-based. Low-latency conversational audio is a later milestone."
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
                                Text { text: "LOCAL CONVERSATION"; color: "#8EACA8"; font.pixelSize: 11; font.letterSpacing: 1.2 }
                                Item { Layout.fillWidth: true }
                                PillButton {
                                    text: "Stop reply"
                                    implicitWidth: 119
                                    implicitHeight: 36
                                    enabled: ember.busy
                                    visible: ember.busy
                                    onClicked: ember.cancelWork()
                                }
                                PillButton {
                                    text: "Delete"
                                    implicitWidth: 84
                                    implicitHeight: 36
                                    enabled: ember.localHistoryReady
                                    onClicked: deleteChatDialog.open()
                                }
                                PillButton {
                                    text: "New conversation"
                                    implicitWidth: 166
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
                                        color: modelData.role === "you" ? "#23433D" : "#172434"
                                        border.color: modelData.role === "you" ? "#42796C" : "#2F4056"
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
                                        enabled: ember.brainReady && !ember.busy
                                        color: "#ECF4F8"
                                        font.pixelSize: 14
                                        wrapMode: TextEdit.Wrap
                                        placeholderText: ember.brainReady ? "Ask Ember anything…" : "Start the local brain to write a message"
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
                                    color: "#173029"
                                    border.color: "#356D57"
                                    Column {
                                        anchors.fill: parent
                                        anchors.margins: 17
                                        spacing: 6
                                        Text { text: "PORTABLE LOCAL SETUP"; color: window.mint; font.pixelSize: 12; font.bold: true; font.letterSpacing: 1 }
                                        Text {
                                            width: parent.width
                                            text: "Default installation: D:\\Games\\Ember. Choose trusted local engines and model files. Nothing downloads automatically."
                                            color: "#D2E9DF"
                                            wrapMode: Text.WordWrap
                                            font.pixelSize: 13
                                        }
                                    }
                                }
                                Text { text: "MODEL ROUTING"; color: "#DAE4EE"; font.pixelSize: 16; font.weight: Font.DemiBold }
                                Repeater {
                                    model: [
                                        { key: "brainExe", label: "llama-server executable" },
                                        { key: "brainModel", label: "Language model · GGUF" },
                                        { key: "asrExe", label: "whisper-cli executable" },
                                        { key: "asrModel", label: "Speech recognition weights" },
                                        { key: "ttsExe", label: "sherpa-onnx TTS executable" },
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
                                Text {
                                    text: "AUDIO ROUTING"
                                    color: "#DAE4EE"
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
                                                    color: "#152C30"
                                                    border.color: "#416A63"
                                                }
                                                contentItem: Text {
                                                    leftPadding: 14
                                                    verticalAlignment: Text.AlignVCenter
                                                    color: "#E8F7EE"
                                                    elide: Text.ElideRight
                                                    font.pixelSize: 13
                                                    text: microphonePicker.displayText
                                                }
                                                popup: Popup {
                                                    y: microphonePicker.height + 4
                                                    width: microphonePicker.width
                                                    implicitHeight: Math.min(contentItem.implicitHeight + 6, 280)
                                                    padding: 3
                                                    background: Rectangle { color: "#17252F"; radius: 10; border.color: "#49796B" }
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
                                                        color: "#EAF5F0"
                                                        font: parent.font
                                                        verticalAlignment: Text.AlignVCenter
                                                        elide: Text.ElideRight
                                                    }
                                                    background: Rectangle { color: parent.highlighted ? "#31574F" : "#17252F" }
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
                                                    color: "#152C30"
                                                    border.color: "#416A63"
                                                }
                                                contentItem: Text {
                                                    leftPadding: 14
                                                    verticalAlignment: Text.AlignVCenter
                                                    color: "#E8F7EE"
                                                    elide: Text.ElideRight
                                                    font.pixelSize: 13
                                                    text: speakerPicker.displayText
                                                }
                                                popup: Popup {
                                                    y: speakerPicker.height + 4
                                                    width: speakerPicker.width
                                                    implicitHeight: Math.min(contentItem.implicitHeight + 6, 280)
                                                    padding: 3
                                                    background: Rectangle { color: "#17252F"; radius: 10; border.color: "#49796B" }
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
                                                        color: "#EAF5F0"
                                                        font: parent.font
                                                        verticalAlignment: Text.AlignVCenter
                                                        elide: Text.ElideRight
                                                    }
                                                    background: Rectangle { color: parent.highlighted ? "#31574F" : "#17252F" }
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
                                Text { text: "PERSONALITY & VOICE"; color: "#DAE4EE"; font.pixelSize: 16; font.weight: Font.DemiBold; Layout.topMargin: 8 }
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
                                            background: Rectangle { radius: 8; color: "#0F1925" }
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
                                        color: "#A7BBC8"
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
                                        onActivated: ember.voiceEngine = currentIndex
                                        contentItem: Text {
                                            leftPadding: 14
                                            text: enginePicker.displayText
                                            color: "#E7F6ED"
                                            font.pixelSize: 13
                                            verticalAlignment: Text.AlignVCenter
                                            elide: Text.ElideRight
                                        }
                                        background: Rectangle {
                                            color: "#172B31"
                                            radius: 12
                                            border.color: "#437B6C"
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
                                                color: "#E6F4EC"
                                                font.pixelSize: 13
                                                verticalAlignment: Text.AlignVCenter
                                            }
                                            background: Rectangle {
                                                color: parent.highlighted ? "#31574F" : "#17252F"
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
                                        color: "#A7BBC8"
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
                                        onActivated: ember.voiceId = currentIndex
                                        contentItem: Text {
                                            text: voiceSelector.displayText
                                            color: "#E7F6ED"
                                            font.pixelSize: 13
                                            leftPadding: 14
                                            verticalAlignment: Text.AlignVCenter
                                            elide: Text.ElideRight
                                        }
                                        background: Rectangle {
                                            color: "#172B31"
                                            radius: 12
                                            border.width: 1
                                            border.color: "#437B6C"
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
                                                color: "#E6F4EC"
                                            }
                                            background: Rectangle {
                                                color: parent.highlighted ? "#31574F" : "#17252F"
                                            }
                                        }
                                        popup: Popup {
                                            y: voiceSelector.height + 4
                                            width: voiceSelector.width
                                            implicitHeight: Math.min(contentItem.implicitHeight + 6, 330)
                                            padding: 3
                                            background: Rectangle {
                                                color: "#17252F"
                                                radius: 10
                                                border.color: "#49796B"
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
                                    implicitHeight: 189
                                    color: window.cardColor
                                    radius: 13
                                    border.color: window.borderColor
                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 16
                                        spacing: 9
                                        Text {
                                            text: "EXTERNAL LOCAL ENGINE · 127.0.0.1:43822"
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
                                                text: ember.localModel
                                                maximumLength: 80
                                                color: "#ECF8F3"
                                                placeholderText: "qwen3-tts"
                                                font.pixelSize: 13
                                                onEditingFinished: ember.localModel = text
                                                background: Rectangle {
                                                    color: "#0C1A24"
                                                    radius: 9
                                                    border.color: "#3D5D61"
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
                                                text: ember.localVoice
                                                maximumLength: 80
                                                color: "#ECF8F3"
                                                placeholderText: "Vivian"
                                                font.pixelSize: 13
                                                onEditingFinished: ember.localVoice = text
                                                background: Rectangle {
                                                    color: "#0C1A24"
                                                    radius: 9
                                                    border.color: "#3D5D61"
                                                }
                                            }
                                        }
                                        Text {
                                            text: "Start your own OpenAI-compatible TTS server on 43822. Output must be PCM16 WAV; redirects and remote hosts are blocked."
                                            color: "#8EA7B4"
                                            font.pixelSize: 11
                                            Layout.fillWidth: true
                                            wrapMode: Text.WordWrap
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
                                            background: Rectangle { radius: 9; color: "#0F1B27" }
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Text {
                                                text: "Use the same script for each voice; Russian requires a multilingual model."
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
