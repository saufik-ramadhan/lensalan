#include "engine/host.hpp"
#include "media/probe.hpp"
#include "platform/credentials.hpp"
#include "platform/local_pipe.hpp"
#include "platform/camera_catalog.hpp"
#include "detection/scheduler.hpp"
#include "engine/live_hub.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace opennvr {
namespace {
std::filesystem::path native_path(const QString& value) { return std::filesystem::path(value.toStdWString()); }
QJsonObject event_json(const StoredEvent& event, bool segments) {
    std::uint64_t bytes = 0;
    QJsonArray files;
    for (const auto& segment : event.segments) {
        bytes += segment.bytes;
        if (segments) files.append(QJsonObject{{"number",segment.number}, {"path",QString::fromStdString(segment.path)},
            {"status",QString::fromStdString(segment.status)}, {"startMs",static_cast<qint64>(segment.start_utc_ms)},
            {"endMs",static_cast<qint64>(segment.end_utc_ms)}});
    }
    QJsonObject value{{"id",QString::fromStdString(event.id)}, {"cameraId",QString::fromStdString(event.camera_id)},
        {"status",QString::fromStdString(event.status)}, {"reason",QString::fromStdString(event.reason)},
        {"startMs",static_cast<qint64>(event.start_utc_ms)}, {"endMs",static_cast<qint64>(event.end_utc_ms)},
        {"bytes",static_cast<qint64>(bytes)}, {"segmentCount",static_cast<int>(event.segments.size())}};
    if (segments) value["segments"] = files;
    return value;
}
} // namespace

class EngineHost::Impl {
    struct Camera;
public:
    explicit Impl(EngineOptions options) : options_(std::move(options)) {
        options_.root = QDir(options_.root).absolutePath();
        options_.data = QDir(options_.data).absolutePath();
        if (!QDir().mkpath(options_.data)) throw std::runtime_error("Cannot create engine data directory");
        lock_ = std::make_unique<QLockFile>(options_.data + "/engine.lock");
        lock_->setStaleLockTime(0);
        if (!lock_->tryLock(0)) throw std::runtime_error("An engine already owns this data directory");
        const auto config_directory = options_.service ? options_.data + "/config" : options_.data;
        QJsonObject configuration;
        QFile settings(config_directory + "/engine.json");
        if (settings.exists()) {
            if (!settings.open(QIODevice::ReadOnly) || settings.size() > 65536) throw std::runtime_error("Engine settings cannot be read");
            const auto document = QJsonDocument::fromJson(settings.readAll());
            if (!document.isObject()) throw std::runtime_error("Invalid engine settings");
            const auto value = document.object();
            configuration = value;
            options_.operator_sid = value["operatorSid"].toString(options_.operator_sid);
            const auto days = value["retentionDays"].toInteger(14);
            const auto quota = value["quotaGB"].toInteger(100);
            const auto reserve = value["reserveGB"].toInteger(10);
            if (days < 1 || days > 3650 || quota < 1 || quota > 100000 || reserve < 1 || reserve > 100000)
                throw std::runtime_error("Invalid storage policy");
            policy_.max_age_ms = days * 86400000LL;
            policy_.quota_bytes = quota * 1000000000ULL;
            policy_.reserve_bytes = reserve * 1000000000ULL;
        }
        if (options_.operator_sid.isEmpty()) options_.operator_sid = current_user_sid();
        if (options_.service) {
            require_admin_owned_directory(options_.data);
            require_admin_owned_directory(config_directory);
            require_service_data_acl(options_.data, options_.operator_sid);
            require_service_data_acl(config_directory, options_.operator_sid, true);
        }
        const bool prefer_local_catalog = options_.credential.isEmpty() && !QFileInfo::exists(config_directory + "/camera.json");
        if (options_.credential.isEmpty()) {
            options_.credential = config_directory + "/camera.json";
            if (!options_.service && !QFileInfo::exists(options_.credential))
                options_.credential = options_.root + "/.local/camera.json";
        }
        std::vector<CameraSource> sources;
        if (options_.synthetic) {
            if (options_.synthetic_cameras < 1 || options_.synthetic_cameras > 12) throw std::runtime_error("Synthetic camera count must be 1 to 12");
            for (int n = 1; n <= options_.synthetic_cameras; ++n) sources.push_back({QString("camera-%1").arg(n), QString("Simulasi %1").arg(n), {}});
        } else if (!options_.service && prefer_local_catalog && !configuration.contains("cameras") && QFileInfo::exists(options_.root + "/.local/cameras.json")) {
            sources = camera_sources(read_camera_catalog(options_.root + "/.local/cameras.json"), options_.root + "/.local", options_.credential);
        } else sources = camera_sources(configuration, config_directory, options_.credential);
        if (!options_.synthetic) scheduler_ = std::make_shared<InferenceScheduler>(native_path(options_.root));
        for (const auto& source : sources) {
            auto camera = std::make_unique<Camera>();
            camera->source = source;
            camera->state = {{"id",source.id},{"name",source.name},{"state","starting"},{"attempts",0},
                {"streamReady",false},{"aiReady",false},{"recording",false}};
            camera->pipe = std::make_unique<LocalPipeServer>(engine_pipe_name(options_.data) + ".Media." + source.id,
                options_.operator_sid, [pointer = camera.get()](const QJsonObject& request) {
                    if (request["command"].toString() != "live") return QJsonObject{{"ok",false},{"error","Unsupported media command"}};
                    return pointer->live.read(request);
                });
            cameras_.push_back(std::move(camera));
        }
        const auto recordings = native_path(options_.data + "/recordings");
        store_ = std::make_unique<EventStore>(native_path(options_.data + "/events.sqlite3"), recordings);
        store_->reconcile();
        policy_.reserve_bytes = std::max(policy_.reserve_bytes,
            static_cast<std::uint64_t>(std::filesystem::space(recordings).capacity / 20));
        state_ = {{"state","starting"}, {"mode",options_.synthetic ? "synthetic" : "camera"},
            {"service",options_.service}, {"pid",QCoreApplication::applicationPid()}, {"attempts",0},
            {"streamReady",false}, {"aiReady",false}, {"recording",false}};
        pipe_ = std::make_unique<LocalPipeServer>(engine_pipe_name(options_.data), options_.operator_sid,
            [this](const QJsonObject& request) { return handle(request); });
    }
    ~Impl() { stop(); pipe_.reset(); }

