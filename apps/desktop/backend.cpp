#include "backend.hpp"
#include "engine_client.hpp"
#include "media/probe.hpp"
#include "platform/credentials.hpp"
#include "platform/camera_catalog.hpp"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUrl>
#include <QTimer>
#include <QJsonArray>
#include <QProcess>
#include <QFileInfo>
#include <QStandardPaths>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

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
    engine_data_ = qEnvironmentVariable("OPENNVR_DATA_DIR");
    if (engine_data_.isEmpty()) {
        const auto installed = qEnvironmentVariable("ProgramData", "C:/ProgramData") + "/OpenNVR";
        engine_data_ = QFileInfo::exists(installed + "/config/engine.json") ? installed : root_ + "/.local/engine";
        engine_service_ = QFileInfo::exists(installed + "/config/engine.json");
    }
    engine_ = new EngineClient(engine_data_, this);
    reloadProfiles();
    connect(engine_, &EngineClient::replied, this, [this](const QString& command, const QJsonObject& reply) {
        if (command == "status") {
            const auto was_connected = engine_connected_;
            engine_connected_ = reply["ok"].toBool();
            if (!engine_connected_) {
                live_supported_ = false;
                camera_states_.clear();
                engine_state_ = "Belum terhubung";
                engine_message_.clear();
                engine_summary_ = "Engine dapat dijalankan terpisah dari desktop.";
                if (was_connected) append("Koneksi kontrol engine terputus; periksa proses/service.");
            } else {
                engine_state_ = reply["state"].toString();
                engine_paused_ = reply["paused"].toBool();
                engine_service_ = reply["service"].toBool();
                live_supported_ = reply["liveProtocol"].toInt() == 1;
                auto sources = reply["cameras"].toArray();
                if (sources.isEmpty()) sources.append(QJsonObject{{"id","camera-1"},{"name","Kamera 1"}});
                QVariantList list;
                camera_states_.clear();
                for (const auto& source : sources) {
                    const auto camera = source.toObject();
                    const auto id = camera["id"].toString();
                    list.append(QVariantMap{{"id",id},{"name",camera["name"].toString()}});
                    camera_states_[id] = camera.toVariantMap();
                }
                if (cameras_ != list) { cameras_ = list; emit camerasChanged(); }
                engine_message_ = reply["message"].toString();
                engine_summary_ = QString("AI: %1 frame · Person: %2 frame · Inferensi: %3 ms · %4 · %5")
                    .arg(reply["analyzedFrames"].toInteger()).arg(reply["personFrames"].toInteger())
                    .arg(reply["inferenceMs"].toDouble(), 0, 'f', 1)
                    .arg(reply["recording"].toBool() ? "Sedang merekam" : "Menunggu event")
                    .arg(reply["service"].toBool() ? "Windows Service" : "Proses pengguna");
                if (reply["mode"].toString() == "synthetic") engine_summary_.prepend("SIMULASI · ");
                const auto last = reply["lastEvent"].toString();
                if (!was_connected || last != last_event_) { last_event_ = last; refreshEvents(filter_date_, filter_camera_); }
            }
            emit changed();
        } else if (!reply["ok"].toBool()) {
            append(reply["error"].toString("Permintaan engine gagal."));
        } else if (command == "events") {
            events_ = reply["events"].toArray().toVariantList();
            emit changed();
        } else if (command == "playback") {
            if (busy_) { append("Hentikan pengujian/playback sebelumnya terlebih dahulu."); return; }
            std::vector<std::filesystem::path> files;
            for (const auto& file : reply["files"].toArray()) files.emplace_back(file.toString().toStdWString());
            if (files.empty()) { append("Belum ada segmen final yang dapat diputar untuk event ini."); return; }
            busy_ = true; cancelled_ = false; status_ = "Memutar event di jendela video"; emit changed();
            worker_ = std::jthread([this, files = std::move(files)] {
                const auto ok = opennvr::play_recordings(files, [this] { return cancelled_.load(); });
                QMetaObject::invokeMethod(this, [this, ok] {
                    busy_ = false; status_ = ok ? "Playback selesai" : "Playback dihentikan atau gagal"; append(status_);
                }, Qt::QueuedConnection);
            });
        } else {
            append(command == "pause" ? "Permintaan jeda dikirim ke engine." : command == "resume" ? "Engine diminta melanjutkan deteksi." : "Permintaan engine diterima.");
            engine_->request({{"command","status"}});
        }
    });
    auto* poll = new QTimer(this);
    poll->setInterval(2000);
    connect(poll, &QTimer::timeout, this, [this] { engine_->request({{"command","status"}}); });
    poll->start();
    QTimer::singleShot(0, this, [this] { engine_->request({{"command","status"}}); });
    append("Live grid menggunakan kanal media engine. Katalog kamera disimpan terenkripsi; perubahan service diterapkan melalui installer Administrator.");
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
    if (engine_connected_ && engine_state_ != "paused" && !synthetic) {
        append("Tunggu sampai status engine Dijeda sebelum membuka koneksi diagnostik tambahan."); return;
    }
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

void Backend::startEngine() {
    if (engine_connected_) return;
    if (engine_data_ != root_ + "/.local/engine" && !qEnvironmentVariableIsSet("OPENNVR_DATA_DIR")) {
        append("Instalasi service terdeteksi. Jalankan OpenNVR melalui Services Windows atau installer service."); return;
    }
    QProcess process;
    process.setProgram(root_ + "/build/dev/opennvr_engine.exe");
    process.setArguments({"--console","--root",root_,"--data",engine_data_});
    process.setWorkingDirectory(root_);
    process.setStandardOutputFile(engine_data_ + "/engine-stdout.log", QIODevice::Append);
    process.setStandardErrorFile(engine_data_ + "/engine-stderr.log", QIODevice::Append);
    QDir().mkpath(engine_data_);
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) { args->flags |= CREATE_NO_WINDOW; });
    if (!process.startDetached()) append("Engine tidak dapat dijalankan. Periksa build dan konfigurasi.");
    else append("Engine dijalankan terpisah; menutup desktop tidak menghentikannya.");
}

