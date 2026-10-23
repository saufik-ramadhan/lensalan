#include "engine/host.hpp"
#include "media/probe.hpp"
#include "platform/credentials.hpp"
#include "platform/local_pipe.hpp"

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
public:
    explicit Impl(EngineOptions options) : options_(std::move(options)) {
        options_.root = QDir(options_.root).absolutePath();
        options_.data = QDir(options_.data).absolutePath();
        if (!QDir().mkpath(options_.data)) throw std::runtime_error("Cannot create engine data directory");
        lock_ = std::make_unique<QLockFile>(options_.data + "/engine.lock");
        lock_->setStaleLockTime(0);
        if (!lock_->tryLock(0)) throw std::runtime_error("An engine already owns this data directory");
        const auto config_directory = options_.service ? options_.data + "/config" : options_.data;
        QFile settings(config_directory + "/engine.json");
        if (settings.exists()) {
            if (!settings.open(QIODevice::ReadOnly) || settings.size() > 65536) throw std::runtime_error("Engine settings cannot be read");
            const auto document = QJsonDocument::fromJson(settings.readAll());
            if (!document.isObject()) throw std::runtime_error("Invalid engine settings");
            const auto value = document.object();
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
        if (options_.credential.isEmpty()) {
            options_.credential = config_directory + "/camera.json";
            if (!options_.service && !QFileInfo::exists(options_.credential))
                options_.credential = options_.root + "/.local/camera.json";
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
    bool wait(int seconds) {
        std::unique_lock lock(wait_mutex_);
        wake_.wait_for(lock, std::chrono::seconds(seconds), [this] { return stopping_.load() || paused_.load(); });
        return !stopping_;
    }
    void supervise() noexcept {
        int failures = 0;
        int attempts = 0;
        const auto began = std::chrono::steady_clock::now();
        auto should_stop = [&] {
            if (options_.run_seconds > 0 && std::chrono::steady_clock::now() - began >= std::chrono::seconds(options_.run_seconds)) stopping_ = true;
            return stopping_.load();
        };
        while (!should_stop()) {
            if (paused_) {
                change({{"state","paused"}, {"streamReady",false}, {"aiReady",false}, {"recording",false}});
                std::unique_lock lock(wait_mutex_);
                wake_.wait_for(lock, std::chrono::seconds(1), [this] { return stopping_.load() || !paused_.load(); });
                continue;
            }
            try {
                const auto storage = store_->retain(QDateTime::currentMSecsSinceEpoch(), policy_);
                change({{"storageReady",storage.writable}, {"storageBytes",static_cast<qint64>(storage.used_bytes)},
                    {"freeBytes",static_cast<qint64>(storage.free_bytes)}});
                if (!storage.writable) { change({{"state","storage_blocked"},{"message",QString::fromStdString(storage.error)}}); wait(5); continue; }
                ProbeConfig config;
                config.synthetic = options_.synthetic;
                config.detect_people = !options_.synthetic;
                config.test_recording = options_.synthetic;
                config.continuous = true;
                config.project_root = native_path(options_.root);
                config.output = native_path(options_.data + "/recordings");
                if (options_.synthetic) { config.post_roll = std::chrono::seconds(3); config.segment_duration = std::chrono::seconds(2); }
                if (!options_.synthetic) {
                    const auto credential = load_camera_credential(options_.credential);
                    config.host = credential.host.toStdString(); config.username = credential.username.toStdString();
                    config.password = credential.password.toStdString();
                }
                change({{"state","connecting"},{"attempts",++attempts}, {"streamReady",false}, {"aiReady",false}, {"recording",false}});
                config.cancelled = [&] { return should_stop() || paused_.load(); };
                config.progress = [this](const std::string& message) { change({{"message",QString::fromStdString(message)}}); };
                config.recording_update = [this](const RecordingUpdate& update) {
                    store_->update(update);
                    if (update.stage == RecordingStage::event_started) change({{"recording",true}});
                    if (update.stage == RecordingStage::event_finished) change({{"recording",false},{"lastEvent",QString::fromStdString(update.event_id)}});
                };
                config.heartbeat = [this](const ProbeReport& report) {
                    const bool ready = report.packets > 0 && (options_.synthetic || report.analyzed_frames > 0);
                    change({{"state",ready ? "running" : "warming_up"}, {"streamReady",report.packets > 0},
                        {"aiReady",!options_.synthetic && report.analyzed_frames > 0},
                        {"packets",static_cast<qint64>(report.packets)}, {"analyzedFrames",static_cast<qint64>(report.analyzed_frames)},
                        {"personFrames",static_cast<qint64>(report.person_frames)}, {"inferenceMs",report.average_inference_ms}});
                };
                auto next_cleanup = std::chrono::steady_clock::now();
                bool writable = true;
                config.storage_ready = [&, this] {
                    if (std::chrono::steady_clock::now() >= next_cleanup) {
                        next_cleanup = std::chrono::steady_clock::now() + std::chrono::seconds(10);
                        const auto current = store_->retain(QDateTime::currentMSecsSinceEpoch(), policy_);
                        writable = current.writable;
                        change({{"storageReady",writable},{"storageBytes",static_cast<qint64>(current.used_bytes)},
                            {"freeBytes",static_cast<qint64>(current.free_bytes)}});
                    }
                    return writable;
                };
                const auto report = run_probe(config);
                if (should_stop() || paused_) continue;
                if (report.elapsed_seconds >= 30) failures = 0;
                const auto error = report.error.empty() ? "Media session ended unexpectedly" : report.error;
                store_->incident("session_error", error, QDateTime::currentMSecsSinceEpoch());
                const auto delay = std::min(15, 1 << std::min(failures++, 4));
                change({{"state","reconnecting"},{"streamReady",false},{"aiReady",false},{"recording",false},
                    {"message",QString::fromStdString(error)},{"retrySeconds",delay}});
                wait(delay);
            } catch (const std::exception& error) {
                change({{"state","configuration_or_storage_error"},{"streamReady",false},{"aiReady",false},
                    {"message",QString::fromUtf8(error.what())}});
                wait(5);
            }
        }
        change({{"state","stopped"},{"streamReady",false},{"aiReady",false},{"recording",false}});
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
};

EngineHost::EngineHost(EngineOptions options) : impl_(std::make_unique<Impl>(std::move(options))) {}
EngineHost::~EngineHost() = default;
void EngineHost::start() { impl_->start(); }
void EngineHost::stop() { impl_->stop(); }
bool EngineHost::stop_requested() const { return impl_->stopping(); }

} // namespace opennvr