    void start() { worker_ = std::jthread([this] { supervise(); }); }
    void stop() {
        stopping_ = true;
        wake_.notify_all();
        if (worker_.joinable()) worker_.join();
    }
    bool stopping() const { return stopping_.load(); }

    QJsonObject handle(const QJsonObject& request) {
        const auto command = request["command"].toString();
        if (command == "status") {
            std::lock_guard lock(state_mutex_);
            auto result = state_;
            QJsonArray cameras;
            qint64 packets = 0, frames = 0, people = 0, attempts = 0;
            bool stream = true, ai = !options_.synthetic, recording = false, all_running = true, all_paused = true;
            int running = 0;
            double inference = 0;
            for (const auto& camera : cameras_) {
                cameras.append(camera->state);
                const auto& state = camera->state;
                packets += state["packets"].toInteger(); frames += state["analyzedFrames"].toInteger();
                people += state["personFrames"].toInteger(); attempts += state["attempts"].toInteger();
                stream &= state["streamReady"].toBool(); ai &= state["aiReady"].toBool();
                recording |= state["recording"].toBool();
                const bool ready = state["state"].toString() == "running";
                running += ready; all_running &= ready;
                all_paused &= state["state"].toString() == "paused";
                inference += state["inferenceMs"].toDouble() * state["analyzedFrames"].toInteger();
            }
            result["cameras"] = cameras; result["cameraCount"] = static_cast<int>(cameras_.size()); result["runningCameras"] = running;
            result["liveProtocol"] = 1;
            result["modelSessions"] = scheduler_ ? scheduler_->sessions() : 0;
            result["inferredFrames"] = static_cast<qint64>(scheduler_ ? scheduler_->inferred() : 0);
            result["replacedAnalysisFrames"] = static_cast<qint64>(scheduler_ ? scheduler_->replaced() : 0);
            result["packets"] = packets; result["analyzedFrames"] = frames; result["personFrames"] = people;
            result["attempts"] = attempts; result["inferenceMs"] = frames ? inference / frames : 0;
            result["streamReady"] = stream; result["aiReady"] = ai; result["recording"] = recording;
            result["state"] = all_paused ? "paused" : all_running ? "running" : cameras_.size() == 1 ? cameras_.front()->state["state"].toString() : "degraded";
            if (!state_["storageReady"].toBool()) result["state"] = "storage_blocked";
            if (cameras_.size() == 1) result["message"] = cameras_.front()->state["message"];
            result["ok"] = true;
            result["paused"] = paused_.load();
            return result;
        }
        if (command == "pause" || command == "resume") {
            paused_ = command == "pause";
            wake_.notify_all();
            return {{"ok",true}, {"paused",paused_.load()}};
        }
        if (command == "shutdown" && !options_.service) {
            stopping_ = true; wake_.notify_all(); return {{"ok",true}};
        }
        if (command == "events") {
            const auto from = request["fromMs"].toInteger(0);
            const auto until = request["untilMs"].toInteger(INT64_MAX);
            if (from < 0 || until <= from) return {{"ok",false},{"error","Invalid time range"}};
            QJsonArray result;
            for (const auto& event : store_->events(from, until, request["cameraId"].toString().toStdString(), request["limit"].toInt(50)))
                result.append(event_json(event, false));
            return {{"ok",true},{"events",result}};
        }
        if (command == "playback") {
            const auto id = request["id"].toString();
            if (id.isEmpty() || id.size() > 128) return {{"ok",false},{"error","Invalid event ID"}};
            const auto event = store_->event(id.toStdString());
            if (event.status == "deleting") return {{"ok",false},{"error","Event sedang dihapus oleh retensi"}};
            auto result = event_json(event, true);
            QJsonArray files;
            for (const auto& segment : event.segments) {
                if (segment.status != "complete") continue;
                const auto path = store_->checked_path(segment.path);
                if (std::filesystem::is_regular_file(path)) files.append(QString::fromStdWString(path.wstring()));
            }
            result["ok"] = true;
            result["files"] = files;
            return result;
        }
        return {{"ok",false},{"error","Unsupported engine command"}};
    }

private:
    void change(const QJsonObject& values) {
        std::lock_guard lock(state_mutex_);
        for (auto i = values.begin(); i != values.end(); ++i) state_[i.key()] = i.value();
        state_["updatedMs"] = QDateTime::currentMSecsSinceEpoch();
    }
    void camera_change(Camera& camera, const QJsonObject& values) {
        std::lock_guard lock(state_mutex_);
        for (auto i = values.begin(); i != values.end(); ++i) camera.state[i.key()] = i.value();
        camera.state["updatedMs"] = QDateTime::currentMSecsSinceEpoch();
    }
    bool wait(int seconds) {
        std::unique_lock lock(wait_mutex_);
        wake_.wait_for(lock, std::chrono::seconds(seconds), [this] { return stopping_.load() || paused_.load(); });
        return !stopping_;
    }
    void camera_loop(Camera& camera) noexcept {
        int failures = 0;
        int attempts = 0;
        auto should_stop = [&] { return stopping_.load(); };
        while (!should_stop()) {
            if (paused_) {
                camera.live.reset();
                camera_change(camera, {{"state","paused"}, {"streamReady",false}, {"aiReady",false}, {"recording",false}});
                std::unique_lock lock(wait_mutex_);
                wake_.wait_for(lock, std::chrono::seconds(1), [this] { return stopping_.load() || !paused_.load(); });
                continue;
            }
            try {
                if (!storage_writable_) { camera_change(camera, {{"state","storage_blocked"}}); wait(5); continue; }
                camera.live.reset();
                ProbeConfig config;
                config.synthetic = options_.synthetic;
                config.detect_people = !options_.synthetic;
                config.test_recording = options_.synthetic;
                config.continuous = true;
                config.camera_id = camera.source.id.toStdString();
                config.scheduler = scheduler_;
                config.live_packet = [&camera](const EncodedPacket& packet) { camera.live.publish(packet); };
                config.project_root = native_path(options_.root);
                config.output = native_path(options_.data + "/recordings");
                if (options_.synthetic) { config.post_roll = std::chrono::seconds(3); config.segment_duration = std::chrono::seconds(2); }
                if (!options_.synthetic) {
                    const auto credential = load_camera_credential(camera.source.credential);
                    config.host = credential.host.toStdString(); config.username = credential.username.toStdString();
                    config.password = credential.password.toStdString();
                }
                camera_change(camera, {{"state","connecting"},{"attempts",++attempts}, {"streamReady",false}, {"aiReady",false}, {"recording",false}});
                config.cancelled = [&] { return should_stop() || paused_.load(); };
                config.progress = [this, &camera](const std::string& message) { camera_change(camera, {{"message",QString::fromStdString(message)}}); };
                config.recording_update = [this, &camera](const RecordingUpdate& update) {
                    store_->update(update);
                    if (update.stage == RecordingStage::event_started) camera_change(camera, {{"recording",true}});
                    if (update.stage == RecordingStage::event_finished) {
                        camera_change(camera, {{"recording",false},{"lastEvent",QString::fromStdString(update.event_id)}});
                        change({{"lastEvent",QString::fromStdString(update.event_id)}});
                    }
                };
                config.heartbeat = [this, &camera](const ProbeReport& report) {
                    const bool ready = report.packets > 0 && (options_.synthetic || report.analyzed_frames > 0);
                    camera_change(camera, {{"state",ready ? "running" : "warming_up"}, {"streamReady",report.packets > 0},
                        {"aiReady",!options_.synthetic && report.analyzed_frames > 0},
                        {"packets",static_cast<qint64>(report.packets)}, {"analyzedFrames",static_cast<qint64>(report.analyzed_frames)},
                        {"personFrames",static_cast<qint64>(report.person_frames)}, {"inferenceMs",report.average_inference_ms}});
                };
                config.storage_ready = [this] { return storage_writable_.load(); };
                const auto report = run_probe(config);
                if (should_stop() || paused_) continue;
                if (report.elapsed_seconds >= 30) failures = 0;
                const auto error = report.error.empty() ? "Media session ended unexpectedly" : report.error;
                store_->incident("session_error", camera.source.id.toStdString() + ": " + error, QDateTime::currentMSecsSinceEpoch());
                const auto delay = std::min(15, 1 << std::min(failures++, 4));
                camera.live.reset();
                camera_change(camera, {{"state","reconnecting"},{"streamReady",false},{"aiReady",false},{"recording",false},
                    {"message",QString::fromStdString(error)},{"retrySeconds",delay}});
                wait(delay);
            } catch (const std::exception& error) {
                camera.live.reset();
                camera_change(camera, {{"state","configuration_or_storage_error"},{"streamReady",false},{"aiReady",false},{"recording",false},
                    {"message",QString::fromUtf8(error.what())}});
                wait(5);
            }
        }
        camera.live.reset();
        camera_change(camera, {{"state","stopped"},{"streamReady",false},{"aiReady",false},{"recording",false}});
    }
    void supervise() noexcept {
        const auto began = std::chrono::steady_clock::now();
        auto next_cleanup = began;
        while (!stopping_) {
            if (options_.run_seconds > 0 && std::chrono::steady_clock::now() - began >= std::chrono::seconds(options_.run_seconds)) { stopping_ = true; wake_.notify_all(); break; }
            if (std::chrono::steady_clock::now() >= next_cleanup) {
                next_cleanup = std::chrono::steady_clock::now() + std::chrono::seconds(10);
                try {
                    const auto current = store_->retain(QDateTime::currentMSecsSinceEpoch(), policy_);
                    storage_writable_ = current.writable;
                    change({{"storageReady",current.writable},{"storageBytes",static_cast<qint64>(current.used_bytes)},
                        {"freeBytes",static_cast<qint64>(current.free_bytes)},{"message",QString::fromStdString(current.error)}});
                } catch (const std::exception& error) {
                    storage_writable_ = false;
                    change({{"storageReady",false},{"message",QString::fromUtf8(error.what())}});
                }
            }
            for (const auto& camera : cameras_) if (!camera->worker.joinable())
                camera->worker = std::jthread([this, pointer = camera.get()] { camera_loop(*pointer); });
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        for (const auto& camera : cameras_) if (camera->worker.joinable()) camera->worker.join();
    }

    EngineOptions options_;
    RetentionPolicy policy_;
    std::unique_ptr<QLockFile> lock_;
    std::unique_ptr<EventStore> store_;
    std::unique_ptr<LocalPipeServer> pipe_;
    std::mutex state_mutex_;
    QJsonObject state_;
    std::atomic_bool stopping_{false};
    std::atomic_bool paused_{false};
    std::mutex wait_mutex_;
    std::condition_variable wake_;
    std::jthread worker_;
    struct Camera {
        CameraSource source;
        QJsonObject state;
        LiveHub live;
        std::unique_ptr<LocalPipeServer> pipe;
        std::jthread worker;
    };
    std::vector<std::unique_ptr<Camera>> cameras_;
    std::shared_ptr<InferenceScheduler> scheduler_;
    std::atomic_bool storage_writable_{false};
};

EngineHost::EngineHost(EngineOptions options) : impl_(std::make_unique<Impl>(std::move(options))) {}
EngineHost::~EngineHost() = default;
void EngineHost::start() { impl_->start(); }
void EngineHost::stop() { impl_->stop(); }
bool EngineHost::stop_requested() const { return impl_->stopping(); }

} // namespace opennvr