void Backend::pauseEngine(bool pause) { engine_->request({{"command",pause ? "pause" : "resume"}}); }
void Backend::stopEngine() { engine_->request({{"command","shutdown"}}); }
void Backend::refreshEvents(QString date, QString camera) {
    QJsonObject request{{"command","events"},{"limit",50},{"cameraId",camera}};
    if (!date.isEmpty()) {
        const auto parsed = QDate::fromString(date, Qt::ISODate);
        if (!parsed.isValid() || parsed.toString(Qt::ISODate) != date) { append("Tanggal harus memakai format YYYY-MM-DD."); return; }
        request["fromMs"] = parsed.startOfDay().toMSecsSinceEpoch();
        request["untilMs"] = parsed.addDays(1).startOfDay().toMSecsSinceEpoch();
    }
    filter_date_ = date; filter_camera_ = camera;
    engine_->request(request);
}
void Backend::playEvent(QString id) { engine_->request({{"command","playback"},{"id",id}}); }

void Backend::reloadProfiles() {
    try {
        const auto local = root_ + "/.local";
        const auto catalog = QFileInfo::exists(local + "/cameras.json") ? opennvr::read_camera_catalog(local + "/cameras.json") : QJsonObject{};
        QVariantList list;
        for (const auto& camera : opennvr::camera_sources(catalog, local, local + "/camera.json")) {
            if (!QFileInfo::exists(camera.credential)) continue;
            const auto credential = opennvr::load_camera_credential(camera.credential);
            list.append(QVariantMap{{"id",camera.id},{"name",camera.name},{"host",credential.host},{"username",credential.username}});
        }
        profiles_ = list; emit profilesChanged();
    } catch (const std::exception& error) { append(QString::fromUtf8(error.what())); }
}
QString Backend::nextProfileId() const {
    for (int n = 1; n <= 12; ++n) {
        const auto id = QString("camera-%1").arg(n);
        bool used = false;
        for (const auto& entry : profiles_) if (entry.toMap()["id"].toString() == id) used = true;
        if (!used) return id;
    }
    return {};
}
void Backend::saveProfile(QString id, QString name, QString host, QString username, QString password) {
    QHostAddress address;
    if (!address.setAddress(host.trimmed()) || address.protocol() != QAbstractSocket::IPv4Protocol || username.trimmed().isEmpty()) {
        append("Isi IPv4 dan username kamera yang valid."); return;
    }
    try {
        const auto local = root_ + "/.local";
        QJsonArray entries;
        const auto existing = QFileInfo::exists(local + "/cameras.json") ? opennvr::read_camera_catalog(local + "/cameras.json") : QJsonObject{};
        QString previous;
        for (const auto& source : opennvr::camera_sources(existing, local, local + "/camera.json")) {
            if (!QFileInfo::exists(source.credential)) continue;
            if (source.id == id) { previous = source.credential; continue; }
            const auto credential = opennvr::load_camera_credential(source.credential);
            if (credential.host == address.toString()) throw std::runtime_error("Alamat kamera sudah dipakai profil lain.");
            entries.append(QJsonObject{{"id",source.id},{"name",source.name},{"credential",QDir(local).relativeFilePath(source.credential)}});
        }
        const auto relative = "cameras/" + id + ".json";
        entries.append(QJsonObject{{"id",id},{"name",name.trimmed()},{"credential",relative}});
        const QJsonObject catalog{{"cameras",entries}};
        opennvr::camera_sources(catalog, local, {}); // Validate IDs, paths and the 12-camera limit before writing.
        if (password.isEmpty() && !previous.isEmpty()) password = opennvr::load_camera_credential(previous).password;
        if (password.isEmpty()) throw std::runtime_error("Isi password untuk kamera baru.");
        opennvr::save_camera_credential(local + "/" + relative, {address.toString(),username.trimmed(),password});
        QSaveFile output(local + "/cameras.json");
        if (!output.open(QIODevice::WriteOnly) || output.write(QJsonDocument(catalog).toJson()) < 0 || !output.commit()) throw std::runtime_error("Katalog tidak dapat disimpan.");
        reloadProfiles();
        append("Profil tersimpan. Terapkan katalog dengan installer Administrator untuk service; engine pengguna memuatnya saat restart.");
    } catch (const std::exception& error) { append(QString::fromUtf8(error.what())); }
}
void Backend::removeProfile(QString id) {
    try {
        const auto local = root_ + "/.local";
        const auto catalog = opennvr::read_camera_catalog(local + "/cameras.json");
        QJsonArray entries;
        for (const auto& entry : catalog["cameras"].toArray()) if (entry.toObject()["id"].toString() != id) entries.append(entry);
        const QJsonObject updated{{"cameras",entries}};
        opennvr::camera_sources(updated, local, {});
        QSaveFile output(local + "/cameras.json");
        if (!output.open(QIODevice::WriteOnly) || output.write(QJsonDocument(updated).toJson()) < 0 || !output.commit()) throw std::runtime_error("Katalog tidak dapat disimpan.");
        reloadProfiles(); append("Profil dihapus dari katalog; rekaman dan file credential tetap disimpan. Terapkan katalog untuk mengubah service.");
    } catch (const std::exception& error) { append(QString::fromUtf8(error.what())); }
}
