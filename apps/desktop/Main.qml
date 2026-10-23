import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    width: 1200
    height: 880
    minimumWidth: 1020
    minimumHeight: 780
    visible: true
    title: "OpenNVR — Monitoring & Event"
    color: "#0b111b"
    font.family: "Segoe UI"
    font.pixelSize: 14
    palette.windowText: "#e5edf6"
    palette.text: "#e5edf6"
    palette.button: "#20314a"
    palette.buttonText: "#e5edf6"
    palette.base: "#111d2d"
    palette.highlight: "#4bc7ae"

    function engineLabel(state) {
        const names = {"starting":"Memulai", "connecting":"Menghubungkan kamera", "warming_up":"Menyiapkan video dan AI",
            "running":"Monitoring aktif", "reconnecting":"Menyambungkan kembali", "paused":"Dijeda",
            "storage_blocked":"Penyimpanan membutuhkan perhatian", "configuration_or_storage_error":"Periksa konfigurasi atau penyimpanan", "stopped":"Berhenti"}
        return names[state] || state
    }
    function eventLabel(state) {
        const names = {"recording":"Sedang direkam", "complete":"Selesai", "interrupted":"Terputus", "partial":"Sebagian", "deleting":"Sedang dihapus"}
        return names[state] || state
    }

    component NavTab: TabButton {
        id: tab
        implicitHeight: 40
        contentItem: Label {
            text: tab.text
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            color: tab.checked ? "#62dcc0" : "#9cafc5"
        }
        background: Rectangle { color: tab.checked ? "#1b2e44" : "#101b2a"; radius: 4 }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 32
        spacing: 20

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 4
                Label { text: "OPENNVR"; color: "#62dcc0"; font.pixelSize: 14; font.letterSpacing: 3 }
                Label { text: "Monitoring & Event"; font.pixelSize: 30; font.bold: true }
                Label { text: "Deteksi orang pada host · Rekaman lokal · Satu kamera"; color: "#93a5bd" }
            }
            Item { Layout.fillWidth: true }
            Label { text: "M1 / DEVELOPMENT"; color: "#e4bc70"; font.pixelSize: 12 }
        }

        TabBar {
            id: tabs
            Layout.fillWidth: true
            NavTab { text: "Engine & Event" }
            NavTab { text: "Diagnostik kamera" }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            ColumnLayout {
                spacing: 16
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: engineInfo.implicitHeight + 36
                    radius: 8
                    color: "#142436"
                    ColumnLayout {
                        id: engineInfo
                        anchors.fill: parent
                        anchors.margins: 18
                        Label { text: window.engineLabel(backend.engineState); font.pixelSize: 22; font.bold: true; color: backend.engineState === "running" ? "#62dcc0" : "#e4bc70" }
                        Label { text: backend.engineSummary; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#b4c5d9" }
                        Label { text: backend.engineMessage; visible: backend.engineMessage.length > 0 && backend.engineState !== "running"; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#d9b77d"; font.pixelSize: 12 }
                        Label { text: backend.engineService ? "Engine dikelola oleh Windows Service. Kontrol start/stop service melalui Services Windows." : "Menutup desktop tidak menghentikan engine. Mode proses pengguna belum menjamin operasi setelah logout."; color: "#8fa3ba"; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Button { text: "Jalankan engine pengguna"; enabled: !backend.engineConnected && !backend.busy && !backend.engineService; onClicked: backend.startEngine() }
                    Button { text: backend.enginePaused ? "Lanjutkan deteksi" : "Jeda deteksi"; enabled: backend.engineConnected; onClicked: backend.pauseEngine(!backend.enginePaused) }
                    Button { text: "Hentikan engine pengguna"; enabled: backend.engineConnected && !backend.engineService; onClicked: backend.stopEngine() }
                    Item { Layout.fillWidth: true }
                    Label { text: "Preview dan pengaturan kamera ada di tab Diagnostik"; color: "#93a5bd"; font.pixelSize: 12 }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "REKAMAN EVENT"; font.letterSpacing: 1; color: "#93a5bd" }
                    Item { Layout.fillWidth: true }
                    TextField { id: eventDate; placeholderText: "YYYY-MM-DD / semua tanggal"; Layout.preferredWidth: 250 }
                    ComboBox { id: cameraFilter; model: ["Semua kamera", "Kamera 1"]; Layout.preferredWidth: 160 }
                    Button { text: "Tampilkan"; enabled: backend.engineConnected; onClicked: backend.refreshEvents(eventDate.text.trimmed(), cameraFilter.currentIndex === 1 ? "camera-1" : "") }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: "#101a28"
                    radius: 8
                    border.color: "#22334a"
                    ListView {
                        id: eventsList
                        anchors.fill: parent
                        anchors.margins: 10
                        clip: true
                        spacing: 8
                        model: backend.events
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Rectangle {
                            required property var modelData
                            width: eventsList.width
                            height: 100
                            color: "#18283b"
                            radius: 6
                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 14
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 5
                                    Label { text: Qt.formatDateTime(new Date(modelData.startMs), "dd MMM yyyy · HH:mm:ss"); font.bold: true; font.pixelSize: 16 }
                                    Label { text: "Kamera 1 · " + window.eventLabel(modelData.status) + " · " + modelData.segmentCount + " segmen"; color: "#b5c7dc" }
                                    Label { text: (modelData.endMs > 0 ? ((modelData.endMs - modelData.startMs) / 1000).toFixed(1) + " detik" : "Sedang berlangsung") + " · " + (modelData.bytes / 1000000).toFixed(1) + " MB"; color: "#8298b2"; font.pixelSize: 12 }
                                }
                                Button { text: "Putar"; enabled: backend.engineConnected && !backend.busy && modelData.status !== "recording" && modelData.status !== "deleting"; onClicked: backend.playEvent(modelData.id) }
                            }
                        }
                    }
                    Label { anchors.centerIn: parent; visible: eventsList.count === 0; text: backend.engineConnected ? "Belum ada event dalam filter ini." : "Jalankan engine untuk melihat indeks rekaman."; color: "#8fa3ba" }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: backend.status; Layout.fillWidth: true; elide: Text.ElideRight; color: "#b4c5d9" }
                    Button { text: "Hentikan playback"; enabled: backend.busy; onClicked: backend.stop() }
                }
                Label { text: "Maksimal 50 event terbaru sesuai filter. Retensi awal 14 hari; engine menjaga kuota dan ruang kosong cadangan."; color: "#8298b2"; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            }

            ColumnLayout {
                spacing: 16

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: notice.implicitHeight + 26
            radius: 8
            color: "#17283a"
            Label {
                id: notice
                anchors.fill: parent
                anchors.margins: 13
                wrapMode: Text.WordWrap
                color: "#bdcee1"
                text: "Diagnostik membuka koneksi kamera tambahan. Jeda engine sebelum menjalankan uji kamera. Mode uji AI dibatasi 60 detik; engine pada tab pertama berjalan terus. Preview GPU dan playback memakai jendela video terpisah."
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Button { text: "Uji deteksi orang (60 dtk)"; enabled: !backend.busy; onClicked: backend.startDetection(preview.checked) }
            Label { text: "CUDA • pre-roll 10 dtk • post-roll 20 dtk"; color: "#62dcc0" }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 20
            ColumnLayout {
                Layout.fillWidth: true
                Label { text: "CAMERA ACCOUNT"; color: "#93a5bd"; font.pixelSize: 12; font.letterSpacing: 1 }
                RowLayout {
                    Layout.fillWidth: true
                    TextField { id: host; Layout.fillWidth: true; placeholderText: "Alamat IPv4 kamera"; text: backend.cameraHost; enabled: !backend.busy }
                    TextField { id: username; Layout.fillWidth: true; placeholderText: "Username"; text: backend.cameraUsername; enabled: !backend.busy }
                }
                RowLayout {
                    Layout.fillWidth: true
                    TextField { id: password; Layout.fillWidth: true; placeholderText: "Password baru (credential tersimpan tidak ditampilkan)"; echoMode: TextInput.Password; enabled: !backend.busy }
                    Button { text: "Simpan terenkripsi"; enabled: !backend.busy; onClicked: { backend.saveCamera(host.text, username.text, password.text); password.clear() } }
                }
                Label { text: "Akun RTSP Tapo • disimpan lokal dengan Windows DPAPI"; color: "#7f93ad"; font.pixelSize: 12 }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Button { text: "Uji substream"; enabled: !backend.busy; onClicked: backend.startProbe(false, true, false, preview.checked) }
            Button { text: "Uji main stream"; enabled: !backend.busy; onClicked: backend.startProbe(false, false, false, preview.checked) }
            Button { text: "Uji klip + pre-roll"; enabled: !backend.busy; onClicked: backend.startProbe(false, false, true, preview.checked) }
            Button { text: "Video sintetis"; enabled: !backend.busy; onClicked: backend.startProbe(true, false, true, preview.checked) }
            CheckBox { id: preview; text: "Preview GPU"; checked: true; enabled: !backend.busy }
            Item { Layout.fillWidth: true }
            Button { text: "Hentikan"; enabled: backend.busy; onClicked: backend.stop() }
        }

        RowLayout {
            Layout.fillWidth: true
            Rectangle { width: 8; height: 8; radius: 4; color: backend.busy ? "#e4bc70" : "#62dcc0" }
            Label { text: backend.status; font.bold: true }
            Item { Layout.fillWidth: true }
            Button { text: "Buka klip terakhir"; enabled: backend.lastRecording.length > 0 && !backend.busy; onClicked: backend.openRecording() }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#101a28"
            radius: 8
            border.color: "#22334a"
            ScrollView {
                anchors.fill: parent
                anchors.margins: 14
                clip: true
                TextArea {
                    text: backend.log
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    color: "#c4d2e4"
                    font.family: "Consolas"
                    font.pixelSize: 13
                    background: null
                }
            }
        }

        Flow {
            Layout.fillWidth: true
            spacing: 12
            Repeater {
                model: backend.capabilities
                Label { required property string modelData; text: modelData; color: modelData.endsWith("unavailable") ? "#d3a86c" : "#7d9b99"; font.pixelSize: 11 }
            }
        }
            }
        }
    }
}
