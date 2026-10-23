#include "media/probe.hpp"
#include "media/encoded_ring.hpp"
#include "media/event_writer.hpp"
#include "detection/person_stream.hpp"

#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <gst/rtsp/gstrtsptransport.h>

#include <cstring>
#include <algorithm>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <atomic>
#include <random>

namespace opennvr {
namespace {
struct Pipeline {
    GstElement* value = nullptr;
    ~Pipeline() {
        if (value) {
            gst_element_set_state(value, GST_STATE_NULL);
            gst_object_unref(value);
        }
    }
};
struct ObjectUnref { void operator()(gpointer value) const { if (value) gst_object_unref(value); } };
using ElementPtr = std::unique_ptr<GstElement, ObjectUnref>;
using BusPtr = std::unique_ptr<GstBus, ObjectUnref>;

void progress(const ProbeConfig& config, const std::string& message) {
    if (config.progress) config.progress(message);
}
} // namespace

void initialize_media() {
    static std::once_flag initialized;
    std::call_once(initialized, [] { gst_init(nullptr, nullptr); });
}

std::vector<std::string> media_capabilities() {
    initialize_media();
    std::vector<std::string> result;
    for (const auto* name : {"rtspsrc", "rtph264depay", "h264parse", "d3d11h264dec",
                            "d3d11videosink", "qml6d3d11sink", "openh264enc", "matroskamux"}) {
        GstElementFactory* factory = gst_element_factory_find(name);
        result.push_back(std::string(name) + (factory ? ": available" : ": unavailable"));
        if (factory) gst_object_unref(factory);
    }
    return result;
}

ProbeReport run_probe(const ProbeConfig& config) {
    initialize_media();
    ProbeReport report;
    auto started = std::chrono::steady_clock::now();
    std::unique_ptr<EventWriter> writer;
    RecordingUpdate current;
    current.camera_id = config.camera_id;
    bool event_open = false;
    std::int64_t session_utc_ms = 0;
    MonoTime segment_start{};
    auto notify = [&](RecordingStage stage) {
        current.stage = stage;
        if (config.recording_update) config.recording_update(current);
    };
    auto finish_segment = [&] {
        if (!writer) return;
        report.recorded_packets += writer->packets();
        try {
            writer->finish();
            writer.reset();
            auto partial = current.file; partial += ".part";
            std::filesystem::rename(partial, current.file);
            current.status = "complete";
            notify(RecordingStage::segment_finished);
        } catch (...) {
            writer.reset();
            current.status = "partial";
            try { notify(RecordingStage::segment_finished); } catch (...) {}
            throw;
        }
    };
    auto finish_event = [&](const std::string& status, const std::string& reason) {
        if (!event_open) return;
        finish_segment();
        current.status = status;
        current.reason = reason;
        notify(RecordingStage::event_finished);
        event_open = false;
    };
    auto start_segment = [&](const EncodedPacket& first) {
        current.start_utc_ms = session_utc_ms + std::chrono::duration_cast<std::chrono::milliseconds>(first.received).count();
        current.end_utc_ms = current.start_utc_ms;
        current.file = std::filesystem::absolute(config.output / current.event_id /
            ("segment-" + std::to_string(current.segment) + ".mkv"));
        std::filesystem::create_directories(current.file.parent_path());
        current.status = "writing";
        notify(RecordingStage::segment_started);
        auto partial = current.file; partial += ".part";
        writer = std::make_unique<EventWriter>(partial, first);
        segment_start = first.received;
        report.recording = current.file;
    };
    try {
        if (!config.continuous && (config.duration.count() < 1 || config.duration > std::chrono::minutes(10))) {
            throw std::invalid_argument("Probe duration must be between 1 and 600 seconds");
        }
        if (config.pre_roll < std::chrono::seconds::zero() || config.pre_roll > std::chrono::seconds(60) ||
            config.post_roll <= std::chrono::seconds::zero() || config.segment_duration <= std::chrono::seconds::zero())
            throw std::invalid_argument("Invalid recording timing configuration");
        if (!config.synthetic && (config.host.empty() ||
            config.host.find_first_not_of("0123456789.") != std::string::npos)) {
            throw std::invalid_argument("An IPv4 camera address is required");
        }
        if (config.detect_people && (config.synthetic || config.test_recording)) {
            throw std::invalid_argument("AI camera mode and manual/synthetic test triggers must be separate");
        }
        std::unique_ptr<PersonStream> analysis;
        if (config.detect_people) {
            progress(config, "Memuat dan warmup model person di CUDA...");
            analysis = std::make_unique<PersonStream>(config.project_root, config.host, config.username, config.password, config.scheduler, config.live_packet);
            progress(config, "AI aktif: CUDA / YOLOX-Tiny; menunggu orang pada substream.");
        }
        const std::string input = config.synthetic
            ? "videotestsrc is-live=true pattern=ball ! video/x-raw,width=640,height=360,framerate=15/1 "
              "! videoconvert ! openh264enc gop-size=30 bitrate=1500000 "
            : "rtspsrc name=camera latency=150 protocols=tcp tcp-timeout=5000000 "
              "! application/x-rtp,media=video,encoding-name=H264 ! rtph264depay ";
        std::string description = input + "! h264parse config-interval=-1 "
            "! video/x-h264,stream-format=byte-stream,alignment=au ! tee name=video "
            "video. ! queue max-size-buffers=120 max-size-bytes=0 max-size-time=0 "
            "! appsink name=packets sync=false max-buffers=120 drop=false ";
        if (config.preview) {
            description += "video. ! queue max-size-buffers=2 leaky=downstream "
                "! d3d11h264dec ! d3d11videosink sync=false";
        }
        GError* error = nullptr;
        Pipeline pipeline{gst_parse_launch(description.c_str(), &error)};
        if (error || !pipeline.value) {
            if (error) g_error_free(error);
            throw std::runtime_error("Media pipeline could not be created; check installed plugins");
        }
        ElementPtr sink(gst_bin_get_by_name(GST_BIN(pipeline.value), "packets"));
        if (!config.synthetic) {
            ElementPtr source(gst_bin_get_by_name(GST_BIN(pipeline.value), "camera"));
            const auto location = "rtsp://" + config.host + ":554/" +
                (config.substream ? "stream2" : "stream1");
            // Credentials stay in memory as separate properties, never in a URI or launch command.
            g_object_set(source.get(), "location", location.c_str(), "user-id", config.username.c_str(),
                         "user-pw", config.password.c_str(), nullptr);
        }
        BusPtr bus(gst_element_get_bus(pipeline.value));
        started = std::chrono::steady_clock::now();
        session_utc_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        if (gst_element_set_state(pipeline.value, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
            throw std::runtime_error("Media pipeline could not start");
        }
        EncodedRing ring(config.pre_roll + std::chrono::seconds(5), 64 * 1024 * 1024);
        std::optional<MonoTime> last_keyframe;
        ConfirmationGate confirmation;
        EventWindow event(config.post_roll);
        MonoTime next_analysis{};
        MonoTime last_analysis{};
        auto last_packet = started;
        auto next_heartbeat = started;
        bool attempted_recording = false;
        while (config.continuous || std::chrono::steady_clock::now() - started < config.duration) {
            if (config.cancelled && config.cancelled()) { report.cancelled = true; break; }
            const auto now = std::chrono::steady_clock::now();
            if (now >= next_heartbeat) {
                next_heartbeat = now + std::chrono::seconds(1);
                report.elapsed_seconds = std::chrono::duration<double>(now - started).count();
                if (config.heartbeat) config.heartbeat(report);
                if (config.storage_ready && !config.storage_ready())
                    throw std::runtime_error("Storage unavailable: recording quota or free-space reserve reached");
            }
            if (GstMessage* message = gst_bus_pop_filtered(bus.get(),
                static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS))) {
                const bool failed = GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR;
                gst_message_unref(message);
                if (failed) throw std::runtime_error("Stream failed: check camera connectivity, credentials and H.264 support");
                throw std::runtime_error("Video stream ended unexpectedly");
            }
            GstSample* sample = gst_app_sink_try_pull_sample(GST_APP_SINK(sink.get()), 100 * GST_MSECOND);
            if (!sample) {
                if (std::chrono::steady_clock::now() - last_packet > std::chrono::seconds(10))
                    throw std::runtime_error("Video stream stopped producing frames");
                continue;
            }
            last_packet = std::chrono::steady_clock::now();
            GstBuffer* buffer = gst_sample_get_buffer(sample);
            GstCaps* caps = gst_sample_get_caps(sample);
            gchar* caps_text = gst_caps_to_string(caps);
            EncodedPacket packet;
            packet.received = std::chrono::duration_cast<MonoTime>(std::chrono::steady_clock::now() - started);
            packet.pts = GST_BUFFER_PTS(buffer);
            packet.dts = GST_BUFFER_DTS(buffer);
            packet.duration = GST_BUFFER_DURATION(buffer);
            packet.keyframe = !GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT);
            packet.caps = caps_text;
            g_free(caps_text);
            const auto size = gst_buffer_get_size(buffer);
            auto data = std::make_shared<std::vector<std::byte>>(size);
            gst_buffer_extract(buffer, 0, data->data(), size);
            packet.data = data;
            if (config.synthetic && config.live_packet) config.live_packet(packet);
            const auto* structure = gst_caps_get_structure(caps, 0);
            gst_structure_get_int(structure, "width", &report.width);
            gst_structure_get_int(structure, "height", &report.height);
            gst_sample_unref(sample);
            report.caps = packet.caps;
            ++report.packets;
            report.bytes += size;
            if (packet.keyframe) {
                ++report.keyframes;
                if (last_keyframe) {
                    report.maximum_gop_seconds = std::max(report.maximum_gop_seconds,
                        std::chrono::duration<double>(packet.received - *last_keyframe).count());
                }
                last_keyframe = packet.received;
            }
            ring.push(packet);
            report.ring_bytes = ring.bytes();
            if (writer) {
                if (packet.keyframe && packet.received - segment_start >= config.segment_duration) {
                    finish_segment();
                    ++current.segment;
                    start_segment(packet);
                }
                writer->push(packet);
                current.end_utc_ms = session_utc_ms + std::chrono::duration_cast<std::chrono::milliseconds>(packet.received).count();
            }
            bool start_event = false;
            if (analysis && packet.received >= next_analysis) {
                next_analysis = packet.received + std::chrono::milliseconds(333);
                if (const auto result = analysis->poll()) {
                    last_analysis = packet.received;
                    ++report.analyzed_frames;
                    const bool person = !result->people.empty();
                    if (person) ++report.person_frames;
                    report.average_inference_ms += (result->inference_ms - report.average_inference_ms) / report.analyzed_frames;
                    if (confirmation.sample(packet.received, person)) {
                        ++report.confirmed_frames;
                        start_event = event.person_seen(packet.received);
                    }
                    if (report.analyzed_frames % 15 == 0 || start_event) {
                        std::ostringstream status;
                        status << "AI: " << result->people.size() << " orang | " << result->inference_ms
                               << " ms | " << report.analyzed_frames << " frame dianalisis";
                        progress(config, status.str());
                    }
                } else if (packet.received - last_analysis > std::chrono::seconds(10)) {
                    throw std::runtime_error("AI substream stopped producing frames");
                }
            }
            const bool manual_start = config.test_recording && !attempted_recording && packet.received >= std::chrono::seconds(6);
            if (manual_start || start_event) {
                attempted_recording = true;
                if (manual_start) event.person_seen(packet.received);
                const auto history = ring.snapshot_from(packet.received - (analysis ? config.pre_roll : std::chrono::seconds(4)));
                if (history.empty()) throw std::runtime_error("No keyframe available for recording");
                static std::atomic_uint64_t sequence{0};
                std::random_device random;
                const auto id = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                current.event_id = std::to_string(id) + "-" + std::to_string(random()) + "-" + std::to_string(sequence++);
                current.segment = 0;
                current.reason.clear();
                current.status = "recording";
                current.start_utc_ms = session_utc_ms + std::chrono::duration_cast<std::chrono::milliseconds>(history.front().received).count();
                notify(RecordingStage::event_started);
                event_open = true;
                start_segment(history.front());
                for (const auto& previous : history) writer->push(previous);
                current.end_utc_ms = session_utc_ms + std::chrono::duration_cast<std::chrono::milliseconds>(packet.received).count();
                ++report.events;
                progress(config, analysis ? "Orang terkonfirmasi: rekaman event dimulai, termasuk buffer pre-roll."
                    : "Pemicu uji manual: buffer sebelum kejadian sedang direkam (bukan deteksi AI).");
            }
            if (event.expire(packet.received) && event_open) {
                finish_event("complete", "post_roll_elapsed");
                progress(config, "Event selesai setelah post-roll tanpa konfirmasi person baru.");
            }
            if (report.packets == 1 || report.packets % 30 == 0) {
                std::ostringstream message;
                message << report.width << 'x' << report.height << " H.264 | " << report.packets
                        << " paket | " << report.keyframes << " keyframe | buffer " << ring.bytes() / 1024 << " KiB";
                progress(config, message.str());
            }
        }
        if (event_open) {
            report.stopped_during_event = event.active();
            finish_event("interrupted", "session_stopped");
        }
        if (report.packets == 0 && !report.cancelled) throw std::runtime_error("No H.264 video received within the probe timeout");
        if (config.test_recording && !attempted_recording && !report.cancelled) {
            throw std::runtime_error("Probe ended before the test recording trigger");
        }
        if (analysis && report.analyzed_frames == 0 && !report.cancelled) {
            throw std::runtime_error("No frames analyzed by the person detector");
        }
        report.success = !report.cancelled;
    } catch (const std::exception& error) {
        report.error = error.what();
        if (event_open) {
            report.stopped_during_event = true;
            try { finish_event("interrupted", "stream_or_storage_error"); }
            catch (...) {
                current.status = "partial";
                current.reason = "finalization_failed";
                try { notify(RecordingStage::event_finished); } catch (...) {}
            }
        }
    }
    report.elapsed_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return report;
}

