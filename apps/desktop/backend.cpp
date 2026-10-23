#include "backend.hpp"
#include "media/probe.hpp"
#include "platform/credentials.hpp"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUrl>

#include <filesystem>

Backend::Backend(QString root, QObject* parent) : QObject(parent), root_(std::move(root)) {
    for (const auto& value : opennvr::media_capabilities()) capabilities_.append(QString::fromStdString(value));
    try {
        const auto credential = opennvr::load_camera_credential(root_ + "/.local/camera.json");
        host_ = credential.host;
        username_ = credential.username;
    } catch (const std::exception&) {
        append("Tambahkan Camera Account Tapo untuk menguji kamera. Password tidak ditampilkan atau dicatat.");
    }
    append("M0 workbench: AI tersedia melalui Uji deteksi orang. Windows Service dan grid multi-kamera belum aktif.");
}

Backend::~Backend() { cancelled_ = true; if (worker_.joinable()) worker_.join(); }

void Backend::append(QString message) {
    log_.append(QDateTime::currentDateTime().toString("HH:mm:ss") + "  " + message);
    while (log_.size() > 150) log_.removeFirst();
    emit changed();
}

void Backend::saveCamera(QString host, QString username, QString password) {
    if (busy_) return;
    QHostAddress address;
    if (!address.setAddress(host.trimmed()) || address.protocol() != QAbstractSocket::IPv4Protocol ||
        username.trimmed().isEmpty() || password.isEmpty()) {
        append("Isi alamat IPv4, username, dan password Camera Account terlebih dahulu.");
        return;
    }
    try {
        opennvr::save_camera_credential(root_ + "/.local/camera.json",
            {address.toString(), username.trimmed(), password});
        host_ = address.toString();
        username_ = username.trimmed();
        append("Konfigurasi disimpan terenkripsi dengan Windows DPAPI.");
    } catch (const std::exception& error) { append(QString::fromUtf8(error.what())); }
}

void Backend::startProbe(bool synthetic, bool substream, bool recording, bool preview) {
    launch(synthetic, substream, recording, preview, false);
}

void Backend::startDetection(bool preview) {
    launch(false, false, false, preview, true);
}

void Backend::launch(bool synthetic, bool substream, bool recording, bool preview, bool detection) {
    if (busy_) return;
    opennvr::ProbeConfig config;
    config.synthetic = synthetic;
    config.substream = substream;
    config.preview = preview;
    config.test_recording = recording;
    config.detect_people = detection;
    config.project_root = std::filesystem::path(root_.toStdWString());
    config.duration = std::chrono::seconds(detection ? 60 : 12);
    config.output = std::filesystem::path((root_ + "/.local/recordings").toStdWString());
    if (!synthetic) {
        try {
            const auto credential = opennvr::load_camera_credential(root_ + "/.local/camera.json");
            config.host = credential.host.toStdString();
            config.username = credential.username.toStdString();
            config.password = credential.password.toStdString();
        } catch (const std::exception& error) { append(QString::fromUtf8(error.what())); return; }
    }
    busy_ = true;
    cancelled_ = false;
    status_ = detection ? "Menguji deteksi orang selama 60 detik" : synthetic ? "Menguji video sintetis" : "Menguji kamera " + host_;
    append(status_);
    config.cancelled = [this] { return cancelled_.load(); };
    config.progress = [this](const std::string& message) {
        QMetaObject::invokeMethod(this, [this, message] { append(QString::fromStdString(message)); }, Qt::QueuedConnection);
    };
    worker_ = std::jthread([this, config = std::move(config)] {
        const auto report = opennvr::run_probe(config);
        QMetaObject::invokeMethod(this, [this, report] {
            busy_ = false;
            status_ = report.cancelled ? "Pengujian dihentikan" : report.success ? "Pengujian selesai" : "Pengujian gagal";
            append(status_);
            if (!report.error.empty()) append(QString::fromStdString(report.error));
            if (!report.recording.empty()) {
                recording_ = QString::fromStdWString(report.recording.wstring());
                append("Klip tersimpan: " + recording_);
            }
            append(QString("%1x%2 | %3 paket | %4 keyframe | %5 detik").arg(report.width).arg(report.height)
                .arg(report.packets).arg(report.keyframes).arg(report.elapsed_seconds, 0, 'f', 1));
            if (report.analyzed_frames) {
                append(QString("AI: %1 frame dianalisis, %2 frame person, %3 event. Rata-rata %4 ms.")
                    .arg(report.analyzed_frames).arg(report.person_frames).arg(report.events)
                    .arg(report.average_inference_ms, 0, 'f', 1));
            }
            if (report.stopped_during_event) append("Pengujian berhenti saat event masih aktif; ekor klip dipotong oleh batas sesi uji.");
            emit changed();
        }, Qt::QueuedConnection);
    });
}

void Backend::stop() { cancelled_ = true; }

void Backend::openRecording() {
    if (!recording_.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(recording_));
}
