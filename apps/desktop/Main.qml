import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    width: 1120
    height: 820
    minimumWidth: 900
    minimumHeight: 700
    visible: true
    title: "OpenNVR — Camera workbench"
    color: "#0b111b"
    font.family: "Segoe UI"
    font.pixelSize: 14
    palette.windowText: "#e5edf6"
    palette.text: "#e5edf6"
    palette.button: "#20314a"
    palette.buttonText: "#e5edf6"
    palette.base: "#111d2d"
    palette.highlight: "#4bc7ae"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 32
        spacing: 20

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 4
                Label { text: "OPENNVR"; color: "#62dcc0"; font.pixelSize: 14; font.letterSpacing: 3 }
                Label { text: "Camera workbench"; font.pixelSize: 30; font.bold: true }
                Label { text: "Validasi stream, deteksi orang, dan rekaman event pada host lokal"; color: "#93a5bd" }
            }
            Item { Layout.fillWidth: true }
            Label { text: "M0 / PROTOTYPE"; color: "#e4bc70"; font.pixelSize: 12 }
        }

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
                text: "Uji deteksi orang menjalankan AI lokal selama 60 detik dan menyimpan klip otomatis saat orang terkonfirmasi. Service latar belakang dan grid multi-kamera belum aktif. Preview GPU terbuka di jendela terpisah."
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
