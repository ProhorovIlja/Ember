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
    property int page: 1
    property color mint: "#77C4FF"
    property color silver: "#B2C2D1"
    property color borderColor: "#1A2D45"
    property color cardColor: "#0A101C"
    property string historyQuery: ""
    property string imageToDelete: ""
    property string externalReferencePreview: ""
    property string externalReferenceSource: "GitHub"
    property string localEditPreview: ""
    property string offlineSetupReport: ""
    property bool preserveOlderScroll: false
    property string focusedArchiveMessageId: ""
    property string focusedArchiveSessionId: ""
    // Purely foreground button-gesture state: no recording is triggered by
    // a clock, app startup, a scheduled task or a background watcher.
    property bool dictationHoldArmed: false
    property double dictationPressStartedAt: 0
    property bool voiceHoldArmed: false
    property bool voiceStopOnRelease: false
    property double voicePressStartedAt: 0
    Dialog {
        id: offlineSetupDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(window.width - 70, 755)
        height: Math.min(window.height - 70, 540)
        modal: true
        title: "Offline setup · local readiness check"
        standardButtons: Dialog.Close
        background: Rectangle {
            color: "#091522"
            radius: 16
            border.color: "#2A6292"
        }
        contentItem: ScrollView {
            TextArea {
                readOnly: true
                selectByMouse: true
                text: window.offlineSetupReport
                font.family: "Consolas"
                font.pixelSize: 12
                color: "#DBE9F6"
                wrapMode: TextEdit.Wrap
                background: Rectangle { color: "#0B192A"; radius: 8 }
            }
        }
    }
    Dialog {
        id: localCodeEditDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(window.width - 70, 790)
        height: Math.min(window.height - 70, 570)
        modal: true
        title: window.localEditPreview.startsWith("NEW FILE BATCH")
               ? "New project files · review every file"
               : window.localEditPreview.startsWith("NEW FILE (NOT WRITTEN)")
                   ? "New source file · review before creation"
                   : "Local source edit · review and apply"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: {
            ember.applyStagedLocalCodeEdit()
            window.localEditPreview = ""
        }
        onRejected: {
            ember.discardStagedLocalCodeEdit()
            window.localEditPreview = ""
        }
        background: Rectangle {
            color: "#091522"
            radius: 16
            border.color: "#2A6292"
        }
        contentItem: ColumnLayout {
            spacing: 8
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: "#B2D6F1"
                font.pixelSize: 12
                text: window.localEditPreview.startsWith("NEW FILE BATCH")
                      ? "Create up to three NEW source files after reading the full contents of EACH? No existing file is overwritten; no folders or commands are created. A failure attempts rollback, but power loss may leave partial files."
                      : window.localEditPreview.startsWith("NEW FILE (NOT WRITTEN)")
                          ? "Create this one NEW source file after reviewing all its contents? Existing files will never be overwritten. No other files or directories will change. No commands are executed. A disk failure may leave a partial new file."
                          : "Apply this complete reviewed patch? One to three existing project source files may change, with separate private backups and per-file atomic writes. No shell/network commands. Multi-file changes are not power-failure atomic."
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                TextArea {
                    readOnly: true
                    text: window.localEditPreview
                    font.family: "Consolas"
                    font.pixelSize: 12
                    color: "#E4EDF9"
                    selectByMouse: true
                    wrapMode: TextEdit.NoWrap
                    background: Rectangle { color: "#0B192A"; radius: 8 }
                }
            }
        }
    }
    Dialog {
        id: externalReferenceDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(window.width - 70, 760)
        height: Math.min(window.height - 70, 560)
        modal: true
        title: window.externalReferenceSource + " · Review before inserting"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: {
            if (composer.text.length + window.externalReferencePreview.length > 8192) {
                externalReferenceDialog.open()
                return
            }
            composer.text += window.externalReferencePreview
            composer.cursorPosition = composer.length
            window.page = 1
        }
        background: Rectangle {
            color: "#091522"
            radius: 16
            border.color: "#2A6292"
        }
        contentItem: ColumnLayout {
            spacing: 8
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: "#9CB8D4"
                font.pixelSize: 12
                text: composer.text.length + window.externalReferencePreview.length <= 8192
                      ? "One-shot external research. Review source links and decide whether to add these excerpts to your local text prompt."
                      : "Too much text for the current draft. Remove text before inserting; the preview remains copyable."
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                TextArea {
                    readOnly: true
                    text: window.externalReferencePreview
                    font.pixelSize: 12
                    color: "#E4EDF9"
                    wrapMode: TextEdit.Wrap
                    selectByMouse: true
                    background: Rectangle { color: "#0B192A"; radius: 8 }
                }
            }
        }
    }
    Connections {
        target: ember
        function onPublicGitHubExcerptReady(excerpt) {
            window.externalReferenceSource = "GitHub"
            window.externalReferencePreview = excerpt
            externalReferenceDialog.open()
        }
        function onWikipediaExcerptReady(excerpt) {
            window.externalReferenceSource = "Wikipedia"
            window.externalReferencePreview = excerpt
            externalReferenceDialog.open()
        }
        function onDriveExcerptReady(excerpt) {
            window.externalReferenceSource = "Google Drive"
            window.externalReferencePreview = excerpt
            externalReferenceDialog.open()
        }
        function onLocalAgentResultReady(excerpt) {
            window.externalReferenceSource = "Ember research agent"
            window.externalReferencePreview = excerpt
            externalReferenceDialog.open()
        }
        function onLocalAgentEditReady(preview) {
            window.localEditPreview = preview
            localCodeEditDialog.open()
        }
        function onLocalTestResultReady(excerpt) {
            window.externalReferenceSource = "Local project test results"
            window.externalReferencePreview = excerpt
            externalReferenceDialog.open()
        }
    }

    Dialog {
        id: deleteImageDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 430
        modal: true
        title: "Delete local image?"
        standardButtons: Dialog.Yes | Dialog.Cancel
        onAccepted: {
            ember.deleteSavedImage(window.imageToDelete)
            window.imageToDelete = ""
        }
        background: Rectangle {
            color: "#091522"
            radius: 16
            border.color: "#2A6292"
        }
        contentItem: Text {
            text: "This removes the image from the on-device Library. Older chats may lose their image preview. This cannot be undone."
            color: "#D7E7F6"
            font.pixelSize: 13
            wrapMode: Text.WordWrap
            width: 360
            height: 95
        }
    }
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
        title: ember.privateMode ? "Clear private session?" : "Delete local chat?"
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
                text: ember.privateMode
                      ? "Discard this private conversation from live memory. Saved chats remain untouched."
                      : "Only this conversation will be deleted from the local SQLite archive. Other chats remain available."
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
                text: "Pin characters, relationships, world rules or project facts in this saved chat. Notes stay on your PC. Optional recall from other saved chats is controlled below."
                color: "#BACFE4"
                wrapMode: Text.WordWrap
                font.pixelSize: 13
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3
                    Text {
                        text: "Recall from other saved chats"
                        color: "#E4F2FF"
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "Off by default · local, on-demand · never used in Private Mode"
                        color: "#93B0C9"
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                }
                PillButton {
                    text: ember.crossChatRecall ? "Recall On" : "Recall Off"
                    implicitWidth: 112
                    implicitHeight: 34
                    enabled: !ember.busy && !ember.listening
                    onClicked: ember.setCrossChatRecall(!ember.crossChatRecall)
                    ToolTip.visible: hovered
                    ToolTip.delay: 700
                    ToolTip.text: "Search a bounded number of saved chat excerpts only when you send a new message. No background indexing."
                }
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
            // Dictation may be inserted in the middle of an existing story,
            // code prompt or paragraph; inspect BOTH insertion boundaries.
            // Long transcriptions are preserved even if they exceed the
            // separate per-message limit of the local LLM.
            const at = composer.cursorPosition
            const before = at > 0 ? composer.text.charAt(at - 1) : ""
            const after = at < composer.text.length ?
                          composer.text.charAt(at) : ""
            const leading = before.length > 0 && !/\s/.test(before) &&
                            !/^\s/.test(text) ? " " : ""
            const trailing = after.length > 0 && !/\s/.test(after) &&
                             !/\s$/.test(text) ? " " : ""
            const insertion = leading + text + trailing
            composer.insert(at, insertion)
            composer.cursorPosition = at + insertion.length
            composer.forceActiveFocus()
        }
        function onDictationAutoSendReady() {
            window.submitComposer()
        }
    }

    function submitComposer() {
        const value = composer.text.trim()
        if ((value.length === 0 && ember.pendingImageUrl.length === 0)
            || !ember.brainReady || ember.busy) return
        // The C++ backend returns true ONLY after validating the trusted
        // endpoint and image payload and initiating the local HTTP stream.
        // Keep the user's draft on every rejected send (including recording,
        // corrupt Library input or a replaced loopback service).
        if (ember.submitText(value))
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
                    // Fully native route; no mic or inference starts on navigation.
                    // Default startup remains Chat until engines are configured.
                    NavButton { label: "Voice"; symbol: "◉"; index: 0; objectName: "emberVoiceNav" }
                    NavButton { label: "Chat"; symbol: "▤"; index: 1 }
                    NavButton { label: "Library"; symbol: "▧"; index: 3 }
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
                            text: ember.privateMode ? "PRIVATE SESSION / LOCAL HISTORY" : "LOCAL HISTORY"
                            color: "#8099AC"
                            font.pixelSize: 10
                            font.letterSpacing: 1.4
                            font.weight: Font.DemiBold
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: ember.privateMode ? "RAM" : ember.localHistoryReady ? "SQLite" : "Memory"
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
                            height: window.historyQuery.length > 0 ? 91 : 49
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
                                anchors.topMargin: 35
                                height: 44
                                text: modelData.snippet || "Title match"
                                wrapMode: Text.WordWrap
                                elide: Text.ElideRight
                                color: "#8FAFC9"
                                font.pixelSize: 11
                                maximumLineCount: 2
                                clip: true
                                ToolTip.visible: window.historyQuery.length > 0 &&
                                                 historyHover.hovered &&
                                                 (modelData.snippet || "").length > 0
                                ToolTip.delay: 650
                                ToolTip.text: modelData.snippet || ""
                            }
                            HoverHandler { id: historyHover }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    const selectedSession = modelData.id
                                    const hitId = modelData.messageId || "0"
                                    const shouldJump = window.historyQuery.length > 0 &&
                                                       hitId !== "0"
                                    window.focusedArchiveMessageId = ""
                                    window.focusedArchiveSessionId = ""
                                    if (!shouldJump) {
                                        ember.openSession(selectedSession)
                                        window.page = 1
                                        return
                                    }
                                    const index = ember.openSessionAtMessage(selectedSession, hitId)
                                    if (index < 0) return
                                    window.focusedArchiveMessageId = hitId
                                    window.focusedArchiveSessionId = selectedSession
                                    window.page = 1
                                    Qt.callLater(function() {
                                        if (window.page === 1 &&
                                            ember.currentSessionId === selectedSession)
                                            timeline.positionViewAtIndex(index, ListView.Center)
                                    })
                                }
                            }
                        }
                    }
                    PillButton {
                        Layout.fillWidth: true
                        text: "Load older conversations"
                        implicitHeight: 31
                        visible: ember.hasOlderSessions &&
                                 window.historyQuery.length === 0 &&
                                 !ember.privateMode
                        enabled: ember.localHistoryReady
                        onClicked: ember.loadOlderSessions()
                        ToolTip.visible: hovered
                        ToolTip.delay: 700
                        ToolTip.text: "Page through all saved sessions, 80 at a time. No cloud, no deletion."
                    }
                    PillButton {
                        Layout.fillWidth: true
                        text: "Export selected chat"
                        implicitHeight: 37
                        enabled: ember.localHistoryReady && ember.currentSessionId.length > 0 && !ember.busy
                        onClicked: ember.exportCurrentSession()
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        PillButton {
                            Layout.fillWidth: true
                            text: "Backup chats + images"
                            implicitHeight: 36
                            enabled: ember.localHistoryReady && !ember.privateMode
                                     && !ember.busy && !ember.listening && !ember.speaking
                            onClicked: ember.exportCompleteHistory()
                        }
                        PillButton {
                            Layout.fillWidth: true
                            text: "Restore full"
                            implicitHeight: 36
                            enabled: ember.localHistoryReady && !ember.privateMode
                                     && !ember.busy && !ember.listening && !ember.speaking
                            onClicked: ember.restoreCompleteHistory()
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        PillButton {
                            Layout.fillWidth: true
                            text: "Backup text"
                            implicitHeight: 36
                            enabled: ember.localHistoryReady && !ember.privateMode
                                     && !ember.busy && !ember.listening && !ember.speaking
                            onClicked: ember.exportCompactHistory()
                        }
                        PillButton {
                            Layout.fillWidth: true
                            text: "Restore"
                            implicitHeight: 36
                            enabled: ember.localHistoryReady && !ember.privateMode
                                     && !ember.busy && !ember.listening && !ember.speaking
                            onClicked: ember.restoreCompactHistory()
                        }
                    }
                    PillButton {
                        Layout.fillWidth: true
                        text: ember.privateMode ? "Private session · END" : "Start private session"
                        implicitHeight: 36
                        enabled: !ember.busy && !ember.listening
                        onClicked: {
                            ember.privateMode = !ember.privateMode
                            window.page = 1
                        }
                    }
                    Text {
                        text: ember.privateMode
                              ? "Private text stays in RAM. Voice recognition still uses temporary local WAV files."
                              : "Full backup includes Library images; text-only backups remain compatible. Neither format is encrypted."
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
                            color: ember.brainReady &&
                                   ((ember.voiceEngine !== 1 &&
                                     ember.recognitionEngine === 0) ||
                                    ember.speechServerReady)
                                   ? window.mint : "#FFB97D"
                        }
                        Text {
                            text: ember.brainReady
                                  ? (((ember.voiceEngine === 1 ||
                                       ember.recognitionEngine > 0) &&
                                      !ember.speechServerReady)
                                     ? "Brain ready · voice still loading"
                                     : (ember.voiceEngine === 0 &&
                                        ember.recognitionEngine === 0
                                        ? "Text brain ready · voice loads on demand"
                                        : "Ember local engines ready"))
                                  : ((ember.brainLaunching || ember.speechServerRunning)
                                     ? "Starting Ember locally" : "Ember offline")
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
                                  window.page === 1 ? "A conversation with Ember" :
                                  window.page === 3 ? "Your private image Library" : "Make Ember yours"
                            font.pixelSize: 27
                            font.family: "Bahnschrift"
                            font.weight: Font.DemiBold
                            color: "#F3F7FE"
                        }
                        Text {
                            text: window.page === 0 ? "English voice · modular local engines · no browser wrapper" :
                                  window.page === 1
                                  ? (ember.privateMode
                                      ? "PRIVATE SESSION · RAM-ONLY TEXT · NO CHAT ARCHIVE"
                                      : "Persistent on-device history · RU/EN dictation · no cloud sync") :
                                  window.page === 3 ? "Images are normalized and stored only on this PC" :
                                  "Modular engines, voices and personality"
                            font.pixelSize: 12
                            color: "#92A5B8"
                        }
                    }
                    Item { Layout.fillWidth: true }
                    PillButton {
                        text: (ember.brainReady || ember.brainLaunching ||
                               ember.speechServerRunning) ? "Stop Ember" : "Start Ember"
                        // Stop remains available during transcription so
                        // a user can explicitly terminate local engines.
                        ToolTip.visible: hovered
                        ToolTip.delay: 700
                        ToolTip.text: "One click starts or stops the local brain and, when selected, native voice/ASR. No cloud model and no automatic startup."
                        onClicked: {
                            if (ember.brainReady || ember.brainLaunching ||
                                ember.speechServerRunning)
                                ember.stopLocalEngines()
                            else
                                ember.startLocalEngines()
                        }
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
                    objectName: "emberViews"
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
                                text: "Tap to record until Stop, or hold and release to ask Ember. Cancel discards audio. Voice stays local."
                                color: "#94AFC8"
                                font.pixelSize: 13
                            }
                            RowLayout {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.maximumWidth: 670
                                visible: !ember.brainReady
                                spacing: 12
                                Text {
                                    Layout.preferredWidth: 320
                                    color: "#E7B89E"
                                    font.pixelSize: 12
                                    wrapMode: Text.WordWrap
                                    text: "Voice needs your local text model and an installed ASR/TTS engine. Nothing is downloaded automatically."
                                }
                                PillButton {
                                    text: "Configure offline"
                                    implicitWidth: 168
                                    implicitHeight: 35
                                    onClicked: window.page = 2
                                    ToolTip.visible: hovered
                                    ToolTip.text: "Select your trusted local GGUF, Whisper and voice models in Settings. No Internet installation is triggered."
                                }
                            }
                            ColumnLayout {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.preferredWidth: 440
                                spacing: 6
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        text: "MIC INPUT"
                                        color: "#74A4CA"
                                        font.pixelSize: 10
                                        font.letterSpacing: 1.1
                                    }
                                    Rectangle {
                                        Layout.fillWidth: true
                                        height: 7
                                        radius: 4
                                        color: "#071829"
                                        border.color: "#1A3C5D"
                                        Rectangle {
                                            anchors.left: parent.left
                                            anchors.top: parent.top
                                            anchors.bottom: parent.bottom
                                            width: (parent.width - 2) *
                                                   (ember.microphoneLevel / 100)
                                            radius: 4
                                            color: "#60B4F5"
                                            Behavior on width { NumberAnimation { duration: 90 } }
                                        }
                                    }
                                    Text {
                                        text: ember.listening ? ember.microphoneLevel + "%" : "Idle"
                                        color: "#9FC8E6"
                                        font.pixelSize: 11
                                    }
                                }
                                PillButton {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: ember.autoEndOnSilence
                                        ? "Experimental auto-finish: ON"
                                        : "Experimental auto-finish: OFF"
                                    implicitWidth: 251
                                    implicitHeight: 31
                                    enabled: !ember.listening
                                    onClicked: ember.autoEndOnSilence = !ember.autoEndOnSilence
                                    ToolTip.visible: hovered
                                    ToolTip.text: "Optional energy-based pause ending. Quiet speech and noise may confuse it; disabled by default."
                                }
                            }
                            RowLayout {
                                Layout.alignment: Qt.AlignHCenter
                                spacing: 13
                                PillButton {
                                    id: voiceRecordControl
                                    text: ember.listening ?
                                          (window.voiceHoldArmed ?
                                           "Release to ask" : "Stop recording") :
                                          ember.speaking ?
                                          "Interrupt & record" : "Tap / hold to speak"
                                    implicitWidth: 190
                                    enabled: ember.listening || window.voiceHoldArmed ||
                                             (ember.brainReady &&
                                              (!ember.busy || ember.speaking))
                                    onPressed: {
                                        if (ember.listening) {
                                            window.voiceStopOnRelease = true
                                            return
                                        }
                                        if (!ember.brainReady ||
                                            (ember.busy && !ember.speaking)) return
                                        window.voiceHoldArmed = true
                                        window.voicePressStartedAt = Date.now()
                                        ember.startRecording()
                                        if (!ember.listening)
                                            window.voiceHoldArmed = false
                                    }
                                    onReleased: {
                                        if (window.voiceStopOnRelease) {
                                            window.voiceStopOnRelease = false
                                            ember.stopRecording()
                                            return
                                        }
                                        if (!window.voiceHoldArmed) return
                                        const elapsed = Date.now() -
                                                        window.voicePressStartedAt
                                        const complete =
                                            ember.shouldCompleteHeldVoice(elapsed)
                                        window.voiceHoldArmed = false
                                        if (complete) ember.stopRecording()
                                    }
                                    onCanceled: {
                                        if (window.voiceHoldArmed && ember.listening)
                                            ember.cancelWork()
                                        window.voiceHoldArmed = false
                                        window.voiceStopOnRelease = false
                                    }
                                    ToolTip.visible: hovered
                                    ToolTip.delay: 700
                                    ToolTip.text: "Tap once to record and again to stop. Hold for 0.45 seconds and release to transcribe/submit. Drag outside to cancel the held take. Needs local LLM and ASR/TTS."
                                }
                                PillButton {
                                    text: ember.speaking ? "Retest voice" : "Test voice"
                                    implicitWidth: 128
                                    enabled: !ember.listening &&
                                             (!ember.busy || ember.speaking)
                                    onClicked: ember.testVoice()
                                    ToolTip.visible: hovered
                                    ToolTip.text: "Replay the voice sample; interrupts current voice output. Does not interrupt active transcription or LLM generation."
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
                                    text: ember.deepReasoning ? "Deep · On" : "Deep · Off"
                                    implicitWidth: 102
                                    implicitHeight: 36
                                    enabled: !ember.busy && !ember.listening
                                    onClicked: ember.setDeepReasoning(!ember.deepReasoning)
                                    ToolTip.visible: hovered
                                    ToolTip.delay: 700
                                    ToolTip.text: "Opt in to longer local thinking on compatible Qwen3 GGUF models. Uses extra tokens and GPU time, not ChatGPT usage."
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
                                    enabled: ember.localHistoryReady && !ember.privateMode
                                    onClicked: continuityDialog.open()
                                }
                                PillButton {
                                    text: ember.privateMode ? "Clear private" : "Delete"
                                    implicitWidth: ember.privateMode ? 122 : 84
                                    implicitHeight: 36
                                    enabled: ember.privateMode || ember.localHistoryReady
                                    onClicked: deleteChatDialog.open()
                                }
                                PillButton {
                                    text: window.width < 1100 ? "New chat" : "New conversation"
                                    implicitWidth: window.width < 1100 ? 105 : 166
                                    implicitHeight: 36
                                    onClicked: ember.clearConversation()
                                }
                            }
                            PillButton {
                                Layout.fillWidth: true
                                text: "Load earlier messages"
                                implicitHeight: 31
                                visible: ember.hasOlderMessages && !ember.privateMode
                                enabled: !ember.busy && !ember.listening && !ember.speaking
                                onClicked: {
                                    window.preserveOlderScroll = true
                                    ember.loadEarlierMessages()
                                }
                                ToolTip.visible: hovered
                                ToolTip.delay: 700
                                ToolTip.text: "Retrieve the next 100 archived messages from this chat. No records are erased."
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
                                        border.color: window.focusedArchiveSessionId === ember.currentSessionId &&
                                                      window.focusedArchiveMessageId.length > 0 &&
                                                      String(modelData.id || "") === window.focusedArchiveMessageId
                                                      ? "#77C4FF"
                                                      : (modelData.role === "you" ? "#3576AF" : "#2F4056")
                                        border.width: window.focusedArchiveSessionId === ember.currentSessionId &&
                                                      window.focusedArchiveMessageId.length > 0 &&
                                                      String(modelData.id || "") === window.focusedArchiveMessageId ? 2 : 1
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
                                                text: modelData.role === "ember"
                                                    ? ember.formatReply(modelData.body)
                                                    : ember.messageText(modelData.body)
                                                textFormat: modelData.role === "ember"
                                                    ? Text.RichText : Text.PlainText
                                                wrapMode: Text.Wrap
                                                color: "#EEF3F9"
                                                font.pixelSize: 14
                                                lineHeight: 1.3
                                            }
                                            Image {
                                                id: chatImagePreview
                                                width: parent.width
                                                height: visible ? 174 : 0
                                                source: modelData.role === "you"
                                                    ? ember.messageImageUrl(modelData.body) : ""
                                                visible: source.toString().length > 0
                                                sourceSize.width: 780
                                                sourceSize.height: 780
                                                fillMode: Image.PreserveAspectFit
                                                asynchronous: true
                                                smooth: true
                                            }
                                            Text {
                                                width: parent.width
                                                visible: modelData.role === "you" &&
                                                    modelData.body.indexOf("[Ember image sha256:") >= 0 &&
                                                    chatImagePreview.source.toString().length === 0
                                                text: "Attached image is unavailable in the local Library."
                                                color: "#F0B6A1"
                                                font.pixelSize: 11
                                                wrapMode: Text.WordWrap
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
                                    function onMessagesChanged() {
                                        if (window.preserveOlderScroll) {
                                            timeline.positionViewAtBeginning()
                                            window.preserveOlderScroll = false
                                        } else {
                                            timeline.positionViewAtEnd()
                                        }
                                    }
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                visible: ember.hasNewerMessages && !ember.privateMode
                                PillButton {
                                    Layout.fillWidth: true
                                    text: "Load newer messages"
                                    implicitHeight: 32
                                    enabled: !ember.busy && !ember.listening && !ember.speaking
                                    onClicked: ember.loadNewerMessages()
                                    ToolTip.visible: hovered
                                    ToolTip.delay: 600
                                    ToolTip.text: "Load up to 100 newer saved messages from this conversation."
                                }
                                PillButton {
                                    Layout.fillWidth: true
                                    text: "Latest messages"
                                    implicitHeight: 32
                                    enabled: !ember.busy && !ember.listening && !ember.speaking
                                    onClicked: {
                                        window.focusedArchiveMessageId = ""
                                        window.focusedArchiveSessionId = ""
                                        ember.showLatestMessages()
                                    }
                                    ToolTip.visible: hovered
                                    ToolTip.delay: 600
                                    ToolTip.text: "Return to recent messages without deleting the old archive."
                                }
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: ember.pendingImageUrl.length > 0 ? 267 : 182
                                color: window.cardColor
                                radius: 17
                                border.color: window.borderColor
                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    spacing: 10
                                    RowLayout {
                                        Layout.fillWidth: true
                                        visible: ember.pendingImageUrl.length > 0
                                        spacing: 12
                                        Image {
                                            Layout.preferredWidth: 86
                                            Layout.preferredHeight: 63
                                            source: ember.pendingImageUrl
                                            sourceSize.width: 360
                                            sourceSize.height: 360
                                            fillMode: Image.PreserveAspectFit
                                            smooth: true
                                            asynchronous: true
                                        }
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            Text {
                                                text: "LOCAL IMAGE ATTACHED"
                                                color: window.mint
                                                font.pixelSize: 11
                                                font.letterSpacing: 0.9
                                            }
                                            Text {
                                                Layout.fillWidth: true
                                                text: ember.visionReady
                                                    ? "On-device vision ready · attach to this message"
                                                    : "Vision projector missing · configure in Settings"
                                                color: ember.visionReady ? "#9BC6E5" : "#F4B9A5"
                                                font.pixelSize: 11
                                                wrapMode: Text.WordWrap
                                            }
                                        }
                                        PillButton {
                                            text: "Remove"
                                            implicitWidth: 84
                                            implicitHeight: 32
                                            onClicked: ember.clearAttachedImage()
                                        }
                                    }
                                    TextArea {
                                        id: composer
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        enabled: !ember.busy && !ember.listening
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
                                                (event.modifiers & Qt.ShiftModifier) &&
                                                event.key === Qt.Key_V) {
                                                ember.pasteImage()
                                                event.accepted = true
                                            }
                                            if ((event.modifiers & Qt.ControlModifier) &&
                                                (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) {
                                                window.submitComposer()
                                                event.accepted = true
                                            }
                                        }
                                        DropArea {
                                            anchors.fill: parent
                                            onDropped: function(drop) {
                                                if (drop.hasUrls && drop.urls.length === 1)
                                                    ember.attachImageUrl(drop.urls[0].toString())
                                            }
                                        }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        visible: ember.incompleteReply
                                        spacing: 10
                                        Text {
                                            text: "The local model reached its reply limit."
                                            color: "#A6C8E8"
                                            font.pixelSize: 12
                                            wrapMode: Text.WordWrap
                                            Layout.fillWidth: true
                                        }
                                        PillButton {
                                            text: "Continue answer"
                                            implicitWidth: 150
                                            implicitHeight: 32
                                            enabled: ember.brainReady && !ember.busy &&
                                                     !ember.listening && !ember.speaking
                                            onClicked: ember.continueIncompleteReply()
                                            ToolTip.visible: hovered
                                            ToolTip.delay: 700
                                            ToolTip.text: "Ask the offline model to resume without repeating. Your unsent draft stays intact."
                                        }
                                    }
                                    Flow {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: childrenRect.height
                                        spacing: 8
                                        PillButton {
                                            text: "Add text file"
                                            implicitWidth: 115
                                            implicitHeight: 31
                                            enabled: !ember.busy && !ember.listening
                                            onClicked: {
                                                var draft = ember.chooseLocalTextFile(composer.text)
                                                if (draft.length > 0) {
                                                    composer.text = draft
                                                    composer.cursorPosition = composer.length
                                                }
                                            }
                                            ToolTip.visible: hovered
                                            ToolTip.delay: 700
                                            ToolTip.text: "Read one selected local UTF-8 text/code file into your editable draft. Nothing is sent until you press Send."
                                        }
                                        PillButton {
                                            text: "Project tools"
                                            implicitWidth: 119
                                            implicitHeight: 31
                                            enabled: !ember.busy && !ember.listening
                                            onClicked: {
                                                var updated = ember.searchLocalCodeFolder(composer.text)
                                                if (updated.length > 0) {
                                                    composer.text = updated
                                                    composer.cursorPosition = composer.length
                                                }
                                            }
                                            ToolTip.visible: hovered
                                            ToolTip.delay: 700
                                            ToolTip.text: "Choose a local folder, then map source filenames or search literal text. Review the editable result; no code is executed."
                                        }
                                        PillButton {
                                            text: "Investigate"
                                            implicitWidth: 120
                                            implicitHeight: 31
                                            enabled: ember.brainReady && !ember.busy &&
                                                     !ember.listening &&
                                                     composer.text.trim().length > 0 &&
                                                     composer.text.length <= 2048
                                            onClicked: ember.researchLocalProject(composer.text)
                                            ToolTip.visible: hovered
                                            ToolTip.delay: 700
                                            ToolTip.text: "Select one local project root. The local model may map, search and inspect bounded source excerpts up to six times. No writes, shell, Git or Internet."
                                        }
                                        PillButton {
                                            text: "Propose fix"
                                            implicitWidth: 120
                                            implicitHeight: 31
                                            enabled: ember.brainReady && !ember.busy &&
                                                     !ember.listening &&
                                                     composer.text.trim().length > 0 &&
                                                     composer.text.length <= 2048
                                            onClicked: ember.proposeLocalProjectFix(composer.text)
                                            ToolTip.visible: hovered
                                            ToolTip.delay: 700
                                            ToolTip.text: "Choose a local project. The model can inspect code and stage up to three coordinated file edits. Nothing changes before you review and approve the entire patch."
                                        }
                                        PillButton {
                                            text: "Run tests"
                                            implicitWidth: 108
                                            implicitHeight: 31
                                            enabled: !ember.busy && !ember.listening &&
                                                     !ember.speaking
                                            onClicked: ember.runApprovedProjectTests()
                                            ToolTip.visible: hovered
                                            ToolTip.delay: 700
                                            ToolTip.text: "Run one approved fixed CTest, Cargo, Python unittest, Node.js, Go or pytest command. Local tests execute project code; no sandbox, shell or automatic rerun."
                                        }
                                        PillButton {
                                            text: "GitHub agent"
                                            implicitWidth: 121
                                            implicitHeight: 31
                                            enabled: ember.brainReady && !ember.busy &&
                                                     !ember.listening &&
                                                     composer.text.trim().length > 0 &&
                                                     composer.text.length <= 2048
                                            onClicked: ember.researchGitHubRepository(composer.text)
                                            ToolTip.visible: hovered
                                            ToolTip.delay: 700
                                            ToolTip.text: "Approve one GitHub repo and public or scoped read-only access. The local model investigates up to six views; it cannot publish issues, rerun Actions or edit code."
                                        }
                                        PillButton {
                                            text: "Edit source"
                                            implicitWidth: 116
                                            implicitHeight: 31
                                            enabled: !ember.busy && !ember.listening
                                            onClicked: {
                                                var editPreview = ember.stageLocalCodeEdit()
                                                if (editPreview.length > 0) {
                                                    window.localEditPreview = editPreview
                                                    localCodeEditDialog.open()
                                                }
                                            }
                                            ToolTip.visible: hovered
                                            ToolTip.delay: 700
                                            ToolTip.text: "Stage one exact UTF-8 file replacement. Review, approve and keep a private backup. No model actions or shell commands."
                                        }
                                        PillButton {
                                            text: "GitHub · Read"
                                            implicitWidth: 122
                                            implicitHeight: 31
                                            enabled: !ember.busy && !ember.listening
                                            onClicked: ember.inspectPublicGitHub()
                                            ToolTip.visible: hovered
                                            ToolTip.delay: 700
                                            ToolTip.text: "Inspect one public repository, issues, PRs or file. Network access only on your click; no token or modifications."
                                        }
                                        PillButton {
                                            text: "GitHub · Issues"
                                            implicitWidth: 134
                                            implicitHeight: 31
                                            enabled: !ember.busy && !ember.listening
                                            onClicked: ember.githubIssuesAction()
                                            ToolTip.visible: hovered
                                            ToolTip.delay: 700
                                            ToolTip.text: "Manually review and publish one GitHub issue with a separate Issues-write credential; never publish in the background."
                                        }
                                        PillButton {
                                            text: "Wiki agent"
                                            implicitWidth: 113
                                            implicitHeight: 31
                                            enabled: ember.brainReady && !ember.busy &&
                                                     !ember.listening &&
                                                     composer.text.trim().length > 0 &&
                                                     composer.text.length <= 2048
                                            onClicked: ember.researchWikipedia(composer.text)
                                            ToolTip.visible: hovered
                                            ToolTip.delay: 700
                                            ToolTip.text: "Approve up to three model-selected English/Russian Wikipedia searches. Fixed official endpoints and source-linked snippets only; review before chat."
                                        }
                                        PillButton {
                                            text: "Wikipedia"
                                            implicitWidth: 110
                                            implicitHeight: 31
                                            enabled: !ember.busy && !ember.listening
                                            onClicked: ember.searchWikipedia()
                                            ToolTip.visible: hovered
                                            ToolTip.delay: 700
                                            ToolTip.text: "Search English or Russian Wikipedia with source URLs. No background requests; review excerpts before adding them to chat."
                                        }
                                        PillButton {
                                            text: "Add image"
                                            implicitWidth: 97
                                            implicitHeight: 31
                                            enabled: !ember.privateMode && !ember.busy && !ember.listening
                                            onClicked: ember.chooseImage()
                                        }
                                        PillButton {
                                            text: "Paste image"
                                            implicitWidth: 102
                                            implicitHeight: 31
                                            enabled: !ember.privateMode && !ember.busy && !ember.listening
                                            onClicked: ember.pasteImage()
                                        }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 8
                                        Text {
                                            Layout.fillWidth: true
                                            text: composer.text.trim().length > 8192
                                                  ? "Draft too long to send · split into 8,192-character messages"
                                                  : "Ctrl + Enter · send"
                                            color: composer.text.trim().length > 8192
                                                   ? "#E9B3A4" : "#8097A7"
                                            font.pixelSize: 11
                                            elide: Text.ElideRight
                                        }
                                        PillButton {
                                            text: "Google Drive"
                                            implicitWidth: 118
                                            implicitHeight: 34
                                            enabled: !ember.busy && !ember.listening
                                            onClicked: ember.driveAction()
                                            ToolTip.visible: hovered
                                            ToolTip.delay: 700
                                            ToolTip.text: "Manual read-only Google consent, folder listing and selected text/Docs excerpts. No automatic sync or writes."
                                        }
                                        PillButton {
                                            id: dictationGesture
                                            text: window.dictationHoldArmed ? "Release to draft" : "Tap / hold to dictate"
                                            implicitWidth: 177
                                            implicitHeight: 38
                                            // Keep the button alive and enabled for the
                                            // matching release, even after C++ enters
                                            // its recording state and updates bindings.
                                            visible: (!ember.dictationActive &&
                                                      !ember.dictationTranscribing) ||
                                                     window.dictationHoldArmed
                                            enabled: (!ember.busy && !ember.listening) ||
                                                     window.dictationHoldArmed
                                            onPressed: {
                                                if (ember.busy || ember.listening ||
                                                    ember.dictationTranscribing) return
                                                window.dictationHoldArmed = true
                                                window.dictationPressStartedAt = Date.now()
                                                ember.startDictation()
                                                if (!ember.dictationActive)
                                                    window.dictationHoldArmed = false
                                            }
                                            onReleased: {
                                                if (!window.dictationHoldArmed) return
                                                const elapsed = Date.now() -
                                                    window.dictationPressStartedAt
                                                const toDraft =
                                                    ember.shouldTranscribeHeldDictation(elapsed)
                                                window.dictationHoldArmed = false
                                                if (toDraft) ember.finishDictation(false)
                                            }
                                            onCanceled: {
                                                if (window.dictationHoldArmed &&
                                                    ember.dictationActive)
                                                    ember.cancelDictation()
                                                window.dictationHoldArmed = false
                                            }
                                            ToolTip.visible: hovered
                                            ToolTip.delay: 700
                                            ToolTip.text: "Tap to begin RU/EN recording and use To draft / Transcribe & send when ready. Hold for at least 0.45s and release to transcribe into an editable draft automatically. Drag outside to cancel. Never sends merely on release."
                                        }
                                        Text {
                                            visible: ember.dictationActive || ember.dictationTranscribing
                                            text: ember.dictationActive ? "● Recording" : "Transcribing locally…"
                                            color: "#88CEF5"
                                            font.pixelSize: 12
                                            Layout.alignment: Qt.AlignVCenter
                                        }
                                        PillButton {
                                            text: "Cancel"
                                            implicitWidth: 78
                                            implicitHeight: 38
                                            visible: ember.dictationActive || ember.dictationTranscribing
                                            enabled: visible
                                            onClicked: ember.cancelDictation()
                                            ToolTip.visible: hovered
                                            ToolTip.text: "Discard this recording or cancel ongoing local transcription. Existing text draft remains unchanged."
                                        }
                                        PillButton {
                                            text: "To draft"
                                            implicitWidth: 92
                                            implicitHeight: 38
                                            visible: ember.dictationActive
                                            enabled: visible
                                            onClicked: ember.finishDictation(false)
                                            ToolTip.visible: hovered
                                            ToolTip.text: "Stop microphone, transcribe all intact RU/EN audio locally and insert editable text. Never automatically send."
                                        }
                                        PillButton {
                                            text: "Transcribe & send"
                                            implicitWidth: 151
                                            implicitHeight: 38
                                            visible: ember.dictationActive
                                            enabled: visible && ember.brainReady
                                            onClicked: ember.finishDictation(true)
                                            ToolTip.visible: hovered
                                            ToolTip.text: "Stop, transcribe locally, insert text in the draft, then send to the local model. No send on cancel, errors or microphone interruption."
                                        }
                                        PillButton {
                                            text: ember.busy ? "Working…" : "Send message"
                                            implicitWidth: 146
                                            implicitHeight: 38
                                            enabled: ember.brainReady && !ember.busy &&
                                                     !ember.listening &&
                                                     composer.text.trim().length <= 8192 &&
                                                     (composer.text.trim().length > 0 ||
                                                      ember.pendingImageUrl.length > 0)
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
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: "MODEL ROUTING"; color: "#D9ECFF"; font.pixelSize: 16; font.weight: Font.DemiBold }
                                    Item { Layout.fillWidth: true }
                                    PillButton {
                                        text: "Check offline setup"
                                        implicitWidth: 176
                                        implicitHeight: 36
                                        onClicked: {
                                            window.offlineSetupReport = ember.inspectOfflineSetup()
                                            offlineSetupDialog.open()
                                        }
                                        ToolTip.visible: hovered
                                        ToolTip.delay: 700
                                        ToolTip.text: "Read-only local model and ASR file checks. No model launch or internet requests; results are not inference tests."
                                    }
                                }
                                Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: 132
                                    radius: 13
                                    color: window.cardColor
                                    border.color: window.borderColor
                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 14
                                        spacing: 10
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Text { text: "LLM context window"; color: "#D2E6F8"; font.pixelSize: 13; Layout.fillWidth: true }
                                            ComboBox {
                                                id: modelContextPicker
                                                Layout.preferredWidth: 205
                                                implicitHeight: 36
                                                model: ["4,096 tokens", "8,192 tokens", "16,384 tokens", "32,768 tokens"]
                                                currentIndex: [4096, 8192, 16384, 32768].indexOf(ember.brainContextTokens)
                                                enabled: !ember.brainReady && !ember.brainLaunching
                                                onActivated: ember.brainContextTokens = [4096, 8192, 16384, 32768][currentIndex]
                                                contentItem: Text { text: modelContextPicker.displayText; leftPadding: 11; color: "#E6F4FF"; verticalAlignment: Text.AlignVCenter; font.pixelSize: 12 }
                                                background: Rectangle { radius: 10; color: "#0A1B31"; border.color: "#2B648D" }
                                            }
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Text { text: "Text response budget"; color: "#D2E6F8"; font.pixelSize: 13; Layout.fillWidth: true }
                                            ComboBox {
                                                id: responseTokensPicker
                                                Layout.preferredWidth: 205
                                                implicitHeight: 36
                                                model: ["Auto · profile default", "2,048 tokens", "4,096 tokens", "8,192 tokens"]
                                                currentIndex: [0, 2048, 4096, 8192].indexOf(ember.brainResponseTokens)
                                                enabled: !ember.busy && !ember.listening
                                                onActivated: ember.brainResponseTokens = [0, 2048, 4096, 8192][currentIndex]
                                                contentItem: Text { text: responseTokensPicker.displayText; leftPadding: 11; color: "#E6F4FF"; verticalAlignment: Text.AlignVCenter; font.pixelSize: 12 }
                                                background: Rectangle { radius: 10; color: "#0A1B31"; border.color: "#2B648D" }
                                            }
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Text { text: "Model GPU offload"; color: "#D2E6F8"; font.pixelSize: 13; Layout.fillWidth: true }
                                            ComboBox {
                                                id: modelGpuPicker
                                                Layout.preferredWidth: 205
                                                implicitHeight: 36
                                                model: ["CPU only · 0", "20 layers", "50 layers", "Max offload · 99"]
                                                currentIndex: [0, 20, 50, 99].indexOf(ember.brainGpuLayers)
                                                enabled: !ember.brainReady && !ember.brainLaunching
                                                onActivated: ember.brainGpuLayers = [0, 20, 50, 99][currentIndex]
                                                contentItem: Text { text: modelGpuPicker.displayText; leftPadding: 11; color: "#E6F4FF"; verticalAlignment: Text.AlignVCenter; font.pixelSize: 12 }
                                                background: Rectangle { radius: 10; color: "#0A1B31"; border.color: "#2B648D" }
                                            }
                                        }
                                    }
                                }
                                Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: 184
                                    radius: 13
                                    color: window.cardColor
                                    border.color: window.borderColor
                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 14
                                        spacing: 8
                                        Text {
                                            text: "RESPONSE SAMPLING · LOCAL"
                                            color: "#CDE7FF"
                                            font.pixelSize: 12
                                            font.weight: Font.DemiBold
                                            font.letterSpacing: 0.6
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Text { text: "Text creativity"; Layout.fillWidth: true; color: "#D2E6F8"; font.pixelSize: 13 }
                                            ComboBox {
                                                id: textTemperaturePicker
                                                Layout.preferredWidth: 205
                                                implicitHeight: 35
                                                model: ["Auto · mode profile", "0.20 · precise", "0.40 · focused", "0.60 · balanced", "0.80 · expressive", "1.00 · creative", "1.20 · experimental"]
                                                currentIndex: [0,20,40,60,80,100,120].indexOf(ember.textTemperaturePercent)
                                                enabled: !ember.busy && !ember.listening
                                                onActivated: ember.textTemperaturePercent = [0,20,40,60,80,100,120][currentIndex]
                                                contentItem: Text { text: textTemperaturePicker.displayText; leftPadding: 11; color: "#E6F4FF"; verticalAlignment: Text.AlignVCenter; font.pixelSize: 12; elide: Text.ElideRight }
                                                background: Rectangle { radius: 10; color: "#0A1B31"; border.color: "#2B648D" }
                                            }
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Text { text: "Text top-p"; Layout.fillWidth: true; color: "#D2E6F8"; font.pixelSize: 13 }
                                            ComboBox {
                                                id: textTopPPicker
                                                Layout.preferredWidth: 205
                                                implicitHeight: 35
                                                model: ["Auto · model default", "0.80 · constrained", "0.90 · focused", "0.95 · diverse", "1.00 · unrestricted"]
                                                currentIndex: [0,80,90,95,100].indexOf(ember.textTopPPercent)
                                                enabled: !ember.busy && !ember.listening
                                                onActivated: ember.textTopPPercent = [0,80,90,95,100][currentIndex]
                                                contentItem: Text { text: textTopPPicker.displayText; leftPadding: 11; color: "#E6F4FF"; verticalAlignment: Text.AlignVCenter; font.pixelSize: 12; elide: Text.ElideRight }
                                                background: Rectangle { radius: 10; color: "#0A1B31"; border.color: "#2B648D" }
                                            }
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Text { text: "Voice reply creativity"; Layout.fillWidth: true; color: "#D2E6F8"; font.pixelSize: 13 }
                                            ComboBox {
                                                id: voiceTemperaturePicker
                                                Layout.preferredWidth: 205
                                                implicitHeight: 35
                                                model: ["Auto · natural 0.65", "0.20 · precise", "0.40 · focused", "0.60 · balanced", "0.80 · expressive", "1.00 · creative", "1.20 · experimental"]
                                                currentIndex: [0,20,40,60,80,100,120].indexOf(ember.voiceTemperaturePercent)
                                                enabled: !ember.busy && !ember.listening
                                                onActivated: ember.voiceTemperaturePercent = [0,20,40,60,80,100,120][currentIndex]
                                                contentItem: Text { text: voiceTemperaturePicker.displayText; leftPadding: 11; color: "#E6F4FF"; verticalAlignment: Text.AlignVCenter; font.pixelSize: 12; elide: Text.ElideRight }
                                                background: Rectangle { radius: 10; color: "#0A1B31"; border.color: "#2B648D" }
                                            }
                                        }
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: "Sampling changes apply to the next reply without restarting the model. Auto keeps defaults; these control words, not the synthesized voice timbre."
                                    font.pixelSize: 11
                                    color: "#91AACA"
                                    wrapMode: Text.WordWrap
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: "Change while the brain is stopped. More context may exceed 12 GB VRAM; CPU mode is slower. Stored chats remain separate from the finite inference window."
                                    font.pixelSize: 11
                                    color: "#91AACA"
                                    wrapMode: Text.WordWrap
                                }
                                Repeater {
                                    model: [
                                        { key: "brainExe", label: "llama-server executable" },
                                        { key: "brainModel", label: "Language model · GGUF" },
                                        { key: "visionProj", label: "Vision projector · GGUF · optional" },
                                        { key: "asrExe", label: "whisper-cli executable" },
                                        { key: "asrModel", label: "Speech recognition weights" },
                                        { key: "asrVadModel", label: "Silero speech detection · optional" },
                                        { key: "ttsExe", label: "sherpa-onnx TTS executable" },
                                        { key: "speechExe", label: "speech.cpp native voice/ASR executable · optional" },
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
                                                enabled: !ember.busy && !ember.listening &&
                                                         !ember.dictationTranscribing &&
                                                         (["brainExe", "brainModel", "visionProj"].indexOf(modelData.key) < 0 ||
                                                          (!ember.brainReady && !ember.brainLaunching))
                                                onClicked: ember.choosePath(modelData.key)
                                            }
                                        }
                                    }
                                }
                                Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: 178
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
                                                enabled: !ember.listening && !ember.busy &&
                                                         !ember.dictationTranscribing
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
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Text {
                                                text: "Offline recognition engine"
                                                color: "#D2E6F8"
                                                font.pixelSize: 13
                                                Layout.fillWidth: true
                                            }
                                            ComboBox {
                                                id: recognitionEnginePicker
                                                Layout.preferredWidth: 212
                                                implicitHeight: 38
                                                model: ["Whisper · existing", "Qwen3-ASR · 0.6B", "Qwen3-ASR · 1.7B"]
                                                currentIndex: ember.recognitionEngine
                                                enabled: !ember.busy && !ember.listening && !ember.speechServerRunning
                                                onActivated: ember.recognitionEngine = currentIndex
                                                font.pixelSize: 12
                                                contentItem: Text {
                                                    text: recognitionEnginePicker.displayText
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
                                                    width: recognitionEnginePicker.width
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
                                            text: ember.recognitionEngine === 0 ?
                                                  "Whisper remains offline and default. Speech replies stay English." :
                                                  "Qwen3-ASR uses installed local GGUF weights. Missing model downloads are blocked unless you explicitly allow them below; RU/EN text stays local."
                                            color: "#869CB6"
                                            font.pixelSize: 11
                                            wrapMode: Text.WordWrap
                                            Layout.fillWidth: true
                                        }
                                    }
                                }

                                Rectangle {
                                    visible: ember.recognitionEngine > 0
                                    Layout.fillWidth: true
                                    implicitHeight: 121
                                    radius: 13
                                    color: window.cardColor
                                    border.color: window.borderColor
                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 14
                                        spacing: 8
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Text {
                                                text: "LOCAL RECOGNITION VOCABULARY"
                                                color: window.mint
                                                font.pixelSize: 11
                                                font.letterSpacing: 0.8
                                                Layout.fillWidth: true
                                            }
                                            Text {
                                                text: asrHintEditor.text.length + " / 512"
                                                color: "#8CA9C2"
                                                font.pixelSize: 11
                                            }
                                        }
                                        TextField {
                                            id: asrHintEditor
                                            Layout.fillWidth: true
                                            implicitHeight: 37
                                            maximumLength: 512
                                            text: ember.recognitionHints
                                            enabled: !ember.busy && !ember.listening
                                            placeholderText: "Names, characters, technical jargon, C++, Yharnam…"
                                            color: "#EAF4FF"
                                            font.pixelSize: 12
                                            onEditingFinished: ember.setRecognitionHints(text)
                                            background: Rectangle {
                                                radius: 9
                                                color: "#071728"
                                                border.color: "#27547C"
                                            }
                                            Connections {
                                                target: ember
                                                function onRecognitionHintsChanged() {
                                                    if (!asrHintEditor.activeFocus)
                                                        asrHintEditor.text = ember.recognitionHints
                                                }
                                            }
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            text: "Only Qwen3-ASR uses these local terms. They belong to this chat, not other sessions."
                                            color: "#829FB8"
                                            font.pixelSize: 11
                                            wrapMode: Text.WordWrap
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
                                    implicitHeight: 225
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
                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 12
                                            Text {
                                                text: "Playback volume"
                                                color: "#C3D5DF"
                                                font.pixelSize: 13
                                                Layout.preferredWidth: 125
                                            }
                                            Slider {
                                                id: speechVolumeSlider
                                                Layout.fillWidth: true
                                                implicitHeight: 31
                                                from: 0
                                                to: 100
                                                stepSize: 1
                                                value: ember.outputVolume
                                                onMoved: ember.outputVolume = Math.round(value)
                                                background: Rectangle {
                                                    x: speechVolumeSlider.leftPadding
                                                    y: speechVolumeSlider.topPadding +
                                                       speechVolumeSlider.availableHeight / 2 - height / 2
                                                    width: speechVolumeSlider.availableWidth
                                                    height: 5
                                                    radius: 3
                                                    color: "#142B47"
                                                    Rectangle {
                                                        width: parent.width * speechVolumeSlider.visualPosition
                                                        height: parent.height
                                                        radius: parent.radius
                                                        color: window.mint
                                                    }
                                                }
                                                handle: Rectangle {
                                                    x: speechVolumeSlider.leftPadding +
                                                       speechVolumeSlider.visualPosition *
                                                       (speechVolumeSlider.availableWidth - width)
                                                    y: speechVolumeSlider.topPadding +
                                                       speechVolumeSlider.availableHeight / 2 - height / 2
                                                    width: 17
                                                    height: 17
                                                    radius: 9
                                                    color: "#D4EFFF"
                                                    border.color: "#4A99DB"
                                                }
                                            }
                                            Text {
                                                text: ember.outputVolume + "%"
                                                color: "#C3D5DF"
                                                Layout.preferredWidth: 43
                                                horizontalAlignment: Text.AlignRight
                                                font.pixelSize: 12
                                            }
                                        }
                                        Text {
                                            text: "Output volume and device selections stay on this PC."
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
                                            "Qwen3-TTS · native speech.cpp"
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
                                        // Preserve upstream sherpa-onnx voice SIDs when
                                        // removing male voices from this female-only picker.
                                        property var femaleVoiceSids: [0, 1, 2, 3, 4, 7, 8]
                                        Layout.preferredWidth: 268
                                        Layout.preferredHeight: 43
                                        model: [
                                            "AF · Female · US",
                                            "Bella · Female · US",
                                            "Nicole · Female · US",
                                            "Sarah · Female · US",
                                            "Sky · Female · US",
                                            "Emma · Female · UK",
                                            "Isabella · Female · UK"
                                        ]
                                        currentIndex: femaleVoiceSids.indexOf(ember.voiceId)
                                        enabled: !ember.busy && !ember.listening && !ember.speaking
                                        onActivated: ember.voiceId = femaleVoiceSids[currentIndex]
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
                                    implicitHeight: ember.localModel === "qwen3-tts-1.7b" ? 433 : 383
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
                                            Layout.fillWidth: true
                                            spacing: 7
                                            Text {
                                                text: "Resident TTS"
                                                Layout.fillWidth: true
                                                color: "#9AB8CF"
                                                font.pixelSize: 11
                                            }
                                            PillButton {
                                                text: "Light · 0.6B"
                                                implicitWidth: 131
                                                implicitHeight: 31
                                                enabled: !ember.busy && !ember.speaking && !ember.speechServerRunning
                                                onClicked: {
                                                    ember.localModel = "qwen3-tts-0.6b"
                                                    externalModel.text = "qwen3-tts-0.6b"
                                                }
                                            }
                                            PillButton {
                                                text: "Expressive · 1.7B"
                                                implicitWidth: 153
                                                implicitHeight: 31
                                                enabled: !ember.busy && !ember.speaking && !ember.speechServerRunning
                                                onClicked: {
                                                    ember.localModel = "qwen3-tts-1.7b"
                                                    externalModel.text = "qwen3-tts-1.7b"
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
                                                onEditingFinished: {
                                                    ember.localVoice = text
                                                    // A running model accepts only verified female speakers in an English-capable TTS model.
                                                    text = ember.localVoice
                                                }
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
                                                text: "Female voices · English synthesis"
                                                color: "#9AB8CF"
                                                font.pixelSize: 11
                                                Layout.fillWidth: true
                                            }
                                            PillButton {
                                                text: "Vivian · bright"
                                                implicitWidth: 146
                                                implicitHeight: 32
                                                enabled: !ember.busy && !ember.speaking &&
                                                         (!ember.speechServerReady ||
                                                          ember.nativeEnglishVoices.indexOf("vivian") >= 0)
                                                onClicked: {
                                                    ember.localVoice = "vivian"
                                                    externalVoice.text = ember.localVoice
                                                }
                                            }
                                            PillButton {
                                                text: "Serena · warm"
                                                implicitWidth: 144
                                                implicitHeight: 32
                                                enabled: !ember.busy && !ember.speaking &&
                                                         (!ember.speechServerReady ||
                                                          ember.nativeEnglishVoices.indexOf("serena") >= 0)
                                                onClicked: {
                                                    ember.localVoice = "serena"
                                                    externalVoice.text = ember.localVoice
                                                }
                                            }
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            text: "Qwen3-TTS Vivian and Serena are native Chinese speakers. " +
                                                  "The model can synthesize English, but English accent and " +
                                                  "naturalness must be checked with Test voice. A voice's " +
                                                  "native language is not its synthesis-language limit."
                                            wrapMode: Text.WordWrap
                                            color: "#7D95AF"
                                            font.pixelSize: 11
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 9
                                            Text {
                                                text: "Verified female speakers (English-capable model)"
                                                color: "#9AB8CF"
                                                font.pixelSize: 11
                                                Layout.fillWidth: true
                                            }
                                            ComboBox {
                                                id: verifiedNativeVoices
                                                visible: ember.speechServerReady &&
                                                         ember.nativeEnglishVoices.length > 0
                                                Layout.preferredWidth: 190
                                                implicitHeight: 34
                                                enabled: !ember.busy && !ember.speaking
                                                model: ember.nativeEnglishVoices
                                                currentIndex: ember.nativeEnglishVoices.indexOf(ember.localVoice)
                                                onActivated: {
                                                    ember.localVoice = currentText
                                                    externalVoice.text = ember.localVoice
                                                }
                                                contentItem: Text {
                                                    text: verifiedNativeVoices.displayText
                                                    leftPadding: 12
                                                    color: "#E6F4FF"
                                                    font.pixelSize: 12
                                                    verticalAlignment: Text.AlignVCenter
                                                }
                                                background: Rectangle {
                                                    radius: 9
                                                    color: "#0A192D"
                                                    border.color: "#254A73"
                                                }
                                                delegate: ItemDelegate {
                                                    required property int index
                                                    required property string modelData
                                                    width: verifiedNativeVoices.width
                                                    height: 34
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
                                            Text {
                                                visible: !verifiedNativeVoices.visible
                                                text: "Available after native model Ready"
                                                color: "#7898B2"
                                                font.pixelSize: 11
                                            }
                                        }
                                        RowLayout {
                                            visible: ember.localModel === "qwen3-tts-1.7b"
                                            Layout.fillWidth: true
                                            spacing: 14
                                            Text {
                                                text: "Delivery"
                                                Layout.preferredWidth: 94
                                                color: window.silver
                                                font.pixelSize: 13
                                            }
                                            ComboBox {
                                                id: deliveryPicker
                                                Layout.fillWidth: true
                                                implicitHeight: 36
                                                model: [
                                                    "Natural · model default",
                                                    "Warm conversation",
                                                    "Soft and gentle",
                                                    "Bright and curious",
                                                    "Expressive narrator",
                                                    "Custom · your delivery description"
                                                ]
                                                currentIndex: ember.deliveryStyle
                                                enabled: !ember.busy && !ember.speaking
                                                onActivated: ember.deliveryStyle = currentIndex
                                                font.pixelSize: 12
                                                contentItem: Text {
                                                    text: deliveryPicker.displayText
                                                    leftPadding: 11
                                                    color: "#E6F4FF"
                                                    font.pixelSize: 12
                                                    verticalAlignment: Text.AlignVCenter
                                                }
                                                background: Rectangle {
                                                    radius: 9
                                                    color: "#0A192D"
                                                    border.color: "#254A73"
                                                }
                                                delegate: ItemDelegate {
                                                    required property int index
                                                    required property string modelData
                                                    width: deliveryPicker.width
                                                    height: 36
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
                                        RowLayout {
                                            visible: ember.localModel === "qwen3-tts-1.7b" &&
                                                     ember.deliveryStyle === 5
                                            Layout.fillWidth: true
                                            spacing: 14
                                            Text {
                                                text: "Custom delivery"
                                                Layout.preferredWidth: 94
                                                color: window.silver
                                                font.pixelSize: 13
                                            }
                                            TextField {
                                                id: customDeliveryInput
                                                Layout.fillWidth: true
                                                implicitHeight: 38
                                                text: ember.customDeliveryPrompt
                                                maximumLength: 240
                                                enabled: !ember.busy && !ember.speaking && !ember.listening
                                                placeholderText: "Warm, natural pauses, gentle curiosity, not theatrical."
                                                color: "#EAF5FF"
                                                font.pixelSize: 12
                                                selectByMouse: true
                                                onEditingFinished: {
                                                    ember.customDeliveryPrompt = text
                                                    text = ember.customDeliveryPrompt
                                                }
                                                ToolTip.visible: hovered
                                                ToolTip.delay: 750
                                                ToolTip.text: "Local 1.7B only, 240 characters maximum, one line. Listening tests determine quality."
                                                background: Rectangle {
                                                    color: "#0A192D"
                                                    radius: 9
                                                    border.color: "#254A73"
                                                }
                                            }
                                        }
                                        Text {
                                            text: "Qwen3-TTS 1.7B supports presets and a custom delivery description; 0.6B does not. These adjust voice prosody, not text creativity. Naturalness requires real listening tests."
                                            color: "#86A4BD"
                                            font.pixelSize: 11
                                            Layout.fillWidth: true
                                            wrapMode: Text.WordWrap
                                        }
                                    }
                                }
                                Rectangle {
                                    visible: ember.voiceEngine === 1 || ember.recognitionEngine > 0
                                    Layout.fillWidth: true
                                    implicitHeight: nativeSpeechControls.implicitHeight + 30
                                    radius: 14
                                    color: window.cardColor
                                    border.color: window.borderColor
                                    ColumnLayout {
                                        id: nativeSpeechControls
                                        anchors.fill: parent
                                        anchors.margins: 15
                                        spacing: 11
                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 8
                                            Text {
                                                text: "NATIVE speech.cpp · " +
                                                      (ember.speechDevice === 2 ? "CPU" :
                                                       ember.speechDevice === 1 ? "GPU" : "AUTO")
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
                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 7
                                            Text {
                                                text: "INFERENCE DEVICE"
                                                color: "#BDDDF4"
                                                font.pixelSize: 11
                                                Layout.fillWidth: true
                                            }
                                            PillButton {
                                                objectName: "emberSpeechAutoDevice"
                                                text: "Auto"
                                                implicitWidth: 76
                                                implicitHeight: 30
                                                fill: ember.speechDevice === 0 ? "#235986" : "#0F2B4B"
                                                enabled: !ember.busy && !ember.listening &&
                                                         !ember.speaking && !ember.speechServerRunning
                                                onClicked: ember.speechDevice = 0
                                            }
                                            PillButton {
                                                text: "GPU"
                                                implicitWidth: 76
                                                implicitHeight: 30
                                                fill: ember.speechDevice === 1 ? "#235986" : "#0F2B4B"
                                                enabled: !ember.busy && !ember.listening &&
                                                         !ember.speaking && !ember.speechServerRunning
                                                onClicked: ember.speechDevice = 1
                                            }
                                            PillButton {
                                                text: "CPU"
                                                implicitWidth: 76
                                                implicitHeight: 30
                                                fill: ember.speechDevice === 2 ? "#235986" : "#0F2B4B"
                                                enabled: !ember.busy && !ember.listening &&
                                                         !ember.speaking && !ember.speechServerRunning
                                                onClicked: ember.speechDevice = 2
                                            }
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            wrapMode: Text.WordWrap
                                            text: ember.speechDevice === 2 ?
                                                  "CPU: reserve GPU memory for the text brain; speech may be slower." :
                                                  ember.speechDevice === 1 ?
                                                  "GPU: fastest candidate, but may compete with the text model for VRAM." :
                                                  "Auto: speech.cpp chooses the available acceleration device."
                                            color: "#83A9C7"
                                            font.pixelSize: 10
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            text: "Offline-first speech.cpp: choose existing GGUF files from any local drive, or put them in models/speech/<alias>.gguf. No copies or downloads are needed."
                                            wrapMode: Text.WordWrap
                                            color: "#8EABC5"
                                            font.pixelSize: 12
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            visible: ember.voiceEngine === 1
                                            spacing: 8
                                            Text {
                                                text: "LOCAL TTS GGUF"
                                                Layout.fillWidth: true
                                                color: "#BDDDF4"
                                                font.pixelSize: 11
                                            }
                                            PillButton {
                                                objectName: "emberNativeTtsPicker"
                                                text: "Choose file"
                                                implicitWidth: 114
                                                implicitHeight: 31
                                                enabled: !ember.busy && !ember.listening &&
                                                         !ember.speaking && !ember.speechServerRunning
                                                onClicked: ember.chooseNativeSpeechModel(false)
                                            }
                                            PillButton {
                                                text: "Clear"
                                                implicitWidth: 68
                                                implicitHeight: 31
                                                enabled: ember.configurationRevision >= 0 &&
                                                         !ember.busy && !ember.listening &&
                                                         !ember.speaking && !ember.speechServerRunning &&
                                                         ember.nativeSpeechModelPath(false).length > 0
                                                onClicked: ember.clearNativeSpeechModel(false)
                                            }
                                        }
                                        Text {
                                            visible: ember.voiceEngine === 1
                                            Layout.fillWidth: true
                                            elide: Text.ElideMiddle
                                            color: "#86A5BE"
                                            font.pixelSize: 10
                                            text: ember.configurationRevision >= 0 &&
                                                  ember.nativeSpeechModelPath(false).length > 0 ?
                                                  "Selected TTS: " + ember.nativeSpeechModelPath(false) :
                                                  "TTS: no external file selected; bundled path is fallback"
                                            ToolTip.visible: mouseHover.containsMouse
                                            ToolTip.text: text
                                            MouseArea { id: mouseHover; anchors.fill: parent; hoverEnabled: true }
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            visible: ember.recognitionEngine > 0
                                            spacing: 8
                                            Text {
                                                text: "LOCAL ASR GGUF"
                                                Layout.fillWidth: true
                                                color: "#BDDDF4"
                                                font.pixelSize: 11
                                            }
                                            PillButton {
                                                objectName: "emberNativeAsrPicker"
                                                text: "Choose file"
                                                implicitWidth: 114
                                                implicitHeight: 31
                                                enabled: !ember.busy && !ember.listening &&
                                                         !ember.speaking && !ember.speechServerRunning
                                                onClicked: ember.chooseNativeSpeechModel(true)
                                            }
                                            PillButton {
                                                text: "Clear"
                                                implicitWidth: 68
                                                implicitHeight: 31
                                                enabled: ember.configurationRevision >= 0 &&
                                                         !ember.busy && !ember.listening &&
                                                         !ember.speaking && !ember.speechServerRunning &&
                                                         ember.nativeSpeechModelPath(true).length > 0
                                                onClicked: ember.clearNativeSpeechModel(true)
                                            }
                                        }
                                        Text {
                                            visible: ember.recognitionEngine > 0
                                            Layout.fillWidth: true
                                            elide: Text.ElideMiddle
                                            color: "#86A5BE"
                                            font.pixelSize: 10
                                            text: ember.configurationRevision >= 0 &&
                                                  ember.nativeSpeechModelPath(true).length > 0 ?
                                                  "Selected ASR: " + ember.nativeSpeechModelPath(true) :
                                                  "ASR: no external file selected; bundled path is fallback"
                                            ToolTip.visible: asrPathHover.containsMouse
                                            ToolTip.text: text
                                            MouseArea { id: asrPathHover; anchors.fill: parent; hoverEnabled: true }
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
                                            Text {
                                                Layout.fillWidth: true
                                                text: "ALLOW MODEL DOWNLOADS"
                                                color: "#BDDDF4"
                                                font.pixelSize: 11
                                            }
                                            PillButton {
                                                text: ember.allowSpeechDownloads ? "Enabled" : "Blocked"
                                                implicitWidth: 106
                                                implicitHeight: 32
                                                enabled: !ember.speechServerRunning
                                                onClicked: ember.allowSpeechDownloads = !ember.allowSpeechDownloads
                                                ToolTip.visible: hovered
                                                ToolTip.text: "Blocked by default; opt-in may need access to a foreign model host."
                                            }
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            visible: ember.voiceEngine === 1
                                            spacing: 10
                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 2
                                                Text {
                                                    text: "TRUST EXTERNAL LOCAL TTS"
                                                    font.pixelSize: 11
                                                    color: "#BDDDF4"
                                                }
                                                Text {
                                                    text: "Advanced · a separate local app may receive spoken text."
                                                    color: "#8EABC5"
                                                    font.pixelSize: 10
                                                    wrapMode: Text.WordWrap
                                                    Layout.fillWidth: true
                                                }
                                            }
                                            PillButton {
                                                text: ember.allowExternalSpeechProvider ? "Trusted" : "Blocked"
                                                implicitWidth: 106
                                                implicitHeight: 32
                                                enabled: !ember.busy && !ember.listening && !ember.speaking
                                                onClicked: ember.allowExternalSpeechProvider = !ember.allowExternalSpeechProvider
                                                ToolTip.visible: hovered
                                                ToolTip.delay: 700
                                                ToolTip.text: "Blocked by default. Only use with a separate TTS server you explicitly started yourself on 127.0.0.1:43822. Managed speech.cpp always requires PID verification."
                                            }
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Item { Layout.fillWidth: true }
                                            PillButton {
                                                text: ember.speechServerRunning ? "Stop native engine" : "Start native engine"
                                                implicitWidth: 170
                                                implicitHeight: 38
                                                enabled: ember.speechServerRunning || !ember.busy
                                                onClicked: {
                                                    if (ember.speechServerRunning) {
                                                        ember.stopSpeechServer()
                                                    } else {
                                                        // Do not rewrite a TTS preset when only
                                                        // Qwen3-ASR is enabled with Kokoro speech.
                                                        if (ember.voiceEngine === 1) {
                                                            ember.localModel = externalModel.text.trim()
                                                            ember.localVoice = externalVoice.text.trim()
                                                        }
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
                    Item {
                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 14
                            Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: 86
                                radius: 15
                                color: "#0B1C31"
                                border.color: "#244C73"
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.margins: 14
                                    spacing: 10
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        Text {
                                            text: "ON-DEVICE IMAGE LIBRARY"
                                            color: window.mint
                                            font.pixelSize: 12
                                            font.letterSpacing: 1.1
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            text: "Images are normalized, deduplicated and never automatically uploaded."
                                            font.pixelSize: 11
                                            color: "#9BB7CE"
                                            wrapMode: Text.WordWrap
                                        }
                                    }
                                    PillButton {
                                        text: "Add image"
                                        implicitWidth: 112
                                        implicitHeight: 37
                                        enabled: !ember.privateMode && !ember.busy && !ember.listening
                                        onClicked: ember.chooseImage()
                                    }
                                    PillButton {
                                        text: "Paste image"
                                        implicitWidth: 114
                                        implicitHeight: 37
                                        enabled: !ember.privateMode && !ember.busy && !ember.listening
                                        onClicked: ember.pasteImage()
                                    }
                                }
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                radius: 17
                                color: window.cardColor
                                border.color: window.borderColor
                                GridView {
                                    id: imageGallery
                                    anchors.fill: parent
                                    anchors.margins: 14
                                    clip: true
                                    cellWidth: 190
                                    cellHeight: 216
                                    model: ember.imageLibraryItems
                                    boundsBehavior: Flickable.StopAtBounds
                                    ScrollBar.vertical: ScrollBar {}
                                    delegate: Rectangle {
                                        required property var modelData
                                        width: imageGallery.cellWidth - 12
                                        height: imageGallery.cellHeight - 12
                                        radius: 13
                                        color: "#0C1B2D"
                                        border.color: "#26476C"
                                        ColumnLayout {
                                            anchors.fill: parent
                                            anchors.margins: 9
                                            spacing: 6
                                            Image {
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: 116
                                                source: modelData.url
                                                sourceSize.width: 430
                                                sourceSize.height: 430
                                                asynchronous: true
                                                fillMode: Image.PreserveAspectFit
                                                smooth: true
                                            }
                                            Text {
                                                text: modelData.label + " · " +
                                                      Math.max(1, Math.ceil(modelData.bytes / 1024)) + " KB"
                                                color: "#A8C8E1"
                                                font.pixelSize: 10
                                                elide: Text.ElideRight
                                                Layout.fillWidth: true
                                            }
                                            RowLayout {
                                                Layout.fillWidth: true
                                                spacing: 6
                                                PillButton {
                                                    text: "Attach"
                                                    Layout.fillWidth: true
                                                    implicitHeight: 31
                                                    enabled: !ember.privateMode && !ember.busy
                                                    onClicked: {
                                                        ember.attachSavedImage(modelData.id)
                                                        window.page = 1
                                                    }
                                                }
                                                PillButton {
                                                    text: "Delete"
                                                    implicitWidth: 70
                                                    implicitHeight: 31
                                                    enabled: !ember.privateMode && !ember.busy
                                                    onClicked: {
                                                        window.imageToDelete = modelData.id
                                                        deleteImageDialog.open()
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                                Text {
                                    anchors.centerIn: parent
                                    visible: imageGallery.count === 0
                                    text: "No saved images yet.\nAdd an image or paste one from your clipboard."
                                    color: "#8CAAC6"
                                    font.pixelSize: 14
                                    horizontalAlignment: Text.AlignHCenter
                                    lineHeight: 1.45
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: "Library is independent of text-chat backups. Deleted images cannot be recovered from a chat transcript alone."
                                color: "#849BB0"
                                font.pixelSize: 11
                                wrapMode: Text.WordWrap
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