bool validate_recording(const std::filesystem::path& file, bool hardware_decode) {
    initialize_media();
    const std::string decode = hardware_decode ? "d3d11h264dec" : "openh264dec";
    GError* error = nullptr;
    Pipeline pipeline{gst_parse_launch(("filesrc name=file ! matroskademux ! h264parse ! " +
        decode + " ! fakesink sync=false").c_str(), &error)};
    if (error || !pipeline.value) { if (error) g_error_free(error); return false; }
    ElementPtr source(gst_bin_get_by_name(GST_BIN(pipeline.value), "file"));
    const auto utf8 = file.u8string();
    g_object_set(source.get(), "location", reinterpret_cast<const char*>(utf8.c_str()), nullptr);
    BusPtr bus(gst_element_get_bus(pipeline.value));
    if (gst_element_set_state(pipeline.value, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) return false;
    GstMessage* message = gst_bus_timed_pop_filtered(bus.get(), 15 * GST_SECOND,
        static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
    const bool ok = message && GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS;
    if (message) gst_message_unref(message);
    return ok;
}

bool play_recordings(const std::vector<std::filesystem::path>& files, const std::function<bool()>& cancelled) {
    initialize_media();
    for (const auto& file : files) {
        if (cancelled()) return false;
        Pipeline pipeline{gst_element_factory_make("playbin", nullptr)};
        if (!pipeline.value) return false;
        const auto utf8 = file.u8string();
        gchar* uri = gst_filename_to_uri(reinterpret_cast<const char*>(utf8.c_str()), nullptr);
        GstElement* video = gst_element_factory_make("d3d11videosink", nullptr);
        GstElement* audio = gst_element_factory_make("fakesink", nullptr);
        if (!uri || !video || !audio) {
            if (uri) g_free(uri);
            if (video) gst_object_unref(video);
            if (audio) gst_object_unref(audio);
            return false;
        }
        gst_object_ref_sink(video); gst_object_ref_sink(audio);
        g_object_set(pipeline.value, "uri", uri, "video-sink", video, "audio-sink", audio, nullptr);
        g_free(uri); gst_object_unref(video); gst_object_unref(audio);
        BusPtr bus(gst_element_get_bus(pipeline.value));
        if (gst_element_set_state(pipeline.value, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) return false;
        for (;;) {
            if (cancelled()) return false;
            GstMessage* message = gst_bus_timed_pop_filtered(bus.get(), 100 * GST_MSECOND,
                static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
            if (!message) continue;
            const bool ended = GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS;
            gst_message_unref(message);
            if (!ended) return false;
            break;
        }
    }
    return true;
}

} // namespace opennvr
